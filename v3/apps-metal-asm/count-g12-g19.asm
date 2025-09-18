    ttl count-g12-g19.asm

* This program runs an 8-bit binary counter
* with output on GPIO[12:19], where G[12] is the
* least significant bit (blinks the fastest)
* and G[19] is the most significant bit
* (blinks the slowest).

ORIGIN equ $4000    ; arbitrary load address
TX_PORT equ $FF00   ; TurboSim putchar address

DIR_PORT  equ $FF06 ; tfr9's pico-io's GPIO[12:19] Direction Port
DATA_PORT equ $FF07 ; tfr9's pico-io's GPIO[12:19] Data Port

    org ORIGIN

entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print
next_char:
    ldb ,x+         ; get next char, and advance pointer
    beq run         ; if char is 0 (EOS), run the counting
    stb TX_PORT     ; write char to Terminal TX port
    bra next_char   ; go do next char

run:
    ldb #$FF        ; all bits `1` indicate output pins
    stb DIR_PORT    ; set the directions to outputs

outer_loop:
    clra            ; use D as 16 bit counter
    clrb
inner_loop:
    sta DATA_PORT   ; write the Most Significant Byte to GPIO pins
    bsr delay ; and delay
    addd #1         ; increment counter
    bne inner_loop  ; unless it rolls over to 0, repeat inner loop.

    ldb #'~         ; if rolls over to 0, print a '~'
    stb TX_PORT     ; putchar
    bra outer_loop  ; and restart count at 0.

delay:
    pshs D
    mul
    mul
    mul
    mul
    mul
    puls D,PC

message:
    .ascii "Going to Count..."
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
