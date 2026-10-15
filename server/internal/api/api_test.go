package api

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"gamelink/server/internal/controller"
	"net/http"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

const tokenA = "alice-test-token-0123456789-abcdef"
const tokenB = "bob-test-token-0123456789-abcdefgh"
const tokenC = "eve-test-token-0123456789-abcdefgh"

type fake struct {
	on                   map[string]bool
	failAuth, failRevoke bool
	revocations          int
}

func (f *fake) Validate(context.Context, string, map[string]bool) error { return nil }
func (f *fake) Authorize(_ context.Context, n, node string, on bool) error {
	if (on && f.failAuth) || (!on && f.failRevoke) {
		return errors.New("offline")
	}
	f.on[n+node] = on
	if !on {
		f.revocations++
	}
	return nil
}
func (f *fake) Member(_ context.Context, n, node string) (controller.Member, error) {
	return controller.Member{Authorized: f.on[n+node], IPAssignments: []string{"10.77.0.2"}}, nil
}
func hash(s string) string { h := sha256.Sum256([]byte(s)); return hex.EncodeToString(h[:]) }
func fixture(t *testing.T) (*Server, *fake, Config) {
	t.Helper()
	cfg := Config{StateFile: filepath.Join(t.TempDir(), "state.json"), TTLSeconds: 60,
		Networks: []string{"abcdef1234000001", "abcdef1234000002"}, Users: []User{{"alice", "aaaaaaaaaa", hash(tokenA)}, {"bob", "bbbbbbbbbb", hash(tokenB)}, {"eve", "cccccccccc", hash(tokenC)}}}
	f := &fake{on: map[string]bool{}}
	s, err := New(context.Background(), cfg, f, false)
	if err != nil {
		t.Fatal(err)
	}
	return s, f, cfg
}
func request(s *Server, method, path, token, body string) *httptest.ResponseRecorder {
	r := httptest.NewRequest(method, path, strings.NewReader(body))
	r.Header.Set("Authorization", "Bearer "+token)
	w := httptest.NewRecorder()
	s.ServeHTTP(w, r)
	return w
}
func create(t *testing.T, s *Server) Room {
	t.Helper()
	w := request(s, "POST", "/api/v1/rooms", tokenA, `{"gameId":"warcraft3"}`)
	if w.Code != 201 {
		t.Fatalf("create %d %s", w.Code, w.Body)
	}
	var room Room
	if err := json.Unmarshal(w.Body.Bytes(), &room); err != nil {
		t.Fatal(err)
	}
	return room
}
func TestRoomLifecycleACL(t *testing.T) {
	s, f, _ := fixture(t)
	r := create(t, s)
	base := "/api/v1/rooms/" + r.ID
	if r.Policy != "preferred_direct" || len(r.JoinKey) != 32 || !f.on[r.NetworkID+"aaaaaaaaaa"] {
		t.Fatal("create binding/policy")
	}
	checks := []struct {
		method, path, token, body string
		code                      int
	}{
		{"GET", base, tokenC, "", 403}, {"GET", base, "wrong", "", 401},
		{"POST", base + "/join", tokenB, `{"joinKey":"wrong"}`, 403},
		{"POST", base + "/join", tokenB, `{"joinKey":"` + r.JoinKey + `","nodeId":"aaaaaaaaaa"}`, 400},
		{"POST", base + "/join", tokenB, `{"joinKey":"` + r.JoinKey + `"}`, 200},
		{"POST", base + "/join", tokenB, `{"joinKey":"` + r.JoinKey + `"}`, 200},
		{"GET", base + "/peers", tokenB, "", 200}, {"GET", base, tokenB, "", 200},
	}
	for _, c := range checks {
		w := request(s, c.method, c.path, c.token, c.body)
		if w.Code != c.code {
			t.Fatalf("%s %s got %d want %d %s", c.method, c.path, w.Code, c.code, w.Body)
		}
		if c.method == "GET" && strings.Contains(w.Body.String(), r.JoinKey) {
			t.Fatal("invitation leaked")
		}
	}
	w := request(s, "POST", base+"/leave", tokenA, `{}`)
	if w.Code != 204 {
		t.Fatal(w.Code, w.Body)
	}
	if len(s.rooms) != 0 || f.on[r.NetworkID+"aaaaaaaaaa"] || f.on[r.NetworkID+"bbbbbbbbbb"] {
		t.Fatal("room not revoked")
	}
}
func TestStrictAndInputRejected(t *testing.T) {
	s, _, _ := fixture(t)
	for _, c := range []struct {
		body string
		code int
	}{
		{`{"gameId":"dst","policy":"direct_required"}`, 422},
		{`{"gameId":"dst","policy":"wat"}`, 400}, {`{"gameId":"unknown"}`, 400},
		{`{"gameId":"dst"} {}`, 400}, {`{"gameId":"dst","nodeId":"aaaaaaaaaa"}`, 400},
		{`{"gameId":"` + strings.Repeat("x", 9000) + `"}`, 400},
	} {
		if w := request(s, "POST", "/api/v1/rooms", tokenA, c.body); w.Code != c.code {
			t.Fatal(w.Code, w.Body)
		}
	}
	if len(s.rooms) != 0 {
		t.Fatal("rejected requests authorized anything")
	}
}
func TestExpiryQuarantinesAndRetries(t *testing.T) {
	s, f, _ := fixture(t)
	r := create(t, s)
	s.now = func() time.Time { return r.ExpiresAt.Add(time.Second) }
	f.failRevoke = true
	if s.Sweep(context.Background()) == nil {
		t.Fatal("expected cleanup error")
	}
	if s.rooms[r.ID] == nil || !s.rooms[r.ID].Closing {
		t.Fatal("failed revoke must quarantine")
	}
	f.failRevoke = false
	if err := s.Sweep(context.Background()); err != nil {
		t.Fatal(err)
	}
	if len(s.rooms) != 0 || f.on[r.NetworkID+"aaaaaaaaaa"] {
		t.Fatal("retry failed")
	}
}
func TestRestartRevokesJournal(t *testing.T) {
	s, f, cfg := fixture(t)
	r := create(t, s)
	restarted, err := New(context.Background(), cfg, f, false)
	if err != nil {
		t.Fatal(err)
	}
	if len(restarted.rooms) != 0 || f.on[r.NetworkID+"aaaaaaaaaa"] || f.revocations == 0 {
		t.Fatal("restart discarded journal")
	}
}
func TestUncertainAuthorizationPersistsCleanup(t *testing.T) {
	s, f, cfg := fixture(t)
	f.failAuth = true
	w := request(s, "POST", "/api/v1/rooms", tokenA, `{"gameId":"dst"}`)
	if w.Code != 503 || len(s.rooms) != 1 {
		t.Fatal(w.Code, w.Body)
	}
	f.failAuth = false
	restarted, err := New(context.Background(), cfg, f, false)
	if err != nil {
		t.Fatal(err)
	}
	if len(restarted.rooms) != 0 || f.revocations != 1 {
		t.Fatal("pending intent lost")
	}
}
func TestLeaveFailureClosesRoom(t *testing.T) {
	s, f, _ := fixture(t)
	r := create(t, s)
	base := "/api/v1/rooms/" + r.ID
	request(s, "POST", base+"/join", tokenB, `{"joinKey":"`+r.JoinKey+`"}`)
	f.failRevoke = true
	if w := request(s, "POST", base+"/leave", tokenB, `{}`); w.Code != 503 {
		t.Fatal(w.Code)
	}
	if !s.rooms[r.ID].Closing {
		t.Fatal("revocation uncertainty not quarantined")
	}
	f.failRevoke = false
	if err := s.Sweep(context.Background()); err != nil {
		t.Fatal(err)
	}
}
func TestPoolAndRateLimit(t *testing.T) {
	s, _, _ := fixture(t)
	create(t, s)
	if w := request(s, "POST", "/api/v1/rooms", tokenA, `{"gameId":"dst"}`); w.Code != 409 {
		t.Fatal(w.Code)
	}
	request(s, "POST", "/api/v1/rooms", tokenB, `{"gameId":"dst"}`)
	if w := request(s, "POST", "/api/v1/rooms", tokenC, `{"gameId":"dst"}`); w.Code != 503 {
		t.Fatal(w.Code)
	}
	limited := false
	for i := 0; i < 125; i++ {
		if request(s, "GET", "/api/v1/health", tokenA, "").Code == 429 {
			limited = true
		}
	}
	if !limited {
		t.Fatal("no rate limit")
	}
}
func TestRealHTTPBoundary(t *testing.T) {
	s, _, _ := fixture(t)
	httpServer := httptest.NewServer(s)
	defer httpServer.Close()
	req, _ := http.NewRequest("GET", httpServer.URL+"/api/v1/health", nil)
	req.Header.Set("Authorization", "Bearer "+tokenA)
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		t.Fatal(resp.StatusCode)
	}
}

func TestConcurrentIdempotentJoin(t *testing.T) {
	s, _, _ := fixture(t)
	r := create(t, s)
	result := make(chan int, 20)
	for i := 0; i < 20; i++ {
		go func() {
			result <- request(s, "POST", "/api/v1/rooms/"+r.ID+"/join", tokenB, `{"joinKey":"`+r.JoinKey+`"}`).Code
		}()
	}
	for i := 0; i < 20; i++ {
		if status := <-result; status != 200 {
			t.Fatal(status)
		}
	}
	if len(s.rooms[r.ID].Members) != 2 {
		t.Fatal("duplicate enrollment")
	}
}
func TestMockJournalCannotReachRealController(t *testing.T) {
	s, f, cfg := fixture(t)
	s.mock = true
	if err := s.save(); err != nil {
		t.Fatal(err)
	}
	if _, err := New(context.Background(), cfg, f, false); err == nil {
		t.Fatal("mock journal accepted in real mode")
	}
}
