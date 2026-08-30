*TFR9_PRELUDE equ $FEE0
*
*TFR9_BEGIN_PTR equ $FEF8
*TFR9_END_PTR equ $FEFA
*TFR9_KRN_PTR equ $FEFC
*TFR9_MAGIC_END equ $FEFE
*
*TFR9_MAGIC_VALUE equ $6789

    use defsfile

    ORG TFR9_PRELUDE

    orcc #$50           ; disable interrupts
    lds #$0500          ; initial system stack
    ldd TFR9_KRN_PTR    ; beginning of kernel
    tfr d,x             ; beginning of kernel
    ldx 9,x             ; offset to entry
    jmp d,x             ; jump to offset after beginning of kernel

    FILL 33,TFR9_BEGIN_PTR-.   ; be exactly 24 bytes.

    END TFR9_PRELUDE
