#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "random.h"
#include "types.h"

#define FRAMERATE 60

// marker and eraser

// 16x1:
// 10 010000 000001 00
// 0x9004

// 1x16:
// 10 000001 010000 00
// 0x8140

// 8x8:
// 10 001000 001000 00
// 0x8820

// marker moves along direction until it hits a border
// on border, check if direction was input
// if so, move to that border and set direction
// otherwise, just keep moving in same direction
// always on border set current square as used

// eraser moves along direction until it hits a border
// on border, check direction marker was going in next square
// move to back of square
// mark old square as empty

const extern u16 black[];
const extern u16 marker_sprite[];
const extern u16 eraser_sprite[];
const extern u16 apple[];

static int end_game = 0;
static int score = 0;

static void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);
}

typedef enum { LEFT = 1, UP = 2, RIGHT = 3, DOWN = 4, INVALID = 0, APPLE = -1 } DIRECTION;

typedef struct {
    int xpos;
    int ypos;
    int xdist;
    int ydist;
    // 00 --> right
    // 01 --> down
    // 10 --> left
    // 11 --> up
    DIRECTION dir;
    u8 step;
} DRY_ERASE;

static DRY_ERASE marker;
static DRY_ERASE eraser;
static int delay;

DIRECTION board[15][20];

static DIRECTION move_buffer[4] = {INVALID, INVALID, INVALID, INVALID};
static int move_buffer_start;
static int move_buffer_end;

static void add_move_buffer(DIRECTION dir) {
    if ((move_buffer_end + 1) % 4 == move_buffer_start)
        return;
    move_buffer[move_buffer_end] = dir;
    move_buffer_end = (move_buffer_end + 1) % 4;
}

DIRECTION get_next_move() {
    if (move_buffer_start == move_buffer_end)
        return INVALID;
    DIRECTION retval = move_buffer[move_buffer_start];
    move_buffer_start = (move_buffer_start + 1) % 4;
    return retval;
}

static void place_apple() {
    int new_x = get_random() % 15;
    int new_y = get_random() % 20;

    while (board[new_x][new_y]) {
        new_x = get_random() % 15;
        new_y = get_random() % 20;
    }

    board[new_x][new_y] = APPLE;

    gpu_buffer_add(new_x * 16 + 4, new_y * 16 + 4, apple, 0x8820);
}

static void draw_marker() {
    switch (marker.dir) {
    case RIGHT:
        marker.xdist++;
        break;
    case LEFT:
        marker.xdist--;
        break;
    case UP:
        marker.ydist--;
        break;
    case DOWN:
        marker.ydist++;
        break;
    default:
        marker.dir = RIGHT;
        break;
    }
    marker.step++;

    if (marker.step == 16 || marker.step == 19) {
        if (marker.step == 16) {
            DIRECTION next = get_next_move();
            while (next != INVALID && next % 2 == marker.dir % 2)
                next = get_next_move();
            if (next != INVALID)
                marker.dir = next;
        }
        if (board[marker.xpos][marker.ypos] == APPLE) {
            delay += 30;
            place_apple();
            score++;
        }
        board[marker.xpos][marker.ypos] = marker.dir;

        marker.step = 0;

        switch (marker.dir) {
        case LEFT:
            marker.ydist = 0;
            marker.xdist = 18;
            marker.xpos--;
            break;
        case RIGHT:
            marker.ydist = 0;
            marker.xdist = -3;
            marker.xpos++;
            break;
        case UP:
            marker.ydist = 18;
            marker.xdist = 0;
            marker.ypos--;
            break;
        case DOWN:
            marker.ydist = -3;
            marker.xdist = 0;
            marker.ypos++;
            break;
        default:
            break;
        }

        if (marker.xpos == 15)
            marker.xpos = 0;
        else if (marker.xpos == -1)
            marker.xpos = 14;

        if (marker.ypos == 20)
            marker.ypos = 0;
        else if (marker.ypos == -1)
            marker.ypos = 19;

        if (board[marker.xpos][marker.ypos] != INVALID &&
            board[marker.xpos][marker.ypos] != APPLE) {
            end_game = 1;
            unhook_timer();
        }
    }

    gpu_buffer_add(marker.xpos * 16 + marker.xdist, marker.ypos * 16 + marker.ydist, marker_sprite,
                   marker.dir == UP || marker.dir == DOWN ? 0x9004 : 0x8140);
}

static void draw_eraser() {
    switch (eraser.dir) {
    case RIGHT:
        eraser.xdist++;
        break;
    case LEFT:
        eraser.xdist--;
        break;
    case UP:
        eraser.ydist--;
        break;
    case DOWN:
        eraser.ydist++;
        break;
    default:
        eraser.dir = RIGHT;
        break;
    }
    eraser.step++;

    if (eraser.step == 16) {
        int xadd = 0;
        int yadd = 0;
        board[eraser.xpos][eraser.ypos] = INVALID;

        eraser.step = 0;

        switch (eraser.dir) {
        case LEFT:
            eraser.xpos--;
            xadd--;
            break;
        case RIGHT:
            eraser.xpos++;
            xadd++;
            break;
        case UP:
            eraser.ypos--;
            yadd--;
            break;
        case DOWN:
            eraser.ypos++;
            yadd++;
            break;
        default:
            break;
        }

        if (eraser.xpos == 15)
            eraser.xpos = 0;
        else if (eraser.xpos == -1)
            eraser.xpos = 14;

        if (eraser.ypos == 20)
            eraser.ypos = 0;
        else if (eraser.ypos == -1)
            eraser.ypos = 19;

        DIRECTION old_dir = eraser.dir;
        eraser.dir = board[eraser.xpos][eraser.ypos];
        if (old_dir != eraser.dir)
            for (int i = 0; i < 3; i++)
                gpu_buffer_add((eraser.xpos - xadd) * 16 + eraser.xdist + xadd * i,
                               (eraser.ypos - yadd) * 16 + eraser.ydist + yadd * i, eraser_sprite,
                               old_dir == UP || old_dir == DOWN ? 0x9004 : 0x8140);

        switch (eraser.dir) {
        case LEFT:
            eraser.ydist = 0;
            eraser.xdist = 15;
            break;
        case RIGHT:
            eraser.ydist = 0;
            eraser.xdist = 0;
            break;
        case UP:
            eraser.ydist = 15;
            eraser.xdist = 0;
            break;
        case DOWN:
            eraser.ydist = 0;
            eraser.xdist = 0;
            break;
        default:
            break;
        }
    }

    gpu_buffer_add(eraser.xpos * 16 + eraser.xdist, eraser.ypos * 16 + eraser.ydist, eraser_sprite,
                   eraser.dir == UP || eraser.dir == DOWN ? 0x9004 : 0x8140);
}

void draw_frame(void);

void snake_paused(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event()))
        if (event->class == PAUSE_KEY || (event->type == KEY_DOWN && event->class == ESCAPE_KEY))
            hook_timer(FRAMERATE, draw_frame);
}

void draw_frame(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->type == KEY_UP || event->type == KEY_HELD)
            continue;
        else if (event->class == LEFT_ARROW_KEY)
            add_move_buffer(LEFT);
        else if (event->class == RIGHT_ARROW_KEY)
            add_move_buffer(RIGHT);
        else if (event->class == UP_ARROW_KEY)
            add_move_buffer(UP);
        else if (event->class == DOWN_ARROW_KEY)
            add_move_buffer(DOWN);
        else if (event->class == PAUSE_KEY || event->class == ESCAPE_KEY)
            hook_timer(5, snake_paused);
        else if (event->class == ASCII_KEY && event->value == '\t') {
            end_game = 2;
            unhook_timer();
            return;
        }
    }

    draw_marker();
    if (delay)
        delay--;
    else
        draw_eraser();
}

inline static void _wait_for_key_press(void) {
    int state = 0;
    const KeyEvent *event;
    for (int i = 0;; i++) {
        while ((event = get_keyboard_event())) {
            // try to get as much entropy as possible
            mix_random(((u32)event->value << 18) ^ ((u32)event->class << 8) ^ event->type);
            mix_random(i);

            if (event->type == KEY_DOWN)
                state = 1;
            else if (event->type == KEY_UP && state)
                return;
        }
    }
}

int run_snake() {
    init_gpu();
    configure_keyboard();

run_snake_start:
    init_background();
    _wait_for_key_press();

    int start_x = get_random() % 15;
    int start_y = get_random() % 20;

    marker = (DRY_ERASE){.xpos = start_x, .ypos = start_y, .xdist = -3, .ydist = 0, .dir = RIGHT};
    eraser = (DRY_ERASE){.xpos = start_x, .ypos = start_y, .xdist = -3, .ydist = 0, .dir = RIGHT, .step = -3};
    board[start_x][start_y] = RIGHT;
    delay = 60;

    place_apple();

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
        score = 0;
        delay = 60;
        end_game = 0;
        memset(board, INVALID, sizeof board);

        mix_random(0xdecafbad); // totally random

        goto run_snake_start;
    }

    return score;
}
