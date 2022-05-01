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

#include "ants.h"
#include "breakout.h"
#include "snake.h"
#include "tetris.h"
#include "touhou.h"

#include "internal-clock.h"

int main() {

    internal_clock();

    start_console(true);

    for (;;) {
        update_console();
    }

    // start_audio("audio.nmid");

    // _wait_for_key();

    // run_tetris();
    // run_snake();
    // run_breakout();
    // run_touhou();
    run_ants();

    return 0;
}
