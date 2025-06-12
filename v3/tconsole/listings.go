package main

import (
	"bufio"
	"flag"
	"log"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

var Borges = flag.String("borges", "/home/strick/borges/", "dir with source module listings")
var FlagTraceOnModule = flag.String("trace_on_module", "", "start tracing when loading this module")

type ModSrc struct {
	Src      map[uint]string
	Filename string
	Err      error
}

var Listings = make(map[string]*ModSrc)

func Lookup(module string, offset uint, startTrace func()) string {
	if *Borges == "" {
		return ""
	}
	if module == "" || module[0] == '(' {
		return "" // Handles "open ../borges/(fe): no such file or directory"
	}

	m, ok := Listings[module]
	if !ok {
		filename := filepath.Join(*Borges, module)
		m = LoadFile(filename)
		Listings[module] = m

		words := strings.Split(module, ".")
		if words[0] == strings.ToLower(*FlagTraceOnModule) {
			startTrace()
		}
	}

	if m.Err != nil {
		return "" // Module not found.
	}
	s, _ := m.Src[offset]
	return s // Empty if offset not found.
}

var parseLabel = regexp.MustCompile(`^([[:xdigit:]]{4})  +[(].*?[)]:[0-9]{5} +([A-Za-z0-9._$@]+[:]?) *$`)
var parse = regexp.MustCompile(`^([[:xdigit:]]{4}) [[:xdigit:]]+ +[(].*?[)]:[0-9]{5}         (.*)$`)
var parseSection = regexp.MustCompile(`^ +[(].*?[)]:[[:digit:]]{5} +(?i:section) +([[:word:]]+)`)
var parseEndSection = regexp.MustCompile(`^ +[(].*?[)]:[[:digit:]]{5} +(?i:endsection)`)

func LoadFile(filename string) *ModSrc {
	d := make(map[uint]string)
	// Try overriding filename with ".mod" instead of version suffix.
	var fd *os.File
	var err error
	if len(filename) > 11 {
		fd, err = os.Open(filename[:len(filename)-11] + ".mod")
	}
	if err != nil {
		fd, err = os.Open(filename)
		if err != nil {
			log.Printf("BAD: Cannot open listing %q: %v", filename, err)

			return &ModSrc{
				Src:      nil,
				Filename: filename,
				Err:      err,
			}
		}
	}
	defer fd.Close()
	r := bufio.NewScanner(fd)
	inOtherSection := false

	var sawAddr, sawLabel string
	for r.Scan() {
		text := r.Text()
		m := parse.FindStringSubmatch(text)
		if m != nil && !inOtherSection {
			hexaddr, line := m[1], m[2]
			addr, err := strconv.ParseUint(hexaddr, 16, 16)
			if err != nil {
				log.Panicf("Should have been a hex integer: %q: %v", hexaddr, err)
			}
			if sawLabel != "" && sawAddr == hexaddr && line[0] == ' ' {
				line = sawLabel + line
			}
			d[uint(addr)] = line
			//log.Printf("FILE %s ADDR %x LINE %q", filename, addr, line)
			sawAddr = ""
			sawLabel = ""
		}
		m = parseLabel.FindStringSubmatch(text)
		if m != nil && !inOtherSection {
			sawAddr = m[1]
			sawLabel = m[2]
		}
		m = parseSection.FindStringSubmatch(text)
		if m != nil {
			section := m[1]
			inOtherSection = (section != "code")
		}
		m = parseEndSection.FindStringSubmatch(text)
		if m != nil {
			inOtherSection = false
		}
	}
	log.Printf("BORGES: Loaded Source: %q (%d)", filename, len(d))
	return &ModSrc{
		Src:      d,
		Filename: filename,
		Err:      nil,
	}
}
