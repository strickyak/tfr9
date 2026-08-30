package main

import (
    "os"
    "io/ioutil"
    "fmt"

    "github.com/strickyak/tfr9/v2/listings"

	. "github.com/strickyak/gomar/gu"
)
func main() {
    a := Value(ioutil.ReadAll(os.Stdin))
    modnames := listings.LsMod(a)

    for _, it := range modnames {
        fmt.Printf("%x %s\n", it.Offset, it.Module)
    }
}
