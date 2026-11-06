.text
.align 2
.global enter_guest
.type enter_guest,%function
enter_guest:
    mov x9, x0
    mov sp, x1
    mov x0, xzr
    mov x1, x2
    br x9
.size enter_guest, .-enter_guest
.align 2
.global enter_thread
.type enter_thread,%function
enter_thread:
    mov x9, x0
    mov sp, x1
    mov x0, x2
    mov x1, x3
    br x9
.size enter_thread, .-enter_thread
.section .note.GNU-stack,"",%progbits
