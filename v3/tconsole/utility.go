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
