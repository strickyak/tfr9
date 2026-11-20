package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"strings"
)

const defaultTNFSPort = "16384"

var (
	fsRoot     = flag.String("fs", "", "Root directory of the exported read-only filesystem (required)")
	listenAddr = flag.String("listen", "localhost:"+defaultTNFSPort, "TCP host:port (or host) to listen on")
	verbose    = flag.Bool("verbose", false, "Log TNFS connections and errors")
)

func normalizeListenAddr(addr string) string {
	if !strings.Contains(addr, ":") {
		return addr + ":" + defaultTNFSPort
	}
	return addr
}

func main() {
	log.SetFlags(0)
	flag.Parse()

	if *fsRoot == "" {
		fmt.Fprintln(os.Stderr, "tnfsd: --fs=/some/dir is required")
		flag.Usage()
		os.Exit(2)
	}

	fsys, err := newReadOnlyFS(*fsRoot)
	if err != nil {
		log.Fatalf("tnfsd: %v", err)
	}

	addr := normalizeListenAddr(*listenAddr)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("tnfsd: listen %s: %v", addr, err)
	}
	defer ln.Close()

	log.Printf("tnfsd: serving %s (read-only TCP) on %s", fsys.root, ln.Addr())

	srv := newTNFSServer(fsys, *verbose)
	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("tnfsd: accept error: %v", err)
			continue
		}
		go srv.serveConn(conn)
	}
}
