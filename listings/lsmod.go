package listings

import (
	"bytes"
	"fmt"
	"strings"
    "os"

	. "github.com/strickyak/gomar/gu"
)

type ModOff struct {
	Module string
	Offset uint
}

func GetName(a []byte, nameOff uint) string {
	var bb bytes.Buffer
    i := uint(0)
	for {
		b := a[nameOff+i]
		c := b & 0x7F
		AssertLE('.', c, a[:20], nameOff)
		AssertLE(c, 'z')
		bb.WriteByte(c)
		if (b & 0x80) != 0 {
			break
		}
        i++
	}
	return bb.String()
}

func LsMod(a []byte) (modnames []ModOff) {
	var offset uint
    for len(a) > 16 {
        for len(a) > 16 && a[0] == 0x87 && a[1] == 0xCD {
            size := uint(a[2])<<8 | uint(a[3])
            nameOff := uint(a[4])<<8 | uint(a[5])
            println("len", len(a), "size", size, "nameOff", nameOff)
            fmt.Fprintf(os.Stderr, "% 3x\n", a[:16])
            name := strings.ToLower(GetName(a, nameOff))
            fullname := fmt.Sprintf("%s.%04x%02x%02x%02x", name, size, a[size-3], a[size-2], a[size-1])
            println("name", name, fullname)
            modnames = append(modnames, ModOff{fullname, offset})
            offset += size
            a = a[size:]
            println()
        }
        if len(a) > 16 {
            a = a[1:]
            offset++
        }
	}
	return
}
