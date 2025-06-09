    ORG $2500

    orcc #$50           ; disable interrupts
    lds #$25F0          ; initial system stack
    jmp $2602           ; Entry to Relocator

    fill '?,$2600-*     ; pad to 256 bytes.

    END $2500
