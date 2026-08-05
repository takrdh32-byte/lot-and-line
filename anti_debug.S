// int anti_debug_check(void) -> 1 agar traced, warna 0
// trick: apne upar ptrace(PTRACE_TRACEME) — agar koi tracer laga hai to fail hota hai
.section .note.GNU-stack,"",%progbits
.text
.global anti_debug_check
.type anti_debug_check, %function
anti_debug_check:
    stp     x29, x30, [sp, #-16]!
    mov     x0, #0              // PTRACE_TRACEME
    mov     x1, #0
    mov     x2, #0
    mov     x3, #0
    mov     x8, #117            // __NR_ptrace (aarch64)
    svc     #0
    ldp     x29, x30, [sp], #16
    cbnz    x0, .Ltraced
    mov     w0, #0
    ret
.Ltraced:
    mov     w0, #1
    ret
.size anti_debug_check, .-anti_debug_check