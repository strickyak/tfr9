package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"strings"
	"sync"
)

var (
	originalSttyState string
	sttyPath          string
	sttyMu            sync.Mutex
	sttyIsCbreak      bool
)

func SaveSttyState() {
	sttyMu.Lock()
	defer sttyMu.Unlock()

	var err error
	sttyPath, err = exec.LookPath("stty")
	if err != nil || sttyPath == "" {
		return
	}
	cmd := exec.Command(sttyPath, "-g")
	cmd.Stdin = os.Stdin
	out, err := cmd.Output()
	if err == nil {
		originalSttyState = strings.TrimSpace(string(out))
	}
}

func SetSttyCbreak() {
	sttyMu.Lock()
	defer sttyMu.Unlock()

	if sttyPath != "" && originalSttyState != "" {
		cmd := exec.Command(sttyPath, "cbreak", "-echo", "-ixon")
		cmd.Stdin = os.Stdin
		_ = cmd.Run()
		sttyIsCbreak = true
	}
}

func RestoreSttyState() {
	sttyMu.Lock()
	defer sttyMu.Unlock()

	if sttyPath != "" && originalSttyState != "" && sttyIsCbreak {
		cmd := exec.Command(sttyPath, originalSttyState)
		cmd.Stdin = os.Stdin
		_ = cmd.Run()
		sttyIsCbreak = false
	}
}

func Fatalf(format string, args ...any) {
	RestoreSttyState()
	log.Fatalf(format, args...)
}

func Panicf(format string, args ...any) {
	RestoreSttyState()
	panic(fmt.Sprintf(format, args...))
}
