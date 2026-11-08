.code32
.text
.globl clock_guest_irq
.type clock_guest_irq, @function
clock_guest_irq:
    pusha
    cld
    call clock_guest_on_irq
    popa
    iret
.size clock_guest_irq, .-clock_guest_irq
.section .note.GNU-stack,"",@progbits
