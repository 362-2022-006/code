#ifndef SUPPORT_H
#define SUPPORT_H

static inline void delay_ns(unsigned int n) {
    asm("           mov r0,%0\n"
        "repeat%=:  sub r0,#83\n"
        "           bgt repeat%=\n"
        :
        : "r"(n)
        : "r0", "cc");
}

void delay_us(unsigned int n);
void delay_ms(unsigned int n);

#endif
