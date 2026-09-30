.globl _start
.section .text
_start:
    ldr r0, =post_relocation
    ldr r1, =START_OF_IRAM
    ldr r2, =intermezzo_end
    sub r2, r2, r0
    bl copy_words
    ldr r0, =START_OF_IRAM
    bx r0

.align 4
post_relocation:
    ldr r0, =LOAD_BLOCK_START
    ldr r1, =RELOCATION_TARGET
    ldr r2, =copy_length_word
    ldr r2, [r2]
    bl copy_words
    ldr r0, =RELOCATION_TARGET
    bx r0

.align 4
copy_words:
    ldr r3, [r0], #4
    str r3, [r1], #4
    subs r2, r2, #4
    bne copy_words
    bx lr

.align 4
copy_length_word:
    .word 0x4D52584C

.align 4
.ltorg
intermezzo_end:
