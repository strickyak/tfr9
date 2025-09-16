package main

import (
	"fmt"
	"log"
	"runtime/debug"
)

var Format = fmt.Sprintf

var Logf = log.Printf

func Panicf(format string, args ...any) {
	log.Panicf("PANIC: "+format, args...)
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
		log.Panicf("...AssertEQ fails: %v vs %v", a, b)
	}
}

func AssertLT[T Ordered](a, b T) {
	if a >= b {
		log.Printf("AssertLT fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		log.Panicf("...AssertLT fails: %v vs %v", a, b)
	}
}

func AssertGE[T Ordered](a, b T) {
	if a < b {
		log.Printf("AssertGE fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		log.Panicf("...AssertGE fails: %v vs %v", a, b)
	}
}

func AssertGT[T Ordered](a, b T) {
	if a <= b {
		log.Printf("AssertGT fails: %v vs %v", a, b)
		log.Printf("vvvvvvvvvvvvvvvvvvvvvvv")
		debug.PrintStack()
		log.Printf("^^^^^^^^^^^^^^^^^^^^^^^")
		log.Panicf("...AssertGT fails: %v vs %v", a, b)
	}
}
