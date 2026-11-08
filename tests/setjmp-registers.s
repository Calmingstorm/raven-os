/* Native Linux i386 executable. No libc and no host setjmp implementation.
 * Exact register, saved-state, ESP and resume-EIP checks for three values.
 */
.code32
.section .bss
.balign 4
env: .skip 24
expected_esp: .skip 4
requested: .skip 4
expected_result: .skip 4
resumed: .skip 4

.text
.globl _start
_start:
    movl $0, requested
    movl $1, expected_result
    call test_one
    movl $0x12345678, requested
    movl $0x12345678, expected_result
    call test_one
    movl $-1234567, requested
    movl $-1234567, expected_result
    call test_one
    movl $0x80000000, requested
    movl $0x80000000, expected_result
    call test_one
    xorl %ebx, %ebx
    movl $1, %eax
    int $0x80

test_one:
    movl $0, resumed
    movl $0x11223344, %ebx
    movl $0x55667788, %esi
    movl $0x99aabbcc, %edi
    movl $0xddeeff01, %ebp
    pushl $env
    movl %esp, expected_esp
    call setjmp
resume_here:
    /* Check live registers before using any for scratch. */
    cmpl $0x11223344, %ebx
    jne fail
    cmpl $0x55667788, %esi
    jne fail
    cmpl $0x99aabbcc, %edi
    jne fail
    cmpl $0xddeeff01, %ebp
    jne fail
    cmpl expected_esp, %esp
    jne fail
    cmpl $0, resumed
    jne check_result
    cmpl $env, (%esp)
    jne fail
    testl %eax, %eax
    jne fail
    cmpl $0x11223344, env
    jne fail
    cmpl $0x55667788, env+4
    jne fail
    cmpl $0x99aabbcc, env+8
    jne fail
    cmpl $0xddeeff01, env+12
    jne fail
    movl expected_esp, %ecx
    cmpl %ecx, env+16
    jne fail
    cmpl $resume_here, env+20
    jne fail
    movl $1, resumed
    addl $4, %esp
    call nested_jump
    jmp fail
check_result:
    cmpl expected_result, %eax
    jne fail
    addl $4, %esp
    ret

nested_jump:
    subl $128, %esp
    movl $0x01020304, %ebx
    movl $0x05060708, %esi
    movl $0x090a0b0c, %edi
    movl $0x0d0e0f10, %ebp
    pushl requested
    pushl $env
    call longjmp
    jmp fail

fail:
    movl $1, %ebx
    movl $1, %eax
    int $0x80
.section .note.GNU-stack,"",@progbits
