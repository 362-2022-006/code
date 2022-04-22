#include <stdio.h>

int main(void) {
    for (int i = 0; i < 8; i++) {
        printf("\033[%dmHello world\033[m  \033[%dmHello world\033[m\n", i+30, i+40);
        printf("\033[%dmHello world\033[m  \033[%dmHello world\033[m\n", i+90, i+100);
    }

    return 0;
}
