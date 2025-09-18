    ttl hello.asm

* Print "Hello TurboSim!", a newline character,
* and then go into an infinite loop.

ORIGIN equ $4000    ; arbitrary load address
TXPORT equ $FF00    ; TurboSim putchar address

    org ORIGIN

    nop             ; unused NOP, to demonstrate `entry` is not `ORIGIN`.
entry:
    lds #ORIGIN     ; stack grows downward from origin.
    ldx #message    ; pointer to what to print

loop:
    ldb ,x+         ; get next char, and advance pointer
    beq stuck       ; if char is 0 (EOS), get stuck
    stb TXPORT      ; write char to Terminal TX port
    bra loop        ; go do next char

stuck:
    bra stuck       ; infinite loop (or SHUTDOWN if debug heuristics are enabled)

message:
    .ascii "Hello TurboSim!"
    fcb 10          ; newline char
    fcb 0           ; End Of String (EOS) marker

    end entry       ; entry is the value to put in the RESET vector
