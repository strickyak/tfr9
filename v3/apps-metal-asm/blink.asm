    ttl blink.asm

* Blink the builtin LED on the Pi Pico board

ORIGIN equ $4000    ; arbitrary load address
TX_PORT equ $FF00   ; TurboSim putchar address
LED_PORT equ $FF04  ; low bit controls LED

    org ORIGIN

    nop             ; unused NOP, to demonstrate `entry` is not `ORIGIN`.
entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print

loop:
    ldb ,x+         ; get next char, and advance pointer
    beq run         ; if char is 0 (EOS), go run the blinker.
    stb TX_PORT      ; write char to Terminal TX port
    bra loop        ; go do next char

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
run:
    ldx #5000       ; delay duration
    clra            ; low bit is 0
loop@:
    coma            ; invert the low bit
    sta LED_PORT    ; set the LED with the low bit
    bsr delay_x
    bra loop@       ; infinite loop


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

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
message:
    .ascii "Blinking the builtin Pi Pico LED"
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
