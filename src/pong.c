#include <stdio.h>
#include <stm32f0xx.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "types.h"

void draw_frame(void);

#define SCREEN_X 240
#define SCREEN_Y 320
#define FRAMERATE 240
#define PADDLE_RATE 1

static int end_game = 0;
int topscore = 0;
int bottomscore = 0;

const extern u16 black[];

// paddle
// 4x40:
// 10 110000 000100 00
// 1011 0000 0001 0000
#define PADDLE_META 0xb010

#define TOP_PADDLE_Y 20
#define BOTTOM_PADDLE_Y (SCREEN_Y - TOP_PADDLE_Y)
#define PADDLE_X_START (SCREEN_X / 2 - PADDLE_WIDTH / 2)

#define PADDLE_THICKNESS 4
#define PADDLE_PADDING 4
#define PADDLE_WIDTH 40

static int top_paddle_x;
static int bottom_paddle_x;

static int top_paddle_vel;
static int bottom_paddle_vel;

const extern u16 paddle_sprite[];

// ball
// 10x10:
// 10 001010 001010 00
// 1000 1010 0010 1000
#define BALL_META 0x8a28

#define BALL_SIZE 8
#define BALL_PADDING 2 // total padding, ie 1px each side --> 2

#define BALL_X_START (SCREEN_X / 2 - BALL_SIZE / 2 - BALL_PADDING / 2)
#define BALL_Y_START (SCREEN_Y / 2 - BALL_SIZE / 2 - BALL_PADDING / 2)
#define BALL_VEL_X_START 0
#define BALL_VEL_Y_START 4
#define BALL_DIR_X_START 0
#define BALL_DIR_Y_START -1

#define MAX_VEL 16

const extern u16 ball_sprite[];
static int ball_x;
static int ball_y;
static int ball_vel_x;
static int ball_vel_y;
static int ball_dir_x;
static int ball_dir_y;
static int hits = 0;

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);
}

static void draw_ball() {
    static int x_state = 0;
    static int y_state = 0;

    if (ball_vel_x > MAX_VEL)
        ball_vel_x = MAX_VEL;
    else if (ball_vel_x < 0)
        ball_vel_x = 0;

    if (ball_vel_y > MAX_VEL)
        ball_vel_y = MAX_VEL;
    else if (ball_vel_y < 0)
        ball_vel_y = 0;

    x_state += ball_vel_x;
    y_state += ball_vel_y;

    if (x_state / MAX_VEL) {
        x_state -= MAX_VEL;
        ball_x += ball_dir_x;

        if (ball_x <= 0 || ball_x >= SCREEN_X - BALL_SIZE - BALL_PADDING)
            ball_dir_x *= -1;
    }

    if (y_state / MAX_VEL) {
        y_state -= MAX_VEL;
        ball_y += ball_dir_y;

        if ((ball_y >= TOP_PADDLE_Y &&
             ball_y <= TOP_PADDLE_Y + PADDLE_THICKNESS &&
             ball_x >= top_paddle_x - BALL_SIZE + BALL_PADDING &&
             ball_x <= top_paddle_x + PADDLE_WIDTH - BALL_PADDING) ||
            (ball_y + BALL_SIZE + BALL_PADDING >= BOTTOM_PADDLE_Y &&
             ball_y + BALL_SIZE + BALL_PADDING <= BOTTOM_PADDLE_Y + PADDLE_THICKNESS &&
             ball_x >= bottom_paddle_x - BALL_SIZE + BALL_PADDING &&
             ball_x <= bottom_paddle_x + PADDLE_WIDTH - BALL_PADDING)) {

            // collision with paddle
            ball_dir_y *= -1;

            int *paddles[2] = {&top_paddle_x, &bottom_paddle_x};
            int idx = ball_y - SCREEN_Y / 2 > 0 ? 1 : 0;

            hits++;
            if (hits % 2) {
                ball_vel_y++;
            }

            if (ball_x - *paddles[idx] < (PADDLE_WIDTH / 8) - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL;
                ball_dir_x = -1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 8) * 2 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 3 * 4;
                ball_dir_x = -1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 8) * 3 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 2;
                ball_dir_x = -1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 2) - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 4;
                ball_dir_x = -1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 8) * 5 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 4;
                ball_dir_x = 1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 8) * 6 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 2;
                ball_dir_x = 1;
            } else if (ball_x - *paddles[idx] <
                       (PADDLE_WIDTH / 8) * 7 - (BALL_SIZE + BALL_PADDING) / 2) {
                ball_vel_x = MAX_VEL / 3 * 4;
                ball_dir_x = 1;
            } else {
                ball_vel_x = MAX_VEL;
                ball_dir_x = 1;
            }

        } else if (ball_y <= 0 || ball_y >= SCREEN_Y - BALL_SIZE - BALL_PADDING) {
            // hit bottom
            // erase ball and paddle
            gpu_buffer_add(ball_x, ball_y, black, 0);
            gpu_buffer_add(top_paddle_x - PADDLE_PADDING, TOP_PADDLE_Y, black, PADDLE_META);
            gpu_buffer_add(bottom_paddle_x - PADDLE_PADDING, BOTTOM_PADDLE_Y, black, PADDLE_META);

            // reset ball and paddle
            ball_x = BALL_X_START;
            ball_y = BALL_Y_START;
            ball_dir_x = BALL_DIR_X_START;
            ball_dir_y = BALL_DIR_Y_START;
            ball_vel_x = BALL_VEL_X_START;
            ball_vel_y = BALL_VEL_Y_START;

            top_paddle_x = PADDLE_X_START;
            bottom_paddle_x = PADDLE_X_START;

            // redraw both
            gpu_buffer_add(top_paddle_x - PADDLE_PADDING, TOP_PADDLE_Y, paddle_sprite, PADDLE_META);
            gpu_buffer_add(bottom_paddle_x - PADDLE_PADDING, BOTTOM_PADDLE_Y, paddle_sprite,
                           PADDLE_META);
            gpu_buffer_add(ball_x, ball_y, ball_sprite, BALL_META);
            unhook_timer();
            // wait_for_move();
            hook_timer(FRAMERATE, draw_frame);
        }
    }
    gpu_buffer_add(ball_x, ball_y, ball_sprite, BALL_META);
}

static void draw_paddle() {
    top_paddle_x += top_paddle_vel;
    if (top_paddle_x < PADDLE_PADDING)
        top_paddle_x = PADDLE_PADDING;
    else if (top_paddle_x > 240 - PADDLE_WIDTH - PADDLE_PADDING)
        top_paddle_x = 240 - PADDLE_WIDTH - PADDLE_PADDING;
    gpu_buffer_add(top_paddle_x - PADDLE_PADDING, TOP_PADDLE_Y, paddle_sprite, PADDLE_META);

    bottom_paddle_x += bottom_paddle_vel;
    if (bottom_paddle_x < PADDLE_PADDING)
        bottom_paddle_x = PADDLE_PADDING;
    else if (bottom_paddle_x > 240 - PADDLE_WIDTH - PADDLE_PADDING)
        bottom_paddle_x = 240 - PADDLE_WIDTH - PADDLE_PADDING;
    gpu_buffer_add(bottom_paddle_x - PADDLE_PADDING, BOTTOM_PADDLE_Y, paddle_sprite, PADDLE_META);
}

void draw_frame(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->type == KEY_HELD)
            continue;
        else if (event->class == LEFT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                top_paddle_vel = -PADDLE_RATE;
            else if (top_paddle_vel < 0)
                top_paddle_vel = 0;
        } else if (event->class == RIGHT_ARROW_KEY) {
            if (event->type == KEY_DOWN)
                top_paddle_vel = PADDLE_RATE;
            else if (top_paddle_vel > 0)
                top_paddle_vel = 0;
        } else if (event->class == ASCII_KEY && event->value == 'a') {
            if (event->type == KEY_DOWN)
                bottom_paddle_vel = -PADDLE_RATE;
            else if (bottom_paddle_vel < 0)
                bottom_paddle_vel = 0;
        } else if (event->class == ASCII_KEY && event->value == 'd') {
            if (event->type == KEY_DOWN)
                bottom_paddle_vel = PADDLE_RATE;
            else if (bottom_paddle_vel > 0)
                bottom_paddle_vel = 0;
        } else if (event->class == ASCII_KEY && event->value == '\t') {
            end_game = 2;
            unhook_timer();
            return;
        }
    }

    draw_ball();
    draw_paddle();
}

int run_pong() {
    init_gpu();
    configure_keyboard();

run_pong_start:
    init_background();

    ball_x = BALL_X_START;
    ball_y = BALL_Y_START;
    ball_dir_x = BALL_DIR_X_START;
    ball_dir_y = BALL_DIR_Y_START;
    ball_vel_x = BALL_VEL_X_START;
    ball_vel_y = BALL_VEL_Y_START;

    top_paddle_x = PADDLE_X_START;
    bottom_paddle_x = PADDLE_X_START;
    top_paddle_vel = 0;
    bottom_paddle_vel = 0;

    draw_paddle();
    draw_ball();

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

        goto run_pong_start;
    }

    return 0;
}
