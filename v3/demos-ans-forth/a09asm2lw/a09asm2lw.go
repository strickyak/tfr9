// Convert spc476's a09-syntax forth.asm into Lost Wizard's lwasm-syntax,
// eliminating .test/.endtst blocks.
package main

import (
    "log"
    "regexp"
    "fmt"
    "os"
    "bufio"
    "strings"
)


const SYM = `([A-Za-z0-9_.@$]*)`
const ASM = `^` + SYM + `[:]?\s*` + SYM + `\s*([^;]*)([;].*)?$`

var PARSE = regexp.MustCompile(ASM).FindStringSubmatch

func main() {
    fmt.Printf("        ORG    ORIGIN\n")

    scan := bufio.NewScanner(os.Stdin)
    skip := false
    local := "UNKNOWN"
    for scan.Scan() {
        text := scan.Text()
        text = strings.TrimRight(text, "\n\r \t")
        m := PARSE(text)
        if m != nil {
            label, op, arg, remark := m[1], m[2], m[3], m[4]

            if op == ".opt" {
                fmt.Printf("******* SKIP %q\n", text)
                continue
            }

            if op == ".test" {
                skip = true
            }

            if op == ".endtst" {
                skip = false
                fmt.Printf("******* SKIP %q\n", text)
                continue
            }

            if skip {
                fmt.Printf("******* SKIP %q\n", text)
                continue
            }

            // fmt.Printf("[%s]  <%s>  (%s)  ;;;;%s\n", label, op, arg, remark)
            if !strings.HasPrefix(arg, "'") && !strings.HasPrefix(arg, "\"") {
                arg = strings.ReplaceAll(arg, " ", "")
                arg = strings.ReplaceAll(arg, "\t", "")
            }

            if strings.HasPrefix(label, ".") {
                label = fmt.Sprintf("%s%s", local, label)
            }
            if strings.HasPrefix(arg, ".") {
                arg = fmt.Sprintf("%s%s", local, arg)
            }
            if strings.HasPrefix(arg, "#.") {
                arg = fmt.Sprintf("#%s%s", local, arg[1:])
            }
            arg = strings.ReplaceAll(arg, "-.", "-" + local + ".")

            switch op {
            case "bls":
                op = "lbls"
            case "bsr":
                op = "lbsr"
            case "ascii":
                op = ".ascii"
            case "orcc":
                switch arg {
                case "{c}":
                    arg = "#$01"
                case "{z}":
                    arg = "#$04"
                }
            case "andcc": 
                switch arg {
                case "{c}":
                    arg = "#$FE"
                case "{z}":
                    arg = "#$FB"
                }
            }

            if len(label + op + arg) == 0 {
                fmt.Printf("    %s\n", remark)
            } else {
                arg = FixDoubleColon(arg, local)
                arg = strings.ReplaceAll(arg, "(.", "(" + local + ".")
                fmt.Printf("%-20s  %12s  %-20s  %s\n", label, op, arg, remark)
            }

            if op == "" && label != "" && !strings.HasPrefix(label, ".") {
                local = label
            }
        } else {
            log.Fatalf("FAIL ---- %q\n", text)
        }
    }
}

var FIND_DOUBLE_COLON = regexp.MustCompile(`^([#]?)([^:]+)[:][:](.*)$`).FindStringSubmatch

func FixDoubleColon(arg string, local string) string {
    m := FIND_DOUBLE_COLON(arg)
    if m == nil {
        return arg
    }

    pound, left, right := m[1], m[2], m[3]
    //if right == ".xt-.name" {
        //right = fmt.Sprintf("L%s.xt-L%s.name", local, local)
    //}

    return pound + fmt.Sprintf("((%s)*256)+((%s)%%255)", left, right)
}
