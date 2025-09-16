    org $4000

entry:
    ldx #message

loop:
    ldb ,x+
    beq stuck
    stb $FF00
    bra loop

stuck:
    bra stuck


message:
    .ascii "Hello TurboSim!"
    fcb 10    ; newline
    fcb 0     ; EOS

; Interrupt and Reset vectors
    org $FFF0
    fdb 0
    fdb 0
    fdb 0
    fdb 0
    fdb 0
    fdb 0
    fdb 0
    fdb entry  ; RESET to entry

    end entry
