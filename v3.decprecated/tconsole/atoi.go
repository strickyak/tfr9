package main

import (
	"log"
	"strconv"
	"strings"
)

func SmartAtoi(s string, bits int) uint {
	var num uint64
	var err error

	switch {
	case strings.HasPrefix(s, "$"):
		num, err = strconv.ParseUint(s[1:], 16, bits)
	case strings.HasPrefix(s, "0x"):
		num, err = strconv.ParseUint(s[2:], 16, bits)
	case strings.HasPrefix(s, "0X"):
		num, err = strconv.ParseUint(s[2:], 16, bits)
	case strings.HasPrefix(s, "%"):

		num, err = strconv.ParseUint(s[1:], 2, bits)
	case strings.HasPrefix(s, "0b"):
		num, err = strconv.ParseUint(s[2:], 2, bits)
	case strings.HasPrefix(s, "0B"):
		num, err = strconv.ParseUint(s[2:], 2, bits)

	case strings.HasPrefix(s, "0"):
		num, err = strconv.ParseUint(s, 8, bits)

	default:
		num, err = strconv.ParseUint(s, 10, bits)
	}

	if err != nil {
		log.Panicf("Bad numeric parse: %q", s)
	}
	return uint(num)
}
