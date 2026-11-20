//go:build !windows

package main

import (
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"
)

var (
	serverAddr = flag.String("server", "apps.irata.com", "TNFS TCP server host[:port] (default port 16384)")
	mountPoint = flag.String("mount", "/tmp/tnfs", "Local directory to mount the read-only TNFS filesystem")
	debugFlag  = flag.Bool("debug", false, "Enable FUSE debug logging")
)

func main() {
	log.SetFlags(0)
	flag.Parse()

	if err := os.MkdirAll(*mountPoint, 0755); err != nil {
		log.Fatalf("tnfsfuse: mkdir %q: %v", *mountPoint, err)
	}

	client, err := dialTNFS(*serverAddr)
	if err != nil {
		log.Fatalf("tnfsfuse: %v", err)
	}
	defer client.Close()

	srv, err := mountTNFS(*mountPoint, client, *debugFlag)
	if err != nil {
		log.Fatalf("tnfsfuse: mount %q: %v", *mountPoint, err)
	}

	sigCh := make(chan os.Signal, 2)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	go func() {
		<-sigCh
		_ = srv.Unmount()
	}()

	log.Printf("tnfsfuse: mounted tcp://%s at %s (read-only)", client.addr, *mountPoint)
	srv.Wait()
}
