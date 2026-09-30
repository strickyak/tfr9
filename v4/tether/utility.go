package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"runtime/debug"
	"strings"
	"sync"
	"sync/atomic"
)

var Format = fmt.Sprintf

var Logf = log.Printf

var (
	panicCount        int32
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
	count := atomic.AddInt32(&panicCount, 1)
	RestoreSttyState()

	if count > 10 {
		fmt.Fprintf(os.Stderr, "TETHER_PANIC_LIMIT_EXCEEDED\n")
		os.Exit(2)
	}

	msg := fmt.Sprintf("PANIC: "+format, args...)
	panic(msg)
}

type Ordered interface {
	~byte | ~int | ~uint | ~int64 | ~uint64 | ~rune | ~string
}

func AssertEQ[T Ordered](a, b T) {
	if a != b {
		log.Printf("AssertEQ fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		Panicf("...AssertEQ fails: %v vs %v", a, b)
	}
}

func AssertLT[T Ordered](a, b T) {
	if a >= b {
		log.Printf("AssertLT fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		Panicf("...AssertLT fails: %v vs %v", a, b)
	}
}

func AssertGE[T Ordered](a, b T) {
	if a < b {
		log.Printf("AssertGE fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		Panicf("...AssertGE fails: %v vs %v", a, b)
	}
}

func AssertGT[T Ordered](a, b T) {
	if a <= b {
		log.Printf("AssertGT fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		Panicf("...AssertGT fails: %v vs %v", a, b)
	}
}

func Cond[T any](pred bool, x T, y T) T {
	if pred {
		return x
	}
	return y
}
