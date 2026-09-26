package main

import (
	"fmt"
	"log"
	"os"
	"os/exec"
	"runtime/debug"
	"strings"
	"sync/atomic"
)

var Format = fmt.Sprintf

var Logf = log.Printf

var panicCount int32
var originalSttyState string
var sttyPath string

func SaveSttyState() {
	sttyPath, _ = exec.LookPath("stty")
	if sttyPath != "" {
		cmd := exec.Command(sttyPath, "-g")
		cmd.Stdin = os.Stdin
		out, err := cmd.Output()
		if err == nil {
			originalSttyState = strings.TrimSpace(string(out))
		}
	}
}

func SetSttyCbreak() {
	if sttyPath != "" {
		cmd := exec.Command(sttyPath, "cbreak", "-echo", "-ixon")
		cmd.Stdin = os.Stdin
		cmd.Run()
	}
}

func RestoreSttyState() {
	if sttyPath != "" && originalSttyState != "" {
		cmd := exec.Command(sttyPath, originalSttyState)
		cmd.Stdin = os.Stdin
		cmd.Run()
	}
}

func Panicf(format string, args ...any) {
	count := atomic.AddInt32(&panicCount, 1)
	fmt.Fprintf(os.Stderr, "TETHER_PANIC_TRIGGERED\n")
	// Lifetime TODO: log instead of stderr

	// Ensure the terminal is restored to a sane state so it isn't broken for the user
	RestoreSttyState()

	if count > 100 {
		fmt.Fprintf(os.Stderr, "TETHER_PANIC_LIMIT_EXCEEDED\n")
		os.Exit(2)
	}

	msg := fmt.Sprintf("PANIC: "+format, args...)
	// Lifetime TODO: log msg
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
