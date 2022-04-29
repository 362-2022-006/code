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

#define ENEMY_MAX_BULLETS 512
#define PLAYER_MAX_BULLETS 64

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
static int boss_hp;

// rle
// 10 rows
// 01 000000 001010 00
// 0100 0000 0010 1000
// #define PLAYER_META 0x4028
// 8x8
// 10 001000 001000 00
// 1000 1000 0010 0000
#define PLAYER_META 0x8820

#define PLAYER_VEL 32
#define PLAYER_WIDTH 8
#define PLAYER_HEIGHT 8
#define STARTING_HP (SCREEN_X / 5)

const extern u16 player_sprite[];

static int player_x;
static int player_y;
static int player_x_vel;
static int player_y_vel;
static int player_hp;
static bool slow;

// 5x20
// 10 000101 010100 00
// 1000 0101 0101 0000
#define HP_META 0x8550

const extern u16 hp_sprite[];

typedef struct {
    u16 y;
    u8 x;
    s8 xvel;
    s8 yvel;
    s8 xstate;
    s8 ystate;
    u8 dead;
} BULLET;

static BULLET enemy_bullet_list[ENEMY_MAX_BULLETS];
static BULLET player_bullet_list[PLAYER_MAX_BULLETS];

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);

    for (int i = 0; i < STARTING_HP; i++) {
        gpu_buffer_add(i * 5, 0, hp_sprite, HP_META);
        gpu_buffer_add(i * 5, SCREEN_Y, hp_sprite, HP_META);
    }
}

static void init_bullets() {
    for (int i = 0; i < ENEMY_MAX_BULLETS; i++)
        enemy_bullet_list[i].dead = 1;

    for (int i = 0; i < PLAYER_MAX_BULLETS; i++) {
        player_bullet_list[i].dead = 1;
    }
}

static int enemy_bullet_allowance;
static int player_bullet_allowance;

static void handle_enemy_bullets(void (*spawn_fn)(BULLET *)) {
    for (int i = 0; i < ENEMY_MAX_BULLETS; i++) {
        BULLET *cur = &(enemy_bullet_list[i]);

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
                // right or bottom of screen
                cur->dead = 1;
                gpu_buffer_add(cur->x, cur->y, black, BULLET_META);
            } else if (cur->x <= 0) {
                // left of screen
                cur->dead = 1;
                gpu_buffer_add(1, cur->y, black, BULLET_META);
            } else if (cur->y <= 0) {
                // top of screen
                cur->dead = 1;
                gpu_buffer_add(cur->x, 1, black, BULLET_META);
            } else {
                if (cur->x - player_x < 2 && cur->x - player_x > -2 && cur->y - player_y < 2 &&
                    cur->y - player_y > -2) {
                    // hit player
                    player_hp--;
                    gpu_buffer_add(player_hp * 5, SCREEN_Y, black, HP_META);
                    cur->dead = 1;
                    gpu_buffer_add(cur->x, cur->y, black, BULLET_META);
                } else {
                    // didn't hit anything, go right ahead sir
                    gpu_buffer_add(cur->x, cur->y, bullet_sprite, BULLET_META);
                }
            }
        } else if (enemy_bullet_allowance) {
            enemy_bullet_allowance--;
            spawn_fn(cur);
        }
    }
}

static void place_boss_spread(BULLET *cur) {
    static s8 state = 0;

    cur->dead = 0;
    cur->x = boss_x + BOSS_WIDTH / 2 - 2;
    cur->y = boss_y + BOSS_HEIGHT;

    cur->xvel = (4 - state) * 4 * (boss_x_vel > 0 ? -1 : 1);
    cur->yvel = 32;

    state++;
    if (state > 8) {
        state = 0;
    }
}

static void update_boss() {
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

void spawn_player_bullet(BULLET *cur);
void handle_player_bullets(void (*spawn_fn)(BULLET *));

static void update_player() {
    static int x_state = 0;
    static int y_state = 0;

    if (slow) {
        x_state += player_x_vel / 2;
        y_state += player_y_vel / 2;
    } else {
        x_state += player_x_vel;
        y_state += player_y_vel;
    }

    if (x_state >= 32 || x_state <= -32) {
        player_x += x_state > 0 ? 1 : -1;
        x_state = 0;
    }
    if (y_state >= 32 || y_state <= -32) {
        player_y += y_state > 0 ? 1 : -1;
        y_state = 0;
    }

    gpu_buffer_add(player_x, player_y, player_sprite, PLAYER_META);
    handle_player_bullets(spawn_player_bullet);
}

void spawn_player_bullet(BULLET *cur) {
    static s8 state = 0;

    cur->dead = 0;
    cur->x = player_x - 2;
    cur->y = player_y - 6;

    // cur->xvel = (4 - state) * 4 * (boss_x_vel > 0 ? -1 : 1);
    cur->xvel = 0;
    cur->yvel = -32;

    state++;
    if (state > 8) {
        state = 0;
    }
}

void handle_player_bullets(void (*spawn_fn)(BULLET *)) {
    for (int i = 0; i < PLAYER_MAX_BULLETS; i++) {
        BULLET *cur = &(player_bullet_list[i]);

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

            if (cur->x + BULLET_SIZE >= SCREEN_X || cur->y + BULLET_SIZE >= SCREEN_Y ||
                cur->y <= 21 || cur->x <= 1) {
                // side of screen
                cur->dead = 1;
                gpu_buffer_add(cur->x, cur->y, black, BULLET_META);
            } else {
                if (cur->x >= boss_x && cur->x + 2 <= boss_x + BOSS_WIDTH && cur->y >= boss_y &&
                    cur->y <= boss_y + BOSS_HEIGHT) {
                    // hit boss
                    boss_hp--;
                    gpu_buffer_add(boss_hp / 2 * 5, 0, black, HP_META);
                    cur->dead = 1;
                    gpu_buffer_add(cur->x, cur->y, black, BULLET_META);
                } else {
                    // didn't hit anything, go right ahead sir
                    gpu_buffer_add(cur->x, cur->y, bullet_sprite, BULLET_META);
                }
            }
        } else if (player_bullet_allowance) {
            player_bullet_allowance--;
            spawn_fn(cur);
        }
    }
}

void draw_frame(void) {
    static u8 enemy_state = 0;
    static u8 player_state = 0;

    if (enemy_state++) {
        enemy_state = 0;
        enemy_bullet_allowance += 1;
    }

    if (player_state++ == 8) {
        player_state = 0;
        player_bullet_allowance += 2;
    }

    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->type == KEY_HELD) {
            continue;
        } else if (event->class == LSHIFT_KEY || event->class == RSHIFT_KEY) {
            if (event->type == KEY_DOWN)
                slow = true;
            else
                slow = false;
        } else if ((event->class == ASCII_KEY && event->value == 'w') ||
                   event->class == UP_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                player_y_vel = -PLAYER_VEL;
            else if (player_y_vel < 0)
                player_y_vel = 0;
        } else if ((event->class == ASCII_KEY && event->value == 's') ||
                   event->class == DOWN_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                player_y_vel = PLAYER_VEL;
            else if (player_y_vel > 0)
                player_y_vel = 0;
        } else if ((event->class == ASCII_KEY && event->value == 'a') ||
                   event->class == LEFT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                player_x_vel = -PLAYER_VEL;
            else if (player_x_vel < 0)
                player_x_vel = 0;
        } else if ((event->class == ASCII_KEY && event->value == 'd') ||
                   event->class == RIGHT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                player_x_vel = PLAYER_VEL;
            else if (player_x_vel > 0)
                player_x_vel = 0;
        }
    }

    handle_enemy_bullets(place_boss_spread);
    update_player();
    update_boss();

    if (boss_hp <= 0 || player_hp <= 0) {
        end_game = 1;
        unhook_timer();
    }
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
    boss_x = SCREEN_X / 2;
    boss_y = 20;
    boss_x_vel = 1;
    boss_y_vel = 0;
    boss_hp = STARTING_HP * 2;

    player_x = SCREEN_X / 2;
    player_y = SCREEN_Y / 2;
    player_x_vel = 0;
    player_y_vel = 0;
    slow = false;
    player_hp = STARTING_HP;

    enemy_bullet_allowance = 0;
    player_bullet_allowance = 0;

    init_background();
    init_bullets();

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
