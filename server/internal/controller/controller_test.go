package controller

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestAdapterContract(t *testing.T) {
	authorized := false
	route := `{"target":"10.77.0.0/24","via":null}`
	private := true
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("X-ZT1-Auth") != "secret" {
			t.Error("missing token")
			w.WriteHeader(401)
			return
		}
		switch r.URL.Path {
		case "/controller/network/abcdef1234000001":
			if !private {
				w.Write([]byte(`{"private":false}`))
				return
			}
			w.Write([]byte(`{"private":true,"routes":[` + route + `]}`))
		case "/controller/network/abcdef1234000001/member":
			w.Write([]byte(`{"aaaaaaaaaa":1}`))
		case "/controller/network/abcdef1234000001/member/aaaaaaaaaa":
			if r.Method == "POST" {
				var m Member
				json.NewDecoder(r.Body).Decode(&m)
				authorized = m.Authorized
			}
			json.NewEncoder(w).Encode(Member{Authorized: authorized, IPAssignments: []string{"10.77.0.2"}})
		default:
			w.WriteHeader(404)
		}
	}))
	defer srv.Close()
	c, err := New(srv.URL, "secret")
	if err != nil {
		t.Fatal(err)
	}
	ctx := context.Background()
	if err = c.Validate(ctx, "abcdef1234000001", nil); err != nil {
		t.Fatal(err)
	}
	if err = c.Authorize(ctx, "abcdef1234000001", "aaaaaaaaaa", true); err != nil {
		t.Fatal(err)
	}
	if err = c.Validate(ctx, "abcdef1234000001", nil); err == nil {
		t.Fatal("unmanaged member accepted")
	}
	if err = c.Validate(ctx, "abcdef1234000001", map[string]bool{"aaaaaaaaaa": true}); err != nil {
		t.Fatal(err)
	}
	if err = c.Authorize(ctx, "abcdef1234000001", "aaaaaaaaaa", false); err != nil {
		t.Fatal(err)
	}
	route = `{"target":"0.0.0.0/0","via":null}`
	if c.Validate(ctx, "abcdef1234000001", nil) == nil {
		t.Fatal("default route accepted")
	}
	route = `{"target":"10.77.0.0/24","via":"10.77.0.1"}`
	if c.Validate(ctx, "abcdef1234000001", nil) == nil {
		t.Fatal("gateway route accepted")
	}
	private = false
	if c.Validate(ctx, "abcdef1234000001", nil) == nil {
		t.Fatal("public network accepted")
	}
}
func TestEndpointSafety(t *testing.T) {
	for _, url := range []string{"http://example.com:9993", "https://127.0.0.1:9993", "http://localhost:9993", "http://127.0.0.1:9993/path", "http://a:b@127.0.0.1:9993"} {
		if _, err := New(url, "secret"); err == nil {
			t.Fatal("unsafe endpoint", url)
		}
	}
}
func TestNoTokenRedirect(t *testing.T) {
	received := false
	sink := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { received = true }))
	defer sink.Close()
	redirect := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { http.Redirect(w, r, sink.URL, 302) }))
	defer redirect.Close()
	c, _ := New(redirect.URL, "secret")
	if c.Validate(context.Background(), "abcdef1234000001", nil) == nil || received {
		t.Fatal("redirect followed")
	}
}

func TestMalformedAndOversizedResponses(t *testing.T) {
	payload := `{"private":true} {}`
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.Write([]byte(payload)) }))
	defer srv.Close()
	c, _ := New(srv.URL, "secret")
	if c.Validate(context.Background(), "abcdef1234000001", nil) == nil {
		t.Fatal("trailing response accepted")
	}
	payload = `{"private":true,"padding":"` + strings.Repeat("x", 1<<20) + `"}`
	if c.Validate(context.Background(), "abcdef1234000001", nil) == nil {
		t.Fatal("oversized response accepted")
	}
}
