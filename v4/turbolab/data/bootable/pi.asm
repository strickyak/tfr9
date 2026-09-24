; ==============================================================================
;  Rabinowitz-Wagon Pi Spigot Algorithm (100 Digits) for Motorola 6809
;  Origin: $1000 | Initial PC: $1000 | Initial S: $1000
;  Data & Stack: $0000 - $0FFF
;  Output Port: $FF00 (Memory-mapped ASCII character sink)
; ==============================================================================

PUTCHAR_PORT equ     $FF00       ; Output character destination
NUM_DIGITS   equ     100         ; Target decimal digits
ARRAY_LEN    equ     335         ; L = floor(10 * 100 / 3) + 2

; ------------------------------------------------------------------------------
;  Direct Page Allocation ($0000 - $00FF)
; ------------------------------------------------------------------------------
             org     $0000
carry        rmb     2           ; 16-bit carry propagated between terms
cur_i        rmb     2           ; 16-bit loop counter 'i' (334 down to 1)
digit_cnt    rmb     1           ; Outer loop counter (0 to 99)
nines_cnt    rmb     1           ; Accumulated count of deferred '9's
predigit     rmb     1           ; Buffered previous digit pending carry
cand_q       rmb     1           ; Candidate digit from base-10 division
div_q        rmb     2           ; 16-bit quotient from DIV16_16
div_r        rmb     2           ; 16-bit remainder from DIV16_16
temp16       rmb     2           ; Scratch 16-bit temporary
denom        rmb     2           ; 16-bit divisor for DIV16_16
div_cnt      rmb     1           ; Loop counter for DIV16_16
dot_emitted  rmb     1           ; Flag: non-zero after decimal point output

; ------------------------------------------------------------------------------
;  Data Buffers
; ------------------------------------------------------------------------------
; Array A stores the mixed-radix numerators (335 words = 670 bytes).
; Placed at $0200 to leave ample room ($0500-$0FFF) for the descending stack.
ARRAY_A      equ     $0200

; ==============================================================================
;  Entry Point
; ==============================================================================
             org     $1000

START:
             ; Ensure Direct Page register points to $00
             clra
             tfr     a,dp

             ; -----------------------------------------------------------------
             ; Step 1: Initialize working array A[0..ARRAY_LEN-1] = 2 (16-bit words)
             ; -----------------------------------------------------------------
             ldx     #ARRAY_A
             ldy     #ARRAY_LEN
             ldd     #2
INIT_LOOP:
             std     ,x++
             leay    -1,y
             bne     INIT_LOOP

             ; Initialize state variables
             clr     nines_cnt
             clr     predigit
             clr     digit_cnt
             clr     dot_emitted

; ------------------------------------------------------------------------------
; Step 2: Main outer loop - emits one digit per iteration
; ------------------------------------------------------------------------------
DIGIT_LOOP:
             ; carry = 0
             ldd     #0
             std     carry

             ; cur_i = ARRAY_LEN - 1 (334)
             ldd     #(ARRAY_LEN-1)
             std     cur_i

; ------------------------------------------------------------------------------
; Step 3: Inner loop - process mixed-radix fractions from right to left
; ------------------------------------------------------------------------------
INNER_LOOP:
             ; Calculate denominator: denom = 2 * cur_i + 1
             ldd     cur_i
             aslb
             rola                ; D = 2 * cur_i
             addd    #1          ; D = 2 * cur_i + 1
             std     denom

             ; Calculate dividend: val = (A[cur_i] * 10) + carry
             ; Address of A[cur_i] = ARRAY_A + 2 * cur_i
             ldd     cur_i
             aslb
             rola                ; D = 2 * cur_i
             addd    #ARRAY_A    ; D = &ARRAY_A[cur_i]
             tfr     d,x         ; X = &ARRAY_A[cur_i]

             ; D = A[cur_i] * 10
             ldd     ,x          ; D = A[cur_i]
             aslb
             rola                ; D = A[cur_i] * 2
             std     temp16
             aslb
             rola                ; D = A[cur_i] * 4
             aslb
             rola                ; D = A[cur_i] * 8
             addd    temp16      ; D = A[cur_i] * 10
             addd    carry       ; D = (A[cur_i] * 10) + carry

             ; Divide: D / denom -> div_q (quotient), div_r (remainder)
             lbsr    DIV16_16

             ; A[cur_i] = remainder (div_r)
             ldd     div_r
             std     ,x          ; X was preserved by DIV16_16

             ; carry = quotient * cur_i
             ; (cur_i * div_q) = (cur_i_hi * div_q << 8) + (cur_i_lo * div_q)
             lda     cur_i       ; cur_i high byte
             ldb     div_q+1     ; quotient
             mul                 ; D = cur_i_hi * quot
             tfr     b,a
             clrb                ; D = (cur_i_hi * quot) << 8
             pshs    d
             lda     cur_i+1     ; cur_i low byte
             ldb     div_q+1     ; quotient
             mul                 ; D = cur_i_lo * quot
             addd    ,s++        ; D = cur_i * quotient
             std     carry

             ; Loop backward: cur_i = cur_i - 1, continue while cur_i > 0
             ldd     cur_i
             subd    #1
             std     cur_i
             lbne    INNER_LOOP

; ------------------------------------------------------------------------------
; Step 4: Process term i = 0 (base 10 extract)
; ------------------------------------------------------------------------------
             ; Divisor for base 10
             ldd     #10
             std     denom

             ; val0 = (A[0] * 10) + carry
             ldd     ARRAY_A     ; D = A[0]
             aslb
             rola                ; D = A[0] * 2
             std     temp16
             aslb
             rola                ; D = A[0] * 4
             aslb
             rola                ; D = A[0] * 8
             addd    temp16      ; D = A[0] * 10
             addd    carry       ; D = val0

             ; Divide D by 10 (base 10)
             lbsr    DIV16_16

             ; A[0] = remainder (val0 % 10)
             ldd     div_r
             std     ARRAY_A

             ; q = quotient (val0 / 10) -> in div_q+1
             lda     div_q+1
             sta     cand_q      ; Save candidate digit

; ------------------------------------------------------------------------------
; Step 5: Carry buffer & deferred '9' output logic
; ------------------------------------------------------------------------------
             cmpa    #9
             bne     CHECK_OVERFLOW

             ; If q == 9: increment deferred nines and don't flush predigit yet
             inc     nines_cnt
             bra     NEXT_DIGIT

CHECK_OVERFLOW:
             cmpa    #10
             bne     NORMAL_DIGIT

             ; --- q == 10: Carry wave propagated through pending 9s ---
             ; Output (predigit + 1)
             lda     predigit
             inca
             lbsr    EMIT_DIGIT

             ; Output all deferred 9s as '0's
             ldb     nines_cnt
             beq     FLUSH_CARRY_DONE
FLUSH_CARRY_NINES:
             lda     #0
             lbsr    EMIT_DIGIT
             decb
             bne     FLUSH_CARRY_NINES
FLUSH_CARRY_DONE:
             clr     predigit
             clr     nines_cnt
             bra     NEXT_DIGIT

NORMAL_DIGIT:
             ; --- Regular digit (0-8) ---
             ; Flush predigit if this is not the first pass
             tst     digit_cnt
             beq     SET_PREDIGIT

             lda     predigit
             lbsr    EMIT_DIGIT

             ; Flush any deferred 9s as '9's
             ldb     nines_cnt
             beq     SET_PREDIGIT
FLUSH_NORMAL_NINES:
             lda     #9
             lbsr    EMIT_DIGIT
             decb
             bne     FLUSH_NORMAL_NINES

SET_PREDIGIT:
             clr     nines_cnt
             lda     cand_q
             sta     predigit    ; Current q becomes new predigit

NEXT_DIGIT:
             inc     digit_cnt
             lda     digit_cnt
             cmpa    #NUM_DIGITS
             lblt    DIGIT_LOOP

             ; Flush the final buffered predigit
             lda     predigit
             lbsr    EMIT_DIGIT

             ; Output trailing newline (ASCII $0A)
             lda     #$0A
             sta     PUTCHAR_PORT

HALT:
             swi                 ; Exit program via SWI

; ==============================================================================
;  Subroutine: EMIT_DIGIT
;  Input:  A = raw digit value (0..9)
;  Side effect: Writes ASCII to PUTCHAR_PORT ($FF00).
;               Inserts decimal point '.' right after the leading '3'.
; ==============================================================================
EMIT_DIGIT:
             pshs    a
             adda    #'0'        ; Convert to ASCII
             sta     PUTCHAR_PORT

             ; Check if decimal point has been emitted yet
             tst     dot_emitted
             bne     EMIT_EXIT   ; Already emitted

             ; Emit decimal point
             lda     #'.'
             sta     PUTCHAR_PORT
             sta     dot_emitted

EMIT_EXIT:
             puls    a,pc

; ==============================================================================
;  Subroutine: DIV16_16
;  Unsigned division: 16-bit dividend / 16-bit divisor
;  Inputs:   D = Dividend (16-bit)
;            denom = Divisor (16-bit in DP, non-zero)
;  Outputs:  div_q = 16-bit Quotient
;            div_r = 16-bit Remainder
;  Preserves: X
; ==============================================================================
DIV16_16:
             pshs    cc,d,x
             std     div_q       ; Store dividend as initial remainder in div_q
             ldd     denom
             std     temp16      ; Divisor in temp16

             lda     #16
             sta     div_cnt

             ldd     #0          ; D will accumulate remainder bits
DIV_LOOP:
             ; Shift quotient left; high bit of dividend shifts into D
             asl     div_q+1
             rol     div_q
             rolb
             rola

             ; Check if remainder (D) >= divisor (temp16)
             cmpd    temp16
             blo     DIV_NEXT

             ; Subtract divisor
             subd    temp16
             ; Set lowest bit of quotient
             inc     div_q+1

DIV_NEXT:
             dec     div_cnt
             bne     DIV_LOOP

             std     div_r       ; Remainder (16-bit)
             puls    cc,d,x,pc

             end     START
