package controller

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"regexp"
	"strings"
	"time"
)

var networkID = regexp.MustCompile(`^[0-9a-f]{16}$`)
var nodeID = regexp.MustCompile(`^[0-9a-f]{10}$`)

type Member struct {
	Authorized    bool     `json:"authorized"`
	IPAssignments []string `json:"ipAssignments"`
}
type Backend interface {
	Validate(context.Context, string, map[string]bool) error
	Authorize(context.Context, string, string, bool) error
	Member(context.Context, string, string) (Member, error)
}
type HTTP struct {
	base   string
	token  string
	client *http.Client
}

func New(base, token string) (*HTTP, error) {
	u, err := url.Parse(base)
	if err != nil || u.Scheme != "http" || u.User != nil || u.RawQuery != "" || u.Fragment != "" || (u.Path != "" && u.Path != "/") {
		return nil, errors.New("controller must be plain HTTP on numeric loopback")
	}
	ip := net.ParseIP(u.Hostname())
	if ip == nil || !ip.IsLoopback() || token == "" {
		return nil, errors.New("controller must be loopback with token")
	}
	return &HTTP{base: strings.TrimRight(base, "/"), token: token, client: &http.Client{Timeout: 3 * time.Second,
		Transport:     &http.Transport{Proxy: nil, DialContext: (&net.Dialer{Timeout: 2 * time.Second}).DialContext},
		CheckRedirect: func(*http.Request, []*http.Request) error { return errors.New("redirect refused") }}}, nil
}
func (c *HTTP) request(ctx context.Context, method, path string, body any, out any) error {
	var reader io.Reader
	if body != nil {
		b, err := json.Marshal(body)
		if err != nil {
			return err
		}
		reader = bytes.NewReader(b)
	}
	req, err := http.NewRequestWithContext(ctx, method, c.base+path, reader)
	if err != nil {
		return err
	}
	req.Header.Set("X-ZT1-Auth", c.token)
	req.Header.Set("Content-Type", "application/json")
	resp, err := c.client.Do(req)
	if err != nil {
		return errors.New("controller unavailable")
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return fmt.Errorf("controller HTTP %d", resp.StatusCode)
	}
	if out != nil {
		b, err := io.ReadAll(io.LimitReader(resp.Body, (1<<20)+1))
		if err != nil || len(b) > 1<<20 {
			return errors.New("controller response too large/unreadable")
		}
		return json.Unmarshal(b, out)
	}
	return nil
}
func path(n, node string) (string, error) {
	if !networkID.MatchString(n) || !nodeID.MatchString(node) {
		return "", errors.New("invalid controller identifier")
	}
	return "/controller/network/" + n + "/member/" + node, nil
}
func (c *HTTP) Authorize(ctx context.Context, n, node string, on bool) error {
	p, err := path(n, node)
	if err != nil {
		return err
	}
	var result Member
	if err = c.request(ctx, "POST", p, map[string]bool{"authorized": on}, &result); err != nil {
		return err
	}
	if result.Authorized != on {
		return errors.New("authorization not confirmed")
	}
	return nil
}
func (c *HTTP) Member(ctx context.Context, n, node string) (Member, error) {
	p, err := path(n, node)
	if err != nil {
		return Member{}, err
	}
	var m Member
	err = c.request(ctx, "GET", p, nil, &m)
	return m, err
}
func (c *HTTP) Validate(ctx context.Context, n string, known map[string]bool) error {
	if !networkID.MatchString(n) {
		return errors.New("invalid network id")
	}
	var cfg struct {
		Private bool `json:"private"`
		Routes  []struct {
			Target string  `json:"target"`
			Via    *string `json:"via"`
		} `json:"routes"`
	}
	if err := c.request(ctx, "GET", "/controller/network/"+n, nil, &cfg); err != nil {
		return err
	}
	if !cfg.Private {
		return errors.New("network must be private")
	}
	for _, r := range cfg.Routes {
		_, subnet, err := net.ParseCIDR(r.Target)
		if err != nil {
			return errors.New("unknown route schema")
		}
		ones, _ := subnet.Mask.Size()
		if ones == 0 || (r.Via != nil && *r.Via != "") {
			return errors.New("default and gateway routes refused")
		}
	}
	var members map[string]json.RawMessage
	if err := c.request(ctx, "GET", "/controller/network/"+n+"/member", nil, &members); err != nil {
		return err
	}
	for id := range members {
		m, err := c.Member(ctx, n, id)
		if err != nil {
			return err
		}
		if m.Authorized && !known[id] {
			return errors.New("network has unmanaged authorized member")
		}
	}
	return nil
}

// Mock is used only when explicitly started in --mock mode. It makes no network calls.
type Mock struct{}

func (Mock) Validate(context.Context, string, map[string]bool) error { return nil }
func (Mock) Authorize(context.Context, string, string, bool) error   { return nil }
func (Mock) Member(context.Context, string, string) (Member, error) {
	return Member{Authorized: true}, nil
}
