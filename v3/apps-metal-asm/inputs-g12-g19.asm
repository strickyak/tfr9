    ttl inputs-g12-g19.asm

ORIGIN equ $4000    ; arbitrary load address
TXPORT equ $FF00    ; TurboSim putchar address

DIR_PORT  equ $FF06    ; tfr9's pico-io's GPIO[12:19] Direction Port
DATA_PORT equ $FF07    ; tfr9's pico-io's GPIO[12:19] Data Port

    org ORIGIN

entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print
next_char:
    ldb ,x+         ; get next char, and advance pointer
    beq run         ; if char is 0 (EOS), run the counting
    stb TXPORT      ; write char to Terminal TX port
    bra next_char        ; go do next char

run:
    clrb            ; All inputs.
    stb DIR_PORT    ; set the directions.
    ldx #1000       ; delay time

loop:
    ldb DATA_PORT   ; read inputs
    bsr print_b_binary
    bsr delay_x
    bra loop


;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
delay_x:   ; in: X=duration.   preserves all but CC.
    pshs D,X
loop@:
    mul
    mul
    mul
    mul
    mul
    leax -1,x
    bne loop@
    puls D,X,PC

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
print_b_binary: ; in: B=number.   preserves all but CC.
    pshs X,D
    ldx #8

loop@:
    lda #'0
    lslb         ; move high bit into carry
    bcc skip@
    inca         ; change '0' to '1'
skip@:
    sta TXPORT   ; putchar
    leax -1,x
    bne loop@

    lda #10      ; newline
    sta TXPORT   ; putchar
    puls D,X,PC
    
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
message:
    .ascii "Going to Ditto in14 to out15 ..."
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
