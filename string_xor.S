// void decrypt_str(const char* in, char* out, int len, unsigned char key)
// x0=in  x1=out  w2=len  w3=key
.section .note.GNU-stack,"",%progbits
.text
.global decrypt_str
.type decrypt_str, %function
decrypt_str:
    cbz     w2, .Ldone
    mov     w4, w3
.Lloop:
    ldrb    w5, [x0], #1
    eor     w5, w5, w4
    strb    w5, [x1], #1
    subs    w2, w2, #1
    b.ne    .Lloop
.Ldone:
    ret
.size decrypt_str, .-decrypt_str