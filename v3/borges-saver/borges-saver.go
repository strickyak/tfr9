package main

import (
	"bufio"
	// "crypto/md5"
	"flag"
	"fmt"
	// "io"
	"log"
	"os"
	"path/filepath"
    "reflect"
	"regexp"
	"strings"
)

/*  Model Lines:
|0000 87CD012A000DC185 (/home/strick/6809):00042         Begin    mod   eom,name,tylg,atrv,start,size
|     D7002B0000
|                      (/home/strick/6809):00043
|                      (/home/strick/6809):00044                  org   0
|     0000             (/home/strick/6809):00045         size     equ   .          REL doesn't require any memory
|                      (/home/strick/6809):00046
|000D 5245CC           (/home/strick/6809):00047         name     fcs   /REL/
|0010 05               (/home/strick/6809):00048                  fcb   edition
*/

var startLine *regexp.Regexp
var continueLine *regexp.Regexp

func init() {
	// ..... "0000 87CD012A000DC185 (/home/strick/6809):00042         Begin    mod   eom,name,tylg,atrv,start,size"
	s := "(HHHH) (HHhhhhhhhhhhhhhh) {.................}:DDDDD         "
	s = strings.ReplaceAll(s, "H", "[0-9a-f]")  // Must be hex digits.
	s = strings.ReplaceAll(s, "h", "[0-9a-f ]") // Maybe 2 hex, maybe 2 spaces.
	// s = strings.ReplaceAll(s, "h?", "([0-9A-H][0-9A-H]|  )")  // Maybe 2 hex, maybe 2 spaces.
	s = strings.ReplaceAll(s, "{", "[(]")   // open paren
	s = strings.ReplaceAll(s, "}", "[)]")   // close paren
	s = strings.ReplaceAll(s, "D", "[0-9]") // decimal digit
	s += "([A-Za-z0-9$@_.]*) +"             // may be a label, must be spaces before opcode
	s += "([A-Za-z0-9]+)"                   // must be an opcode.
	startLine = regexp.MustCompile("^" + s)
	c := "     ([0-9a-f]{2,16})$"
	continueLine = regexp.MustCompile("^" + c)
}

func LoadFile(filename string) []byte {
	var err error
	fd, err := os.Open(filename)
	if err != nil {
		log.Panicf("Cannot open listing %q: %v", filename, err)
	}
	defer fd.Close()
	// log.Printf("loading %q", filename)

	r := bufio.NewScanner(fd)
	inModule := false

	var addr uint
	var module []byte
	for r.Scan() {
		text := strings.ToLower(r.Text())
		sl := startLine.FindStringSubmatch(text)
		cl := continueLine.FindStringSubmatch(text)
		if sl != nil {
			// log.Printf("0(%q,%q,%q,%q) :: %q\n", sl[1], sl[2], sl[3], sl[4], text)

			if sl[4] == "mod" {
				inModule = true
			}

			if inModule {
				n, err := fmt.Sscanf(sl[1], "%x", &addr)
				if err != nil {
					panic(err)
				}
				if n != 1 {
					panic(sl[1])
				}
				var tmp []byte
				n, err = fmt.Sscanf(sl[2], "%x", &tmp)
				if err != nil {
					panic(err)
				}
				if n != 1 {
					panic(sl[2])
				}
				if len(module) != int(addr) {
					log.Printf("BAD filename %q len %d=0x%x addr %d=0x%x sl=%q,%q,%q,%q", filename, len(module), len(module), addr, addr, sl[1], sl[2], sl[3], sl[4])
					return nil
				}
				module = append(module, tmp...)
			}

			if sl[4] == "emod" {
				return module
			}

		} else if cl != nil {
			if inModule {
				// log.Printf("1(%q) :: %q\n", cl[1], text)
				var tmp []byte
				n, err := fmt.Sscanf(cl[1], "%x", &tmp)
				if err != nil {
					panic(err)
				}
				if n != 1 {
					panic(cl[1])
				}
				module = append(module, tmp...)
			}
		}
	}
	if inModule {
		log.Printf("BAD: no emod: %q", filename)
	}
	return nil
}

func ModuleName(module []byte) string {
	if len(module) < 10 {
		log.Panicf("too short: %02x", module)
	}
	if module[0] != 0x87 || module[1] != 0xCD {
		log.Panicf("bad magic")
	}
	size := int(module[2])*256 + int(module[3])
	if len(module) != size {
		log.Panicf("bad size %d len %d", size, len(module))
	}
	nameptr := int(module[4])*256 + int(module[5])
	var sb strings.Builder
	for {
		c := module[nameptr]
		sb.WriteByte(c & 0x7F)
		if c&0x80 != 0 {
			break
		}
		nameptr++
	}
	name := sb.String()

	got := (uint32(module[size-3]) << 16) + (uint32(module[size-2]) << 8) + uint32(module[size-1])
	// log.Printf("GOT %x", got)

	calculated := CRC(module) ^ 0xFFFFFF
	// log.Printf("CRC %x", calculated)
	if got != calculated {
		log.Panicf("bad crc: got %x calculated %x", got, calculated)
	}

	return fmt.Sprintf("%s.%04x%06x", name, size, got)
}

func CRC(a []byte) uint32 {
	var crc uint32 = 0xFFFFFF
	for k := 0; k < len(a)-3; k++ {
		crc ^= uint32(a[k]) << 16
		for i := 0; i < 8; i++ {
			crc <<= 1
			if (crc & 0x1000000) != 0 {
				crc ^= 0x800063
			}
		}
	}
	return crc & 0xffffff
}

func SaveListingCopy(readpath, outdir, id string) {
	c1, err := os.ReadFile(readpath)
	if err != nil {
		log.Panicf("Cannot read file: %q: %v", readpath, err)
	}

	writepath := filepath.Join(outdir, strings.ToLower(id))
	c2, _ := os.ReadFile(writepath)

	z := reflect.DeepEqual(c1 , c2 )
    // log.Printf("%v, %2x, %2x, %q -> %q", z, md5.Sum(c1), md5.Sum(c2), readpath, writepath)
    if !z {
        // log.Printf("% 3x", c1);
        // log.Printf("% 3x", c2);
        log.Printf("Saving %q -> %q", readpath, writepath)
        os.WriteFile(writepath, c1, 0777)
    }
}

/*
func SaveListingCopy(readpath, outdir, id string) {
	if ListingCopyAlreadySaved(readpath, outdir, id) {
		return
	}

	r, err := os.Open(readpath)
	if err != nil {
		log.Panicf("Cannot read file: %q: %v", readpath, err)
	}
	defer r.Close()

	writepath := filepath.Join(outdir, strings.ToLower(id))
	w, err := os.Create(writepath)
	if err != nil {
		log.Panicf("Cannot create file: %q: %v", writepath, err)
	}
	defer w.Close()

    log.Printf("Saving %q -> %q", readpath, writepath)
	_, err = io.Copy(w, r)
	if err != nil {
		log.Panicf("Cannot copy file: %q to %q: %v", readpath, writepath, err)
	}
}
*/

func HasListSuffix(path string) bool {
	if strings.HasSuffix(path, ".lst") {
		return true
	}
	if strings.HasSuffix(path, ".list") {
		return true
	}
	if strings.HasSuffix(path, ".list+") {
		return true
	}
	if strings.HasSuffix(path, ".listing") {
		return true
	}
	return false
}

func Walker(path string, info os.FileInfo, err error) error {
	if HasListSuffix(path) {
		if info.Mode().IsRegular() {
			module := LoadFile(path)
			if module == nil {
				if *Verbose {
					log.Printf("==== no module for %q", path)
				}
				return nil
			}
			id := ModuleName(module)
			if *Verbose {
				log.Printf("OKAY ==== %q ==> %q\n", path, id)
			}
			if *OutDir != "" {
				SaveListingCopy(path, *OutDir, id)
			}
		}
	}
	return nil
}

var OutDir = flag.String("outdir", "", "directory to write listings to")
var Verbose = flag.Bool("v", false, "extra verbose debugging output")

func main() {
	flag.Parse()

	if *OutDir == "" || len(flag.Args()) == 0 {
		log.Panicf("USAGE:  borges-saver -outdir WhereToSaveListings/  WhereToSearchForListings/ ...")
	}

	for i, dirname := range flag.Args() {
		log.Printf("[%d] Walking %q", i+1, dirname)
		err := filepath.Walk(dirname, Walker)
		if err != nil {
			log.Panicf("cannot walk: %q: %v", dirname, err)
		}
	}
}
