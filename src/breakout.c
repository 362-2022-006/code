#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "types.h"

#define FRAMERATE 240
#define SCREEN_X 240
#define SCREEN_Y 320

const extern u16 black[];
const extern u16 w_sprite[];
const extern u16 l_sprite[];

static int end_game = 0;

#define STARTING_LIVES 3
static int lives;

// paddle
// 4x40:
// 10 110000 000100 00
// 1011 0000 0001 0000
#define PADDLE_META 0xb010

#define PADDLE_THICKNESS 4
#define PADDLE_PADDING 4
#define PADDLE_WIDTH 40

#define PADDLE_RATE 1

#define PADDLE_Y (SCREEN_Y - 40)
#define PADDLE_X_START (SCREEN_X / 2 - PADDLE_WIDTH / 2)

const extern u16 paddle_sprite[];
static int paddle_x;
static int paddle_vel;

// ball
// 10x10:
// 10 001010 001010 00
// 1000 1010 0010 1000
#define BALL_META 0x8a28

#define BALL_SIZE 8
#define BALL_PADDING 2 // total padding, ie 1px each side --> 2

#define BALL_X_START (SCREEN_X / 2 - BALL_SIZE / 2 - BALL_PADDING / 2)
#define BALL_Y_START (PADDLE_Y - 20)
#define BALL_VEL_X_START 0
#define BALL_VEL_Y_START 1
#define BALL_DIR_X_START 0
#define BALL_DIR_Y_START 1

const extern u16 ball_sprite[];
static int ball_x;
static int ball_y;
static int ball_vel_x;
static int ball_vel_y;
static int ball_dir_x;
static int ball_dir_y;

static int difficulty;

static bool y_updated = false;
static bool x_updated = false;

// brick
// 16x8
// 10 010000 001000 00
// 1001 0000 0010 0000
#define BRICK_META 0x9020

#define BRICK_START 64
#define BRICK_END 104

const extern u16 brick_sprite[];
static u16 bricks[(BRICK_END - BRICK_START) / 8];

// test point
// 1x1
// 10 000010 000010 00
// 1000 0010 0000 1000
#define POINT_META 0x8104
const u16 point[] = {0xffff, 0xffff, 0xffff, 0xffff};

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);

    for (int y = 0; y < sizeof bricks / sizeof bricks[0]; y++)
        for (int x = 0; x < (SCREEN_X - 16) / 16; x++) {
            bricks[y] |= 1 << x;
            gpu_buffer_add((x * 16) + (y % 2) * 8, y * 8 + BRICK_START, brick_sprite, BRICK_META);
        }
}

// hangs until valid paddle movement, applies it
static void wait_for_move() {
    const KeyEvent *event;
    while (true) {
        event = get_keyboard_event();
        if (event->type == KEY_DOWN && event->class == LEFT_ARROW_KEY) {
            paddle_vel = -PADDLE_RATE;
            return;
        } else if (event->type == KEY_DOWN && event->class == RIGHT_ARROW_KEY) {
            paddle_vel = PADDLE_RATE;
            return;
        }
    }
}

// hangs until any input (down and up)
inline static void wait_for_input(void) {
    int state = 0;
    const KeyEvent *event;
    for (int i = 0;; i++)
        while ((event = get_keyboard_event()))
            if (event->type == KEY_DOWN)
                state = 1;
            else if (event->type == KEY_UP && state)
                return;
}

void check_collision(int x_test, int y_test) {
    if ((bricks[y_test] >> x_test) & 1) {
        int y = y_test * 8 + BRICK_START;
        int x = x_test * 16 + y_test % 2 * 8;

        bricks[y_test] &= ~(1 << x_test);
        gpu_buffer_add(x, y, black, BRICK_META);

        if (!x_updated && (ball_x >= x + 16 || ball_x <= x - BALL_SIZE - BALL_PADDING)) {
            x_updated = true;
            ball_dir_x *= -1;
            goto CHECK_COLLISION_WINCON;
        }
        if (!y_updated && (ball_y >= y + 7 || ball_y <= y - BALL_SIZE - BALL_PADDING)) {
            y_updated = true;
            ball_dir_y *= -1;
            goto CHECK_COLLISION_WINCON;
        }
    }

CHECK_COLLISION_WINCON:
    for (int y = 0; y < sizeof bricks / sizeof bricks[0]; y++)
        if (bricks[y])
            return;
    gpu_buffer_add(SCREEN_X / 2 - 8, SCREEN_Y / 2 - 8, w_sprite, 0);
    end_game = 1;
    unhook_timer();
}

static void draw_ball() {
    static int x_state = 0;
    static int y_state = 0;

    if (ball_vel_x > 8)
        ball_vel_x = 8;
    else if (ball_vel_x < 0)
        ball_vel_x = 0;

    if (ball_vel_y > 8)
        ball_vel_y = 8;
    else if (ball_vel_y < 0)
        ball_vel_y = 0;

    x_state += ball_vel_x;
    y_state += ball_vel_y;

    if (x_state >> 3) {
        x_state -= 8;
        ball_x += ball_dir_x;

        if (ball_x <= 0 || ball_x >= SCREEN_X - BALL_SIZE - BALL_PADDING)
            ball_dir_x *= -1;
    }

    if (y_state >> 3) {
        y_state -= 8;
        ball_y += ball_dir_y;

        if (ball_y + BALL_SIZE + BALL_PADDING >= PADDLE_Y &&
            ball_y + BALL_SIZE + BALL_PADDING <= PADDLE_Y + PADDLE_THICKNESS &&
            ball_x >= paddle_x - BALL_SIZE + BALL_PADDING &&
            ball_x <= paddle_x + PADDLE_WIDTH - BALL_PADDING) {

            // collision with paddle
            ball_dir_y *= -1;
            ball_y = PADDLE_Y - BALL_SIZE - BALL_PADDING;

            if (ball_x - paddle_x < (PADDLE_WIDTH / 8) - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 4;
                ball_dir_x = -1;
            } else if (ball_x - paddle_x <
                       (PADDLE_WIDTH / 8) * 2 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 3;
                ball_dir_x = -1;
            } else if (ball_x - paddle_x <
                       (PADDLE_WIDTH / 8) * 3 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 2;
                ball_dir_x = -1;
            } else if (ball_x - paddle_x < (PADDLE_WIDTH / 2) - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 1;
                ball_dir_x = -1;
            } else if (ball_x - paddle_x <
                       (PADDLE_WIDTH / 8) * 5 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 1;
                ball_dir_x = 1;
            } else if (ball_x - paddle_x <
                       (PADDLE_WIDTH / 8) * 6 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 2;
                ball_dir_x = 1;
            } else if (ball_x - paddle_x <
                       (PADDLE_WIDTH / 8) * 7 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = 3;
                ball_dir_x = 1;
            } else {
                ball_vel_x = 4;
                ball_dir_x = 1;
            }

        } else if (ball_y <= 0) {
            // collision with top
            ball_dir_y *= -1;
        } else if (ball_y >= SCREEN_Y - BALL_SIZE - BALL_PADDING) {
            // hit bottom
            // erase ball and paddle
            gpu_buffer_add(ball_x, ball_y, black, 0);
            gpu_buffer_add(paddle_x - PADDLE_PADDING, PADDLE_Y, black, PADDLE_META);

            // reset ball and paddle
            ball_x = BALL_X_START;
            ball_y = BALL_Y_START;
            ball_dir_x = BALL_DIR_X_START;
            ball_dir_y = BALL_DIR_Y_START;
            ball_vel_x = BALL_VEL_X_START;
            ball_vel_y = BALL_VEL_Y_START + difficulty;

            paddle_x = PADDLE_X_START;

            lives--;
            if (lives) {
                // redraw both
                gpu_buffer_add(paddle_x - PADDLE_PADDING, PADDLE_Y, paddle_sprite, PADDLE_META);
                gpu_buffer_add(ball_x, ball_y, ball_sprite, BALL_META);
                wait_for_move();
            } else {
                gpu_buffer_add(SCREEN_X / 2 - 8, SCREEN_Y / 2 - 8, l_sprite, 0);
                end_game = 1;
                unhook_timer();
            }
        } else if (ball_y >= BRICK_START - BALL_SIZE - BALL_PADDING && ball_y < BRICK_END) {
            // collision with bricks?

            int y_test, x_test;

            y_updated = false;
            x_updated = false;

            // test top left
            y_test = (ball_y - BRICK_START) / 8;
            x_test = (ball_x - y_test % 2 * 8) / 16;
            if (y_test < 0)
                goto TEST_COLLISION_3;
            check_collision(x_test, y_test);

            // test top right
            x_test = (ball_x + BALL_SIZE + BALL_PADDING - y_test % 2 * 8) / 16;
            check_collision(x_test, y_test);

        TEST_COLLISION_3:
            // test bottom right
            y_test = (ball_y + BALL_SIZE + BALL_PADDING - BRICK_START) / 8;
            x_test = (ball_x + BALL_SIZE + BALL_PADDING - y_test % 2 * 8) / 16;
            if (y_test >= sizeof bricks / sizeof bricks[0])
                goto TEST_COLLISION_END;
            check_collision(x_test, y_test);

            // test bottom left
            x_test = (ball_x - y_test % 2 * 8) / 16;
            check_collision(x_test, y_test);
        }
    }

TEST_COLLISION_END:
    gpu_buffer_add(ball_x, ball_y, ball_sprite, BALL_META);
}

static void draw_paddle() {
    paddle_x += paddle_vel;
    if (paddle_x < PADDLE_PADDING)
        paddle_x = PADDLE_PADDING;
    else if (paddle_x > 240 - PADDLE_WIDTH - PADDLE_PADDING)
        paddle_x = 240 - PADDLE_WIDTH - PADDLE_PADDING;
    // -2 because there are 2 black pixels on either side
    gpu_buffer_add(paddle_x - PADDLE_PADDING, PADDLE_Y, paddle_sprite, PADDLE_META);
}

void draw_frame(void);

void breakout_paused(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event()))
        if (event->class == PAUSE_KEY || (event->type == KEY_DOWN && event->class == ESCAPE_KEY))
            hook_timer(FRAMERATE, draw_frame);
}

void draw_frame(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->type == KEY_HELD)
            continue;
        else if (event->class == LEFT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                paddle_vel = -PADDLE_RATE;
            else if (paddle_vel < 0)
                paddle_vel = 0;
        } else if (event->class == RIGHT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                paddle_vel = PADDLE_RATE;
            else if (paddle_vel > 0)
                paddle_vel = 0;
        } else if (event->class == PAUSE_KEY ||
                   (event->class == ESCAPE_KEY && event->type == KEY_DOWN))
            hook_timer(5, breakout_paused);
        else if (event->class == ASCII_KEY && event->value == '\t') {
            end_game = 2;
            unhook_timer();
            return;
        }
    }

    draw_paddle();
    draw_ball();
}

int get_difficulty() {
    const KeyEvent *event;
    while (true) {
        event = get_keyboard_event();
        if (event->type == KEY_DOWN && event->class == ASCII_KEY)
            if (event->value >= '0' && event->value <= '9')
                return event->value - '0';
    }
}

int run_breakout() {
    init_gpu();
    configure_keyboard();

run_breakout_start:
    init_background();

    paddle_x = PADDLE_X_START;
    paddle_vel = 0;

    lives = STARTING_LIVES;

    ball_x = BALL_X_START;
    ball_y = BALL_Y_START;
    ball_dir_x = BALL_DIR_X_START;
    ball_dir_y = BALL_DIR_Y_START;
    ball_vel_x = BALL_VEL_X_START;

    draw_frame();
    difficulty = get_difficulty();
    ball_vel_y = BALL_VEL_Y_START + difficulty;
    hook_timer(FRAMERATE, draw_frame);

    while (!end_game)
        asm volatile("wfi");

    const KeyEvent *event;
    while (end_game == 1) {
        event = get_keyboard_event();
        if (event->class == ASCII_KEY && event->value == '\t')
            end_game = 2;
        else if (event->class == ASCII_KEY && event->value == 'q')
            end_game = 3;
        asm volatile("wfi");
    }

    if (end_game == 2) {
        end_game = 0;

        goto run_breakout_start;
    }

    return 0;
}
