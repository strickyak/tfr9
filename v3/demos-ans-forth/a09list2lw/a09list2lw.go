package main

import (
	"fmt"
	"os"
	"regexp"
	// "log"
	"bufio"
	"strconv"
	"strings"
)

// EXAMPLE:
//  IN:
//   005A: 000000000000...   177 | forth__leave_stack      fdb     0,0,0,0,0,0,0,0
//  OUT:
//   12AC 10260091         (     _forth.lwasm):00946                       lbne  forth__private_number_q_xt.error_ret

const IN = `(....)[:](................)\s+([0-9]+) [|] (.*)$`

var PARSE = regexp.MustCompile(IN).FindStringSubmatch

func main() {

	scan := bufio.NewScanner(os.Stdin)

	for scan.Scan() {
		text := scan.Text()
		text = strings.TrimRight(text, "\n\r \t")
		m := PARSE(text)
		if m != nil {
			addr, codes, num, src := m[1], m[2], m[3], m[4]

			codes = strings.ReplaceAll(codes, ".", "")
			codes = strings.ReplaceAll(codes, " ", "")

			// Forth is in the first half of RAM; tests are in the second half.  Skip tests.
			if addr < "8000" {
				fmt.Printf("%s %-16s (.................):%05d          %s\n", addr, codes, atoi(num), src)
				continue
			}
		}
		fmt.Printf("* %q\n", text)
	}
}

func atoi(s string) int {
	z, _ := strconv.ParseInt(s, 10, 32)
	return int(z)
}
