********************************************************************
* clif - Tiny Forth CLI Command Module for OS-9
*
* Executes Forth commands passed via the command line.
* Supports 16-bit integers and DO...LOOP in interpretation mode.
********************************************************************

                    nam       clif
                    ttl       Tiny Forth CLI Command

Level               equ       1
                    use       os9.d
C$CR                equ       $0D

tylg                set       Prgrm+Objct
atrv                set       ReEnt+rev
rev                 set       $00
edition             set       1

mod_base:           mod       eom,name,tylg,atrv,start,size

                    org       0
* Direct Page variables ($00..$FF):
dp_base             rmb       2                   ; $00 Base address of DP (initial U)
inptr               rmb       2                   ; $02 Current input pointer
lsp                 rmb       2                   ; $04 Loop stack pointer
outptr              rmb       2                   ; $06 Output buffer write pointer
outstart            rmb       2                   ; $08 Output buffer start address
out_total           rmb       2                   ; $0A Total output bytes emitted
toklen              rmb       1                   ; $0C Length of current token
num_neg             rmb       1                   ; $0D Negative flag for number parser
num_base            rmb       1                   ; $0E Number base (10 or 16)
div_rem             rmb       1                   ; $0F Remainder for div10
temp1               rmb       2                   ; $10 Scratch 16-bit
op1                 rmb       2                   ; $12 Operand 1 for multiply
op2                 rmb       2                   ; $14 Operand 2 for multiply

tokbuf              rmb       32                  ; Token buffer (null-terminated)
outbuf              rmb       128                 ; Output buffer (128 bytes)
LStackBase          rmb       64                  ; Loop stack area (64 bytes = ~10 frames)
LStackTop           equ       .

* Forth Data Stack:
                    rmb       256                 ; Forth Data Stack (256 bytes = 128 words)
DataStackTop        equ       .

* Safety margin for S (hardware stack):
                    rmb       512
size                equ       .

name                fcs       /clif/
                    fcb       edition

start:
                    ; Set Direct Page to point to allocated memory at U
                    tfr       u,d
                    tfr       a,dp

                    ; Save direct page base (initial U)
                    std       <dp_base

                    ; inptr points to parameter string in X
                    stx       <inptr

                    ; Initialize Loop Stack pointer
                    ldd       #LStackTop
                    addd      <dp_base
                    std       <lsp

                    ; Initialize Output Buffer
                    ldd       #outbuf
                    addd      <dp_base
                    std       <outstart
                    std       <outptr
                    clra
                    clrb
                    std       <out_total

                    ; Initialize Forth Data Stack in U
                    leau      DataStackTop,u

interp_loop:
                    lbsr      NextToken
                    beq       finish              ; Z=1 -> end of input string

                    ; Search dictionary
                    lbsr      find_word
                    bcs       try_number          ; Carry=1 -> not found in dict

                    ; Found in dictionary: execute word
                    ; find_word returns routine offset in D (relative to mod_base)
                    leax      mod_base,pcr
                    leax      d,x
                    jsr       ,x
                    bra       interp_loop

try_number:
                    lbsr      parse_num
                    bcs       syntax_error        ; Carry=1 -> not a number either!

                    ; Number parsed successfully in D
                    pshu      d                   ; Push 16-bit number to Forth data stack
                    bra       interp_loop

syntax_error:
                    ; Flush pending output
                    lbsr      flush_out
                    ; Print "? "
                    leax      err_msg,pcr
                    ldy       #2
                    lda       #1
                    os9       I$Write
                    ; Print token
                    ldd       <dp_base
                    addd      #tokbuf
                    tfr       d,x
                    ldb       <toklen
                    clra
                    tfr       d,y
                    lda       #1
                    os9       I$Write
                    ; Print newline
                    leax      cr_msg,pcr
                    ldy       #1
                    lda       #1
                    os9       I$Write
                    ; Exit with error code
                    ldb       #E$IllArg
                    os9       F$Exit

finish:
                    ; If any output was produced, emit newline
                    ldd       <out_total
                    beq       no_out
                    lda       #C$CR
                    lbsr      emit_char
                    lbsr      flush_out
no_out:
                    clrb
                    os9       F$Exit

err_msg             fcc       "? "
cr_msg              fcb       C$CR

********************************************************************
* NextToken - Extract next whitespace-delimited token into tokbuf
* Returns: Z=1 if no more tokens, Z=0 if token extracted
********************************************************************
NextToken:
                    ldx       <inptr
skip_ws:
                    lda       ,x+
                    cmpa      #' '
                    beq       skip_ws
                    cmpa      #9                  ; tab
                    beq       skip_ws
                    cmpa      #'"'                ; quote
                    beq       skip_ws
                    cmpa      #C$CR
                    beq       at_eol
                    tsta
                    beq       at_eol

                    ; Non-whitespace found; back up to first char
                    leax      -1,x
                    ldy       <dp_base
                    leay      tokbuf,y
                    clrb

copy_tok:
                    lda       ,x+
                    cmpa      #' '
                    beq       tok_delim
                    cmpa      #9
                    beq       tok_delim
                    cmpa      #'"'                ; quote
                    beq       tok_delim
                    cmpa      #C$CR
                    beq       tok_eol
                    tsta
                    beq       tok_eol

                    ; Convert lowercase to uppercase
                    cmpa      #'a'
                    blo       not_lower
                    cmpa      #'z'
                    bhi       not_lower
                    suba      #$20
not_lower:
                    sta       ,y+
                    incb
                    cmpb      #30                 ; max token length 30
                    blo       copy_tok

skip_long:
                    lda       ,x+
                    cmpa      #' '
                    beq       tok_delim
                    cmpa      #9
                    beq       tok_delim
                    cmpa      #'"'
                    beq       tok_delim
                    cmpa      #C$CR
                    beq       tok_eol
                    tsta
                    beq       tok_eol
                    bra       skip_long

tok_eol:
                    leax      -1,x                ; re-examine CR or null on next call
tok_delim:
                    stx       <inptr              ; save current pointer
                    clr       ,y                  ; null-terminate tokbuf
                    stb       <toklen
                    andcc     #$FB                ; clear Z (token found)
                    rts

at_eol:
                    leax      -1,x                ; stay at EOL
                    stx       <inptr
                    clr       <toklen
                    orcc      #$04                ; set Z (no token)
                    rts

********************************************************************
* find_word - Search dictionary for tokbuf
* Returns: CC C=0 on match (D = routine offset relative to dict),
*          CC C=1 on not found
********************************************************************
find_word:
                    leax      dict,pcr
search_loop:
                    lda       ,x+                 ; length of dict entry name
                    beq       not_found           ; 0 = end of dictionary
                    cmpa      <toklen             ; does length match?
                    bne       skip_entry

                    ; Length matches; compare characters
                    ldy       <dp_base
                    leay      tokbuf,y
                    pshs      a                   ; counter = length
cmp_chars:
                    lda       ,x+
                    cmpa      ,y+
                    bne       mismatch
                    dec       ,s
                    bne       cmp_chars
                    leas      1,s                 ; discard counter

                    ; Match found! Load routine offset
                    ldd       ,x
                    andcc     #$FE                ; clear Carry (found)
                    rts

mismatch:
                    puls      b                   ; B = chars counter (including current)
                    decb                          ; current char was already consumed by lda ,x+
                    leax      b,x                 ; skip remaining chars of name
                    leax      2,x                 ; skip 2-byte routine offset
                    bra       search_loop

skip_entry:
                    leax      a,x                 ; skip name
                    leax      2,x                 ; skip 2-byte offset
                    bra       search_loop

not_found:
                    orcc      #$01                ; set Carry (not found)
                    rts

********************************************************************
* parse_num - Parse tokbuf as 16-bit signed/unsigned integer
* Supports decimal and hex (prefixed with '$')
* Returns: CC C=0 on success (D = value), CC C=1 on failure
********************************************************************
parse_num:
                    ldb       <toklen
                    lbeq      pnum_fail

                    ldx       <dp_base
                    leax      tokbuf,x

                    clr       <num_neg
                    lda       ,x
                    cmpa      #'-'
                    bne       not_neg
                    decb                          ; was token just '-'?
                    beq       pnum_fail           ; '-' alone is not a number
                    inc       <num_neg
                    leax      1,x
not_neg:
                    ; Check base
                    lda       #10
                    sta       <num_base
                    lda       ,x
                    cmpa      #'$'
                    bne       not_hex
                    decb                          ; was token just '$'?
                    beq       pnum_fail
                    lda       #16
                    sta       <num_base
                    leax      1,x
not_hex:
                    ; Accumulate in stack
                    clra
                    clrb
                    pshs      d                   ; acc on stack at ,s

pnum_lp:
                    lda       ,x+
                    beq       pnum_ok

                    ; Convert char to digit value 0..15 in A
                    cmpa      #'0'
                    blo       pnum_fail_pop
                    cmpa      #'9'
                    bhi       pnum_hex
                    suba      #'0'
                    bra       pnum_got_dig

pnum_hex:
                    cmpa      #'A'
                    blo       pnum_fail_pop
                    cmpa      #'F'
                    bhi       pnum_fail_pop
                    suba      #'A'-10

pnum_got_dig:
                    cmpa      <num_base
                    bhs       pnum_fail_pop

                    ; Save 16-bit digit in temp1
                    clr       <temp1
                    sta       <temp1+1

                    ; Check base before loading accumulator
                    lda       <num_base
                    cmpa      #16
                    beq       pnum_m16

                    ; Multiply accumulator by 10
                    ldd       ,s                  ; current accumulator
                    lslb
                    rola                          ; D * 2
                    pshs      d
                    lslb
                    rola
                    lslb
                    rola                          ; D * 8
                    addd      ,s++                ; D * 10
                    bra       pnum_add

pnum_m16:
                    ldd       ,s                  ; current accumulator
                    lslb
                    rola
                    lslb
                    rola
                    lslb
                    rola
                    lslb
                    rola                          ; D * 16

pnum_add:
                    addd      <temp1              ; add digit to (acc * base)
                    std       ,s                  ; update accumulator on stack
                    bra       pnum_lp

pnum_ok:
                    puls      d                   ; D = accumulated value
                    tst       <num_neg
                    beq       pnum_nonneg
                    coma
                    comb
                    addd      #1
pnum_nonneg:
                    andcc     #$FE                ; clear Carry (success)
                    rts

pnum_fail_pop:
                    leas      2,s                 ; drop accumulator
pnum_fail:
                    orcc      #$01                ; set Carry (failure)
                    rts

********************************************************************
* div10 - Unsigned 16-bit division by 10
* In:  D = dividend (16-bit)
* Out: D = quotient, <div_rem = remainder (0..9)
********************************************************************
div10:
                    clr       <div_rem
                    ldx       #16                 ; loop counter
div10_lp:
                    lslb
                    rola
                    rol       <div_rem
                    pshs      a                   ; preserve high byte of D
                    lda       <div_rem
                    cmpa      #10
                    blo       div10_no_sub
                    suba      #10
                    sta       <div_rem
                    orb       #1                  ; set bit 0 of quotient
div10_no_sub:
                    puls      a                   ; restore high byte of D
                    leax      -1,x                ; decrement loop counter
                    bne       div10_lp
                    rts

********************************************************************
* print_dec - Print signed 16-bit integer in decimal
* In: D = signed 16-bit integer
********************************************************************
print_dec:
                    pshs      d,x,y
                    tsta
                    bpl       pdec_pos
                    pshs      d
                    lda       #'-'
                    lbsr      emit_char
                    puls      d
                    coma
                    comb
                    addd      #1
pdec_pos:
                    cmpd      #0
                    bne       pdec_nonzero
                    lda       #'0'
                    lbsr      emit_char
                    puls      d,x,y,pc

pdec_nonzero:
                    std       <temp1              ; preserve D
                    clra
                    pshs      a                   ; sentinel 0
                    ldd       <temp1              ; restore D
pdec_lp:
                    lbsr      div10               ; D = D/10, div_rem = digit
                    std       <temp1              ; save quotient D
                    lda       <div_rem
                    adda      #'0'
                    pshs      a                   ; push digit to stack
                    ldd       <temp1              ; reload quotient D (sets Z flag)
                    bne       pdec_lp
pdec_pop:
                    puls      a
                    tsta
                    beq       pdec_done
                    lbsr      emit_char
                    bra       pdec_pop
pdec_done:
                    puls      d,x,y,pc

********************************************************************
* emit_char - Output a character, buffering as needed
* In: A = character
********************************************************************
emit_char:
                    pshs      d,x,y
                    ldx       <outptr
                    sta       ,x+
                    stx       <outptr
                    inc       <out_total+1
                    bne       emit_cnt_ok
                    inc       <out_total
emit_cnt_ok:
                    tfr       x,d
                    subd      <outstart
                    cmpb      #120
                    blo       emit_ret
                    lbsr      flush_out
emit_ret:
                    puls      d,x,y,pc

********************************************************************
* flush_out - Flush output buffer to stdout
********************************************************************
flush_out:
                    pshs      d,x,y
                    ldd       <outptr
                    subd      <outstart
                    beq       flush_ret
                    tfr       d,y                 ; byte count
                    ldx       <outstart           ; buffer address
                    lda       #1                  ; stdout path
                    os9       I$Write
                    ldd       <outstart
                    std       <outptr
flush_ret:
                    puls      d,x,y,pc

********************************************************************
* Forth Word Implementations
********************************************************************

* SWAP ( a b -- b a )
word_swap:
                    ldd       ,u
                    ldx       2,u
                    std       2,u
                    stx       ,u
                    rts

* ! ( n addr -- )
word_store:
                    pulu      x                   ; address
                    pulu      d                   ; value
                    std       ,x
                    rts

* @ ( addr -- n )
word_fetch:
                    pulu      x
                    ldd       ,x
                    pshu      d
                    rts

* + ( n1 n2 -- sum )
word_plus:
                    pulu      d
                    addd      ,u
                    std       ,u
                    rts

* - ( n1 n2 -- diff )  n1 - n2
word_minus:
                    pulu      d                   ; D = n2
                    std       <temp1
                    ldd       ,u                  ; D = n1
                    subd      <temp1              ; D = n1 - n2
                    std       ,u
                    rts

* * ( n1 n2 -- prod ) 16-bit multiply
word_mul:
                    pulu      d
                    std       <op2                ; op2 = n2
                    ldd       ,u
                    std       <op1                ; op1 = n1
                    lda       <op1+1              ; op1_l
                    ldb       <op2                ; op2_h
                    mul
                    pshs      b                   ; cross 1
                    lda       <op1                ; op1_h
                    ldb       <op2+1              ; op2_l
                    mul
                    addb      ,s+                 ; cross 1 + cross 2
                    pshs      b
                    lda       <op1+1              ; op1_l
                    ldb       <op2+1              ; op2_l
                    mul                           ; A = high, B = low
                    adda      ,s+                 ; add cross to A
                    std       ,u                  ; replace TOS
                    rts

* . ( n -- ) print decimal and space
word_dot:
                    pulu      d
                    lbsr      print_dec
                    lda       #' '
                    lbsr      emit_char
                    rts

* DO ( limit index -- )
word_do:
                    pulu      d                   ; index
                    pulu      x                   ; limit
                    ldy       <lsp
                    leay      -6,y
                    sty       <lsp
                    std       2,y                 ; frame.index
                    stx       ,y                  ; frame.limit
                    ldx       <inptr
                    stx       4,y                 ; frame.start_ptr
                    rts

* LOOP ( -- )
word_loop:
                    ldy       <lsp
                    ldd       2,y                 ; index
                    addd      #1
                    std       2,y
                    cmpd      ,y                  ; index == limit?
                    beq       loop_done
                    ldx       4,y                 ; restart at start_ptr
                    stx       <inptr
                    rts
loop_done:
                    leay      6,y                 ; pop loop frame
                    sty       <lsp
                    rts

* I ( -- index )
word_i:
                    ldy       <lsp
                    ldd       2,y
                    pshu      d
                    rts

* DUP ( a -- a a )
word_dup:
                    ldd       ,u
                    pshu      d
                    rts

* DROP ( a -- )
word_drop:
                    leau      2,u
                    rts

* OVER ( a b -- a b a )
word_over:
                    ldd       2,u
                    pshu      d
                    rts

* ROT ( a b c -- b c a )
word_rot:
                    ldd       4,u
                    ldx       2,u
                    ldy       ,u
                    stx       4,u
                    sty       2,u
                    std       ,u
                    rts

* AND ( n1 n2 -- n3 )
word_and:
                    pulu      d
                    anda      ,u
                    andb      1,u
                    std       ,u
                    rts

* OR ( n1 n2 -- n3 )
word_or:
                    pulu      d
                    ora       ,u
                    orb       1,u
                    std       ,u
                    rts

* XOR ( n1 n2 -- n3 )
word_xor:
                    pulu      d
                    eora      ,u
                    eorb      1,u
                    std       ,u
                    rts

* ? ( addr -- ) fetch and print
word_question:
                    pulu      x
                    ldd       ,x
                    pshu      d
                    lbra      word_dot

* C! ( c addr -- )
word_cstore:
                    pulu      x
                    pulu      d
                    stb       ,x
                    rts

* C@ ( addr -- c )
word_cfetch:
                    pulu      x
                    clra
                    ldb       ,x
                    pshu      d
                    rts

* CR ( -- )
word_cr:
                    lda       #C$CR
                    lbsr      emit_char
                    rts

* SPACE ( -- )
word_space:
                    lda       #' '
                    lbsr      emit_char
                    rts

* EMIT ( c -- )
word_emit:
                    pulu      d
                    tfr       b,a
                    lbsr      emit_char
                    rts

* BYE ( -- )
word_bye:
                    lbra      finish

********************************************************************
* Dictionary Table
********************************************************************
dict:
                    fcb       4
                    fcc       "SWAP"
                    fdb       word_swap

                    fcb       1
                    fcc       "!"
                    fdb       word_store

                    fcb       4
                    fcc       "POKE"
                    fdb       word_store

                    fcb       5
                    fcc       "STORE"
                    fdb       word_store

                    fcb       2
                    fcc       "W!"
                    fdb       word_store

                    fcb       1
                    fcc       "@"
                    fdb       word_fetch

                    fcb       4
                    fcc       "PEEK"
                    fdb       word_fetch

                    fcb       5
                    fcc       "FETCH"
                    fdb       word_fetch

                    fcb       2
                    fcc       "W@"
                    fdb       word_fetch

                    fcb       1
                    fcc       "+"
                    fdb       word_plus

                    fcb       1
                    fcc       "-"
                    fdb       word_minus

                    fcb       1
                    fcc       "*"
                    fdb       word_mul

                    fcb       1
                    fcc       "."
                    fdb       word_dot

                    fcb       2
                    fcc       "DO"
                    fdb       word_do

                    fcb       4
                    fcc       "LOOP"
                    fdb       word_loop

                    fcb       1
                    fcc       "I"
                    fdb       word_i

                    fcb       3
                    fcc       "DUP"
                    fdb       word_dup

                    fcb       4
                    fcc       "DROP"
                    fdb       word_drop

                    fcb       4
                    fcc       "OVER"
                    fdb       word_over

                    fcb       3
                    fcc       "ROT"
                    fdb       word_rot

                    fcb       3
                    fcc       "AND"
                    fdb       word_and

                    fcb       2
                    fcc       "OR"
                    fdb       word_or

                    fcb       3
                    fcc       "XOR"
                    fdb       word_xor

                    fcb       1
                    fcc       "?"
                    fdb       word_question

                    fcb       2
                    fcc       "C!"
                    fdb       word_cstore

                    fcb       5
                    fcc       "CPOKE"
                    fdb       word_cstore

                    fcb       2
                    fcc       "C@"
                    fdb       word_cfetch

                    fcb       5
                    fcc       "CPEEK"
                    fdb       word_cfetch

                    fcb       2
                    fcc       "CR"
                    fdb       word_cr

                    fcb       5
                    fcc       "SPACE"
                    fdb       word_space

                    fcb       4
                    fcc       "EMIT"
                    fdb       word_emit

                    fcb       3
                    fcc       "BYE"
                    fdb       word_bye

                    fcb       0                   ; end of dictionary

                    emod
eom                 equ       *
                    end
