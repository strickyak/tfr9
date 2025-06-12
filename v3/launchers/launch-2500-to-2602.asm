; launch-2500-to-2602.asm
; Produce 256 bytes, [ $2500 : $2600 ).
; Entry at $2500.
; Disable interrupts, start a stack, and jump to $2602.

    ORG $2500

    orcc #$50           ; disable interrupts
    lds #$25F0          ; initial system stack
    jmp $2602           ; Entry to Relocator

    fill '?,$2600-*     ; pad to 256 bytes.

    END $2500
