//go:build !linux && !darwin

package main

import (
	"errors"
	"os"
)

func lockState(string) (*os.File, error) {
	return nil, errors.New("server requires Linux or macOS advisory locking")
}
