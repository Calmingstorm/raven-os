/* i386 SysV cdecl. Offsets match RavenJmpState in libc/include/setjmp.h. */
.code32
.text
.globl setjmp
.type setjmp, @function
setjmp:
    movl 4(%esp), %edx
    movl %ebx, 0(%edx)
    movl %esi, 4(%edx)
    movl %edi, 8(%edx)
    movl %ebp, 12(%edx)
    leal 4(%esp), %ecx
    movl %ecx, 16(%edx)
    movl (%esp), %ecx
    movl %ecx, 20(%edx)
    xorl %eax, %eax
    ret
.size setjmp, .-setjmp

.globl longjmp
.type longjmp, @function
longjmp:
    movl 4(%esp), %edx
    movl 8(%esp), %eax
    testl %eax, %eax
    jnz 1f
    incl %eax
1:
    movl 0(%edx), %ebx
    movl 4(%edx), %esi
    movl 8(%edx), %edi
    movl 12(%edx), %ebp
    movl 16(%edx), %esp
    jmp *20(%edx)
.size longjmp, .-longjmp

.section .note.GNU-stack,"",@progbits
