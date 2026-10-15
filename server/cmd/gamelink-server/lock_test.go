//go:build linux || darwin

package main

import (
	"path/filepath"
	"testing"
)

func TestStateLock(t *testing.T) {
	path := filepath.Join(t.TempDir(), "state.json")
	first, err := lockState(path)
	if err != nil {
		t.Fatal(err)
	}
	if second, err := lockState(path); err == nil {
		second.Close()
		first.Close()
		t.Fatal("second writer accepted")
	}
	first.Close()
	third, err := lockState(path)
	if err != nil {
		t.Fatal("lock did not recover", err)
	}
	third.Close()
}
