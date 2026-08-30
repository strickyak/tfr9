package main

import (
	"flag"
	"fmt"

	"github.com/strickyak/tfr9/v2/listings"
    . "github.com/strickyak/gomar/gu"
)

func main() {
	flag.Parse()

	for _, a := range flag.Args() {
		m := listings.LoadFile(a)
		keys := Sorted(Keys(m.Src))
		for _, k := range keys {
            v := m.Src[k]
			fmt.Printf("%s : %04x : %s\n", m.Filename, k, v)
		}
	}
}
