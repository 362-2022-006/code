#include <stdio.h>
#include <stm32f0xx.h>
// #include <string.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "lcd.h"
#include "random.h"
#include "types.h"

#define FRAMERATE 60
#define SCREEN_X 240
#define SCREEN_Y (320 - 20)

#define MAX_BULLETS 512

static int end_game = 0;

const extern u16 black[];

// 4x4
// 10 000100 000100 00
// 1000 0100 0001 0000
#define BULLET_META 0x8410
#define BULLET_SIZE 4
// 6x6
// 16 000116 000116 00
// 640 0116 0001 640
// #define BULLET_META 0x8618
// #define BULLET_SIZE 6

const extern u16 bullet_sprite[];

// 32x8
// 10 100000 001000 00
// 1010 0000 0010 0000
#define BOSS_META 0xa020

#define BOSS_WIDTH 32
#define BOSS_HEIGHT 8

const extern u16 boss_sprite[];

static int boss_x;
static int boss_y;

static int boss_x_vel;
static int boss_y_vel;

typedef struct {
    u16 y;
    u8 x;
    s8 xvel;
    s8 yvel;
    s8 xstate;
    s8 ystate;
    u8 dead;
} BULLET;

static BULLET bullet_list[MAX_BULLETS];

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);
}

static void init_bullets() {
    for (int i = 0; i < MAX_BULLETS; i++)
        bullet_list[i].dead = 1;
}

static int bullet_allowance = 32;

static void update_bullets(void (*spawn_fn)(BULLET *)) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        BULLET *cur = &(bullet_list[i]);

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

            if ((cur->x) + BULLET_SIZE >= SCREEN_X || (cur->y) + BULLET_SIZE >= SCREEN_Y) {
                cur->dead = 1;
                gpu_buffer_add(cur->x, cur->y, black, BULLET_META);
            } else if (cur->x <= 0) {
                cur->dead = 1;
                gpu_buffer_add(1, cur->y, black, BULLET_META);
            } else if (cur->y <= 0) {
                cur->dead = 1;
                gpu_buffer_add(cur->x, 1, black, BULLET_META);
            } else {
                gpu_buffer_add(cur->x, cur->y, bullet_sprite, BULLET_META);
            }
        } else if (bullet_allowance) {
            bullet_allowance--;
            spawn_fn(cur);
        }
    }
}

static void place_boss_spread(BULLET *cur) {
    static s8 state = 0;

    cur->dead = 0;
    cur->x = boss_x + BOSS_WIDTH / 2 - 2;
    cur->y = boss_y + BOSS_HEIGHT;

    // cur->vel = 0xf0 + state;
    cur->xvel = (4 - state) * 4 * (boss_x_vel > 0 ? -1 : 1);
    cur->yvel = 32;

    state++;
    if (state > 8) {
        state = 0;
    }
}

void move_boss() {
    boss_x += boss_x_vel;
    boss_y += boss_y_vel;

    if (boss_x <= 0 || boss_x + BOSS_WIDTH >= SCREEN_X) {
        boss_x_vel *= -1;
    }
    if (boss_y <= 0 || boss_y + BOSS_HEIGHT >= SCREEN_Y) {
        boss_y_vel *= -1;
    }

    gpu_buffer_add(boss_x, boss_y, boss_sprite, BOSS_META);
}

void draw_frame(void) {
    static u8 state = 0;

    if (state++) {
        state = 0;
        bullet_allowance += 1;
    }
    // bullet_allowance++;

    update_bullets(place_boss_spread);
    move_boss();
}

/*
static void wait_for_key_press(void) {
    int state = 0;
    const KeyEvent *event;
    for (int i = 0;; i++) {
        while ((event = get_keyboard_event())) {
            // try to get as much entropy as possible
            mix_random(((u32)event->value << 18) ^ ((u32)event->class << 8) ^ event->type);
            mix_random(i);

            if (event->type == KEY_DOWN) {
                state = 1;
            } else if (event->type == KEY_UP && state) {
                return;
            }
        }
    }
}
*/

int run_touhou() {
    init_gpu();
    configure_keyboard();

run_touhou_start:
    init_background();
    init_bullets();

    boss_x = SCREEN_X / 2;
    boss_y = 20;
    boss_x_vel = 1;
    boss_y_vel = 0;

    // wait_for_key_press();

    hook_timer(FRAMERATE, draw_frame);

    while (!end_game)
        asm volatile("wfi");

    const KeyEvent *event;
    while (end_game == 1) {
        while ((event = get_keyboard_event()))
            if (event->class == ASCII_KEY && event->value == '\t')
                end_game = 2;
            else if (event->class == ASCII_KEY && event->value == 'q')
                end_game = 3;
        asm volatile("wfi");
    }

    if (end_game == 2) {
        end_game = 0;

        goto run_touhou_start;
    }

    return 0;
}
