package main

import (
	"bufio"
	"flag"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"strconv"
	"strings"

    "github.com/strickyak/tfr9/v2/listings"

	. "github.com/strickyak/gomar/gu"
)

// var BORGES = flag.String("borges", "/home/strick/borges/", "where to find listing files")
// var ADDR = flag.Uint("addr", 0xC000, "rom address")
var RUNLOG = flag.String("runlog", "/dev/null", "output of grok")

var ALIST = flag.String("alist", "", "absolute listing")

func main() {
	flag.Parse()

    var big *listings.ModSrc

    if *ALIST != "" {
        big = listings.LoadFile(*ALIST)
        // log.Printf("big: %#v", big)
    } else {

        a := Value(ioutil.ReadAll(os.Stdin))
        romStart := uint(0xFF00 - len(a))
        modnames := listings.LsMod(a)

        big = &listings.ModSrc{
            Src: make(map[uint]string),
        }
        for _, it := range modnames {
            log.Printf("MODULE: offset %x addr %x module %q", it.Offset, it.Offset + romStart, it.Module)

            m := listings.LoadFile(*listings.Borges + it.Module)
            keys := Sorted(Keys(m.Src))
            for _, k := range keys {
                v := m.Src[k]
                log.Printf("%s : %04x : %s", m.Filename, k, v)
                big.Src[romStart+it.Offset+k] = fmt.Sprintf("\"%s\"+%04x %s", it.Module, k, v)
            }
        }
        bigKeys := Sorted(Keys(big.Src))
        for _, k := range bigKeys {
            v := big.Src[k]
            log.Printf("big: %04x : %s", k, v)
        }
    }

	r := Value(os.Open(*RUNLOG))
	scanner := bufio.NewScanner(r)
	for scanner.Scan() {
		s := scanner.Text()
		if strings.HasPrefix(s, "!# ") {  // Trim off logging prefix.
            s = s[3:]
        }
		if strings.HasPrefix(s, "@") {
			w := strings.Split(s, " ")
			AssertLE(3, len(w))
			addy := uint(Value(strconv.ParseUint(w[1], 16, 16)))
			if source, ok := big.Src[addy]; ok {
				s = s + " :: " + source
			}
		}
		fmt.Printf("%s\n", s)
	}

	if err := scanner.Err(); err != nil {
		log.Fatal(err)
	}
}
