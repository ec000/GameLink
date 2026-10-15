package api

import (
	"context"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"sync"
	"time"

	"gamelink/server/internal/controller"
)

type User struct {
	ID        string `json:"id"`
	NodeID    string `json:"nodeId"`
	TokenHash string `json:"tokenHash"`
}
type Config struct {
	Listen              string   `json:"listen"`
	ControllerURL       string   `json:"controllerUrl"`
	ControllerTokenFile string   `json:"controllerTokenFile"`
	StateFile           string   `json:"stateFile"`
	Networks            []string `json:"networks"`
	Users               []User   `json:"users"`
	TTLSeconds          int      `json:"ttlSeconds"`
}
type Membership struct {
	UserID     string `json:"userId"`
	NodeID     string `json:"nodeId"`
	Authorized bool   `json:"authorized"`
}
type Room struct {
	ID        string                `json:"id"`
	NetworkID string                `json:"networkId"`
	GameID    string                `json:"gameId"`
	Policy    string                `json:"policy"`
	Owner     string                `json:"owner"`
	ExpiresAt time.Time             `json:"expiresAt"`
	Closing   bool                  `json:"closing"`
	Members   map[string]Membership `json:"members"`
	JoinKey   string                `json:"joinKey,omitempty"`
}
type journal struct {
	Mock    bool             `json:"mock"`
	Version int              `json:"version"`
	Rooms   map[string]*Room `json:"rooms"`
}
type bucket struct {
	Start time.Time
	Count int
}
type Server struct {
	mu        sync.Mutex
	cfg       Config
	backend   controller.Backend
	rooms     map[string]*Room
	buckets   map[string]bucket
	now       func() time.Time
	mock      bool
	unhealthy bool
}

var hexNode = regexp.MustCompile(`^[0-9a-f]{10}$`)
var hexNetwork = regexp.MustCompile(`^[0-9a-f]{16}$`)
var userID = regexp.MustCompile(`^[a-zA-Z0-9_-]{1,64}$`)

func New(ctx context.Context, cfg Config, backend controller.Backend, mock bool) (*Server, error) {
	if cfg.StateFile == "" || len(cfg.Networks) == 0 || len(cfg.Users) == 0 || cfg.TTLSeconds < 30 || cfg.TTLSeconds > 86400 {
		return nil, errors.New("invalid config")
	}
	seenUsers, seenNodes, seenTokens, seenNetworks := map[string]bool{}, map[string]bool{}, map[string]bool{}, map[string]bool{}
	for _, u := range cfg.Users {
		hash, err := hex.DecodeString(u.TokenHash)
		if !userID.MatchString(u.ID) || !hexNode.MatchString(u.NodeID) || err != nil || len(hash) != 32 || hex.EncodeToString(hash) != u.TokenHash || seenUsers[u.ID] || seenNodes[u.NodeID] || seenTokens[u.TokenHash] {
			return nil, errors.New("invalid or duplicate user binding")
		}
		seenUsers[u.ID] = true
		seenNodes[u.NodeID] = true
		seenTokens[u.TokenHash] = true
	}
	for _, n := range cfg.Networks {
		if !hexNetwork.MatchString(n) || seenNetworks[n] {
			return nil, errors.New("invalid or duplicate network")
		}
		seenNetworks[n] = true
	}
	s := &Server{cfg: cfg, backend: backend, rooms: map[string]*Room{}, buckets: map[string]bucket{}, now: time.Now, mock: mock}
	if b, err := os.ReadFile(cfg.StateFile); err == nil {
		var j journal
		if json.Unmarshal(b, &j) != nil || j.Version != 1 || j.Rooms == nil || j.Mock != mock {
			return nil, errors.New("invalid journal; manual recovery required")
		}
		s.rooms = j.Rooms
	} else if !os.IsNotExist(err) {
		return nil, err
	}
	// Validate ownership before any writes to the existing Controller.
	known := map[string]map[string]bool{}
	for _, r := range s.rooms {
		if r == nil || !seenNetworks[r.NetworkID] {
			return nil, errors.New("journal network outside configured pool")
		}
		if known[r.NetworkID] == nil {
			known[r.NetworkID] = map[string]bool{}
		}
		for _, m := range r.Members {
			if !hexNode.MatchString(m.NodeID) {
				return nil, errors.New("invalid journal node")
			}
			known[r.NetworkID][m.NodeID] = true
		}
	}
	for _, n := range cfg.Networks {
		if err := backend.Validate(ctx, n, known[n]); err != nil {
			return nil, err
		}
	}
	// Restart invalidates old rooms. Durable intent is never silently discarded.
	for _, r := range s.rooms {
		r.Closing = true
	}
	if err := s.save(); err != nil {
		return nil, err
	}
	if err := s.cleanup(ctx); err != nil {
		return nil, errors.New("restart revocation incomplete; retry startup")
	}
	return s, nil
}
func (s *Server) save() error {
	b, err := json.Marshal(journal{Mock: s.mock, Version: 1, Rooms: s.rooms})
	if err != nil {
		return err
	}
	dir := filepath.Dir(s.cfg.StateFile)
	if err = os.MkdirAll(dir, 0700); err != nil {
		return err
	}
	f, err := os.CreateTemp(dir, ".gamelink-state-")
	if err != nil {
		return err
	}
	name := f.Name()
	defer os.Remove(name)
	if err = f.Chmod(0600); err == nil {
		_, err = f.Write(b)
	}
	if err == nil {
		err = f.Sync()
	}
	closeErr := f.Close()
	if err == nil {
		err = closeErr
	}
	if err == nil {
		err = os.Rename(name, s.cfg.StateFile)
	}
	if err == nil {
		d, e := os.Open(dir)
		if e != nil {
			return e
		}
		defer d.Close()
		err = d.Sync()
	}
	if err != nil {
		s.unhealthy = true
	}
	return err
}
func (s *Server) cleanup(ctx context.Context) error {
	var all error
	for id, r := range s.rooms {
		if !r.Closing && s.now().Before(r.ExpiresAt) {
			continue
		}
		r.Closing = true
		if err := s.save(); err != nil {
			return err
		}
		failed := false
		for _, m := range r.Members {
			if err := s.backend.Authorize(ctx, r.NetworkID, m.NodeID, false); err != nil {
				failed = true
				all = err
			}
		}
		if !failed {
			delete(s.rooms, id)
			if err := s.save(); err != nil {
				return err
			}
		}
	}
	return all
}
func (s *Server) Sweep(ctx context.Context) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.cleanup(ctx)
}
func randomID(n int) string {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}
func (s *Server) authenticate(r *http.Request) (User, bool) {
	token := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
	if token == r.Header.Get("Authorization") || len(token) < 32 || len(token) > 256 {
		return User{}, false
	}
	sum := sha256.Sum256([]byte(token))
	for _, u := range s.cfg.Users {
		h, _ := hex.DecodeString(u.TokenHash)
		if subtle.ConstantTimeCompare(sum[:], h) == 1 {
			return u, true
		}
	}
	return User{}, false
}
func (s *Server) limit(key string, max int) bool {
	now := s.now()
	if len(s.buckets) >= 4096 {
		for k, b := range s.buckets {
			if now.Sub(b.Start) >= time.Minute {
				delete(s.buckets, k)
			}
		}
		if _, ok := s.buckets[key]; !ok && len(s.buckets) >= 4096 {
			return false
		}
	}
	b := s.buckets[key]
	if now.Sub(b.Start) >= time.Minute {
		b = bucket{Start: now}
	}
	b.Count++
	s.buckets[key] = b
	return b.Count <= max
}
func respond(w http.ResponseWriter, status int, v any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	if v != nil {
		_ = json.NewEncoder(w).Encode(v)
	}
}
func fail(w http.ResponseWriter, status int, code string) {
	respond(w, status, map[string]string{"error": code})
}
func body(w http.ResponseWriter, r *http.Request, out any) bool {
	dec := json.NewDecoder(http.MaxBytesReader(w, r.Body, 8192))
	dec.DisallowUnknownFields()
	if err := dec.Decode(out); err != nil {
		fail(w, 400, "invalid_json")
		return false
	}
	var extra any
	if dec.Decode(&extra) != io.EOF {
		fail(w, 400, "trailing_json")
		return false
	}
	return true
}
func public(r *Room) Room { copy := *r; copy.JoinKey = ""; return copy }
func (s *Server) inRoom(user string) bool {
	for _, r := range s.rooms {
		if _, ok := r.Members[user]; ok {
			return true
		}
	}
	return false
}
func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mu.Lock()
	defer s.mu.Unlock()
	host, _, _ := net.SplitHostPort(r.RemoteAddr)
	if !s.limit("ip:"+host, 300) {
		fail(w, 429, "rate_limited")
		return
	}
	u, ok := s.authenticate(r)
	if !ok {
		log.Printf("auth_rejected")
		fail(w, 401, "unauthorized")
		return
	}
	if !s.limit("user:"+u.ID, 120) {
		fail(w, 429, "rate_limited")
		return
	}
	// Never log raw URL, request body, invitation or Authorization header.
	log.Printf("api_request user=%s method=%s", u.ID, r.Method)
	if s.unhealthy {
		fail(w, 503, "storage_unhealthy_restart_required")
		return
	}
	if r.URL.Path == "/api/v1/health" && r.Method == "GET" {
		mode := "controller"
		if s.mock {
			mode = "mock"
		}
		respond(w, 200, map[string]any{"status": "ok", "mode": mode, "strictDirectSupported": false})
		return
	}
	if r.URL.Path == "/api/v1/rooms" && r.Method == "POST" {
		s.create(w, r, u)
		return
	}
	p := strings.Split(strings.TrimPrefix(r.URL.Path, "/api/v1/rooms/"), "/")
	if !strings.HasPrefix(r.URL.Path, "/api/v1/rooms/") || len(p) > 2 {
		fail(w, 404, "not_found")
		return
	}
	room, ok := s.rooms[p[0]]
	if !ok {
		fail(w, 404, "room_not_found")
		return
	}
	if !s.now().Before(room.ExpiresAt) || room.Closing {
		fail(w, 409, "room_expired_or_closing")
		return
	}
	if len(p) == 2 && p[1] == "join" && r.Method == "POST" {
		s.join(w, r, u, room)
		return
	}
	member, isMember := room.Members[u.ID]
	if !isMember || !member.Authorized {
		fail(w, 403, "not_member")
		return
	}
	if len(p) == 1 && r.Method == "GET" {
		respond(w, 200, public(room))
		return
	}
	if len(p) == 2 && p[1] == "peers" && r.Method == "GET" {
		peers := []any{}
		for _, m := range room.Members {
			cm, err := s.backend.Member(r.Context(), room.NetworkID, m.NodeID)
			if err != nil {
				fail(w, 503, "controller_unavailable")
				return
			}
			peers = append(peers, map[string]any{"userId": m.UserID, "nodeId": m.NodeID, "authorized": m.Authorized && cm.Authorized, "virtualIps": cm.IPAssignments})
		}
		respond(w, 200, map[string]any{"peers": peers})
		return
	}
	if len(p) == 2 && p[1] == "leave" && r.Method == "POST" {
		var req struct{}
		if !body(w, r, &req) {
			return
		}
		if room.Owner == u.ID {
			room.Closing = true
			if s.save() != nil {
				fail(w, 503, "storage_unavailable")
				return
			}
			if err := s.cleanup(r.Context()); err != nil {
				if _, stillPending := s.rooms[room.ID]; stillPending {
					fail(w, 503, "revocation_pending")
					return
				}
			}
		} else {
			// Persist a room close intent on ambiguity; don't re-use until all revoked.
			if err := s.backend.Authorize(r.Context(), room.NetworkID, u.NodeID, false); err != nil {
				room.Closing = true
				_ = s.save()
				fail(w, 503, "revocation_pending")
				return
			}
			delete(room.Members, u.ID)
			if s.save() != nil {
				fail(w, 503, "storage_unavailable")
				return
			}
		}
		respond(w, 204, nil)
		return
	}
	fail(w, 404, "not_found")
}
func (s *Server) create(w http.ResponseWriter, r *http.Request, u User) {
	var req struct {
		GameID string `json:"gameId"`
		Policy string `json:"policy"`
	}
	if !body(w, r, &req) {
		return
	}
	if req.GameID != "warcraft3" && req.GameID != "dst" && req.GameID != "overcooked2" {
		fail(w, 400, "unsupported_game")
		return
	}
	if req.Policy == "direct_required" {
		fail(w, 422, "strict_direct_unsupported")
		return
	}
	if req.Policy == "" {
		req.Policy = "preferred_direct"
	}
	if req.Policy != "preferred_direct" && req.Policy != "relay_allowed" {
		fail(w, 400, "invalid_policy")
		return
	}
	if s.inRoom(u.ID) {
		fail(w, 409, "already_in_room")
		return
	}
	busy := map[string]bool{}
	for _, room := range s.rooms {
		busy[room.NetworkID] = true
	}
	n := ""
	for _, candidate := range s.cfg.Networks {
		if !busy[candidate] {
			n = candidate
			break
		}
	}
	if n == "" {
		fail(w, 503, "network_pool_exhausted")
		return
	}
	// Recheck external changes before assigning an idle network.
	if s.backend.Validate(r.Context(), n, nil) != nil {
		fail(w, 503, "network_not_safe")
		return
	}
	room := &Room{ID: randomID(8), NetworkID: n, GameID: req.GameID, Policy: req.Policy, Owner: u.ID,
		ExpiresAt: s.now().Add(time.Duration(s.cfg.TTLSeconds) * time.Second), JoinKey: randomID(16), Members: map[string]Membership{}}
	s.rooms[room.ID] = room
	if !s.enroll(w, r, u, room) {
		return
	}
	respond(w, 201, room)
}
func (s *Server) enroll(w http.ResponseWriter, r *http.Request, u User, room *Room) bool {
	room.Members[u.ID] = Membership{u.ID, u.NodeID, false}
	if s.save() != nil {
		fail(w, 503, "storage_unavailable")
		return false
	}
	if s.backend.Authorize(r.Context(), room.NetworkID, u.NodeID, true) != nil {
		room.Closing = true
		_ = s.save()
		fail(w, 503, "authorization_uncertain_cleanup_pending")
		return false
	}
	room.Members[u.ID] = Membership{u.ID, u.NodeID, true}
	if s.save() != nil {
		fail(w, 503, "storage_unavailable")
		return false
	}
	return true
}
func (s *Server) join(w http.ResponseWriter, r *http.Request, u User, room *Room) {
	var req struct {
		JoinKey string `json:"joinKey"`
	}
	if !body(w, r, &req) {
		return
	}
	if subtle.ConstantTimeCompare([]byte(req.JoinKey), []byte(room.JoinKey)) != 1 {
		fail(w, 403, "invalid_invitation")
		return
	}
	if m, ok := room.Members[u.ID]; ok && m.Authorized {
		respond(w, 200, public(room))
		return
	}
	if s.inRoom(u.ID) {
		fail(w, 409, "already_in_room")
		return
	}
	if len(room.Members) >= 8 {
		fail(w, 409, "room_full")
		return
	}
	if !s.enroll(w, r, u, room) {
		return
	}
	respond(w, 200, public(room))
}
