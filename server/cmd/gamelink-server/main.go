package main

import (
	"context"
	"encoding/json"
	"flag"
	"gamelink/server/internal/api"
	"gamelink/server/internal/controller"
	"log"
	"net"
	"net/http"
	"os"
	"os/signal"
	"path/filepath"
	"runtime"
	"strings"
	"syscall"
	"time"
)

func main() {
	config := flag.String("config", "deploy/server.json", "configuration path")
	mock := flag.Bool("mock", false, "explicit offline mock; loopback only, never authorizes real nodes")
	flag.Parse()
	if runtime.GOOS == "windows" && !*mock {
		log.Fatal("Windows supports local --mock testing only; deploy the real controller service on Linux")
	}
	b, err := os.ReadFile(*config)
	if err != nil {
		log.Fatal("cannot read config")
	}
	var cfg api.Config
	dec := json.NewDecoder(strings.NewReader(string(b)))
	dec.DisallowUnknownFields()
	if err = dec.Decode(&cfg); err != nil {
		log.Fatal("invalid config")
	}
	if err = os.MkdirAll(filepath.Dir(cfg.StateFile), 0700); err != nil {
		log.Fatal("state directory unavailable")
	}
	stateLock, err := lockState(cfg.StateFile)
	if err != nil {
		log.Fatal("state already in use or lock unavailable")
	}
	defer stateLock.Close()
	var backend controller.Backend
	if *mock {
		host, _, e := net.SplitHostPort(cfg.Listen)
		ip := net.ParseIP(host)
		if e != nil || ip == nil || !ip.IsLoopback() {
			log.Fatal("mock must bind numeric loopback")
		}
		backend = controller.Mock{}
		log.Print("MOCK mode: no real ZeroTier authorization or P2P")
	} else {
		// Production HTTP is behind a local TLS reverse proxy.
		host, _, e := net.SplitHostPort(cfg.Listen)
		ip := net.ParseIP(host)
		if e != nil || ip == nil || !ip.IsLoopback() {
			log.Fatal("bind loopback; use TLS reverse proxy")
		}
		token, e := os.ReadFile(cfg.ControllerTokenFile)
		if e != nil {
			log.Fatal("cannot read controller token")
		}
		backend, err = controller.New(cfg.ControllerURL, strings.TrimSpace(string(token)))
		if err != nil {
			log.Fatal("invalid controller endpoint")
		}
	}
	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()
	app, err := api.New(ctx, cfg, backend, *mock)
	if err != nil {
		log.Printf("startup refused: %v", err)
		os.Exit(1)
	}
	srv := &http.Server{Addr: cfg.Listen, Handler: app, ReadHeaderTimeout: 3 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 30 * time.Second, IdleTimeout: 30 * time.Second, MaxHeaderBytes: 8192}
	go func() {
		ticker := time.NewTicker(5 * time.Second)
		defer ticker.Stop()
		for {
			select {
			case <-ctx.Done():
				return
			case <-ticker.C:
				cleanupCtx, cancel := context.WithTimeout(ctx, 25*time.Second)
				err := app.Sweep(cleanupCtx)
				cancel()
				if err != nil {
					log.Print("cleanup incomplete; networks quarantined")
				}
			}
		}
	}()
	go func() {
		<-ctx.Done()
		shutdownCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		_ = srv.Shutdown(shutdownCtx)
	}()
	log.Printf("GameLink API listening=%s", cfg.Listen)
	if err = srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
		log.Fatal("HTTP listener failed")
	}
}
