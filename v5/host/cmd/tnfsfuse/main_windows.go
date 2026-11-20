//go:build windows

package main

import "log"

func main() {
	log.Fatal("tnfsfuse is only supported on Unix systems with FUSE")
}
