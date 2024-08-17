package main

import (
    "github.com/strickyak/tfr9/listings"
    "os"
    "io/ioutil"
    "fmt"

	. "github.com/strickyak/gomar/gu"
)
func main() {
    a := Value(ioutil.ReadAll(os.Stdin))
    modnames := listings.LsMod(a)

    for _, it := range modnames {
        fmt.Printf("%x %s\n", it.Offset, it.Module)
    }
}
