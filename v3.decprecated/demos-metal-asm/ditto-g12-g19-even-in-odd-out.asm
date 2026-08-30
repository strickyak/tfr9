    ttl ditto-g12-g19-even-in-odd-out.asm

* This program repeatedly copies 4 input bits top 4 output bits:
*   G13 outputs what is input on G12.
*   G15 outputs what is input on G14.
*   G17 outputs what is input on G16.
*   G19 outputs what is input on G18.
* After the initial message, nothing is output on TX.

ORIGIN equ $4000       ; arbitrary load address
TX_PORT equ $FF00      ; TurboSim putchar address

DIR_PORT  equ $FF06    ; tfr9's pico-io's GPIO[12:19] Direction Port
DATA_PORT equ $FF07    ; tfr9's pico-io's GPIO[12:19] Data Port

    org ORIGIN

entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print
next_char:
    ldb ,x+         ; get next char, and advance pointer
    beq run         ; if char is 0 (EOS), run the counting
    stb TX_PORT      ; write char to Terminal TX port
    bra next_char        ; go do next char

run:
    ldb #$AA        ; Odd GPIO in 12:19 are outputs.
    stb DIR_PORT    ; set the directions.

loop:
    lda DATA_PORT   ; read inputs
    lsla            ; shift even input bits to odd output bits
    sta DATA_PORT   ; set outputs
    bra loop

message:
    .ascii "Going to Ditto in14 to out15 ..."
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
