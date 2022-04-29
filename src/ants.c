#include <stdio.h>
#include <stdlib.h>
#include <stm32f0xx.h>

#include "gpu.h"
#include "hook.h"
#include "random.h"
#include "types.h"

#define FRAMERATE 60
#define SCREEN_X 240
#define SCREEN_Y 320

#define MAX_ANTS 1024

// 3x3
// 10 000011 000011 00
// 1000 0011 0000 1100
#define ANT_META 0x830c
#define ANT_SIZE 3
// 4x4
// 10 000100 000100 00
// 1000 0100 0001 0000
// #define ANT_META 0x8410
// #define ANT_SIZE 4

typedef struct {
    u16 y;
    u8 x;
    s8 xvel;
    s8 yvel;
    s8 xstate;
    s8 ystate;
    u8 dead;
} ANT;

static ANT *ants;

const extern u16 black[];
const extern u16 ant_sprite[];

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);
}

void init_ants() {
    for (int i = 0; i < MAX_ANTS; i++) {
        ants[i].dead = 1;
    }
}

void add_random_ant(ANT *cur) {
    static u8 state = 0;

    cur->x = get_random();
    cur->y = get_random() & 0x1ff;

    cur->xvel = (get_random() & 0x1f) * (state & 1 ? 1 : -1);
    cur->yvel = (get_random() & 0x1f) * (state & 2 ? 1 : -1);

    // cur->xvel = (s8)(16 - state);
    // cur->yvel = (s8)(16 - state);
    // cur->xvel = 32;
    // cur->yvel = -32;

    cur->dead = 0;

    state++;
}

static void update_ants(void (*spawn_fn)(ANT *)) {
    for (int i = 0; i < MAX_ANTS; i++) {
        ANT *cur = &(ants[i]);

        if (!(cur->dead)) {

            cur->xstate += cur->xvel;
            cur->ystate += cur->yvel;
            if (cur->xstate >= 64 || cur->xstate <= -64) {
                cur->x += (cur->xstate > 0) ? 1 : -1;
                cur->xstate = 0;
            }
            if (cur->ystate >= 64 || cur->ystate <= -64) {
                cur->y += (cur->ystate > 0) ? 1 : -1;
                cur->ystate = 0;
            }

            if ((cur->x) + ANT_SIZE >= SCREEN_X || (cur->y) + ANT_SIZE >= SCREEN_Y) {
                cur->dead = 1;
                gpu_buffer_add(cur->x, cur->y, black, ANT_META);
            } else if (cur->x <= 0) {
                cur->dead = 1;
                gpu_buffer_add(1, cur->y, black, ANT_META);
            } else if (cur->y <= 0) {
                cur->dead = 1;
                gpu_buffer_add(cur->x, 1, black, ANT_META);
            } else {
                gpu_buffer_add(cur->x, cur->y, ant_sprite, ANT_META);
            }
        } else {
            spawn_fn(cur);
        }
    }
}

void draw_frame(void) { update_ants(add_random_ant); }

int run_ants() {
    ants = malloc(MAX_ANTS * sizeof(*ants));

    init_gpu();
    init_background();
    init_ants();
    // hook_timer(FRAMERATE, draw_frame);
    for (;;) {
        update_ants(add_random_ant);
        // asm volatile("wfi");
    }

    free(ants);

    return 0;
}
