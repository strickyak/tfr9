package main

import (
	"fmt"
	"log"
)

var Format = fmt.Sprintf

var Logf = log.Printf

var Fatalf = log.Printf

func Panicf(format string, args ...any) {
	log.Panicf("PANIC: "+format, args...)
}

type Ordered interface {
	byte | int | uint | int64 | uint64 | rune | string
}

func AssertEQ[T Ordered](a, b T) {
	if a != b {
		log.Fatalf("AssertEQ fails: %v vs %v", a, b)
	}
}

func AssertLT[T Ordered](a, b T) {
	if a >= b {
		log.Fatalf("AssertLT fails: %v vs %v", a, b)
	}
}

func AssertGE[T Ordered](a, b T) {
	if a < b {
		log.Fatalf("AssertGE fails: %v vs %v", a, b)
	}
}
