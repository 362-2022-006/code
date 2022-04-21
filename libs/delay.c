#include "delay.h"

void delay_us(unsigned int n) {
    delay_ns(1000 * n);
}

void delay_ms(unsigned int n) {
    delay_ns(1000000 * n);
}
