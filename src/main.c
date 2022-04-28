#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "audio.h"
#include "console.h"
#include "fat.h"
#include "keyboard.h"
#include "sd.h"
#include "text.h"
#include "types.h"

#include "snake.h"
#include "tetris.h"

int main() {
    // start_console(true);

    // for (;;) {
    //     update_console();
    // }

    // start_audio();

    // _wait_for_key();

    run_snake();
    // run_tetris();

    return 0;
}
