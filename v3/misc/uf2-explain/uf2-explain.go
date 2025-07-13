package main

import (
    "encoding/binary"
    "fmt"
    "log"
    "os"
)

type Block struct {
    Magic1 uint32
    Magic2 uint32
    Flags uint32
    Dest uint32
    Used uint32
    Seq uint32
    Count uint32
    Family uint32
    Payload [476]byte
    Magic3 uint32
}

func (b Block) String() string {
    if b.Magic1 != 0x0A324655 {
        panic(b.Magic1)
    }
    if b.Magic2 != 0x9E5D5157 {
        panic(b.Magic1)
    }
    if b.Magic3 != 0x0AB16F30 {
        panic(b.Magic1)
    }
    return fmt.Sprintf("id=%x fl=%x [%d./%d.] %x@%x %q", b.Family, b.Flags, b.Seq, b.Count, b.Used, b.Dest, b.Payload[:])
}


func main() {
    for {
        var block Block
        err := binary.Read( os.Stdin, binary.LittleEndian, &block);
        if err != nil {
            log.Printf("binary.Read: %v", err)
            break
        }
        fmt.Printf("%v\n", block)
    }
}
