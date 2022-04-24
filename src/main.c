#include <stdbool.h>
#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "audio.h"
#include "console.h"
#include "random.h"
#include "tetris.h"

int main() {
    // start_console();

    // for (;;) {
    //     update_console();
    // }

    start_audio();

    run_tetris();

    return 0;
}
