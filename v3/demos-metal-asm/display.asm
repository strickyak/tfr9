    ttl blink.asm

* Blink the builtin LED on the Pi Pico board

ORIGIN equ $4000    ; arbitrary load address
TX_PORT equ $FF00   ; TurboSim putchar address
LED_PORT equ $FF04  ; low bit controls LED

DISPLAY_X_PORT             equ  $FF08
DISPLAY_Y_PORT             equ  $FF09
DISPLAY_COMMAND_PORT       equ  $FF0A

DISPLAY_CLEAR_BUFFER       equ  0
DISPLAY_SET_POINT          equ  1
DISPLAY_SEND_BUFFER        equ  2



    org ORIGIN

    nop             ; unused NOP, to demonstrate `entry` is not `ORIGIN`.
entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print

str_loop:
    ldb ,x+         ; get next char, and advance pointer
    beq run         ; if char is 0 (EOS), go run the blinker.
    stb TX_PORT      ; write char to Terminal TX port
    bra str_loop        ; go do next char

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
run:
    lda #DISPLAY_CLEAR_BUFFER
    sta DISPLAY_COMMAND_PORT

    ldb #60
loop1:
    stb DISPLAY_X_PORT
    lda #4
    sta DISPLAY_Y_PORT
    lda #DISPLAY_SET_POINT
    sta DISPLAY_COMMAND_PORT
    lda #60
    sta DISPLAY_Y_PORT
    lda #DISPLAY_SET_POINT
    sta DISPLAY_COMMAND_PORT

    stb DISPLAY_Y_PORT
    lda #4
    sta DISPLAY_X_PORT
    lda #DISPLAY_SET_POINT
    sta DISPLAY_COMMAND_PORT
    lda #60
    sta DISPLAY_X_PORT
    lda #DISPLAY_SET_POINT
    sta DISPLAY_COMMAND_PORT

    subb #4
    bne loop1

    ldb #DISPLAY_SEND_BUFFER
    stb DISPLAY_COMMAND_PORT

    ldd #$0808
    std DISPLAY_X_PORT
    ldb #'T
    stb DISPLAY_COMMAND_PORT

    ldd #$1808
    std DISPLAY_X_PORT
    ldb #'F
    stb DISPLAY_COMMAND_PORT

    ldd #$0818
    std DISPLAY_X_PORT
    ldb #'R
    stb DISPLAY_COMMAND_PORT

    ldd #$1818
    std DISPLAY_X_PORT
    ldb #'9
    stb DISPLAY_COMMAND_PORT

    ldd #$1919
    std DISPLAY_X_PORT
    ldb #'9
    stb DISPLAY_COMMAND_PORT

    ldb #DISPLAY_SEND_BUFFER
    stb DISPLAY_COMMAND_PORT

stuck: bra stuck



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
    .ascii "Display Demo"
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
