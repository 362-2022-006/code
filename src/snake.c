#include <stdbool.h>
#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "lcd.h"
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

// marker moves along direction until it hits a border
// on border, check if direction was input
// if so, move to that border and set direction
// otherwise, just keep moving in same direction
// always on border set current square as used

// eraser moves along direction until it hits a border
// on border, check direction marker was going in next square
// move to back of square
// mark old square as empty

const extern u8 black[];
const extern u8 marker_sprite[];
const extern u8 eraser_sprite[];

int gameover = 0;

void init_background() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, black, 0);
}

typedef enum { LEFT = 0, UP = 1, RIGHT = 2, DOWN = 3, INVALID } DIRECTION;

struct {
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
} marker = {.xpos = 10, .ypos = 10, .xdist = 0, .ydist = 0, .dir = RIGHT};

DIRECTION move_buffer[4] = {INVALID, INVALID, INVALID, INVALID};
int move_buffer_start;
int move_buffer_end;

void add_move_buffer(DIRECTION dir) {
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

void draw_marker() {
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
    case INVALID:
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
        case INVALID:
            break;
        }
    }

    gpu_buffer_add(marker.xpos * 16 + marker.xdist, marker.ypos * 16 + marker.ydist, marker_sprite,
                   marker.dir == UP || marker.dir == DOWN ? 0x9004 : 0x8140);
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
    }

    draw_marker();
}

int run_snake() {
    init_gpu();
    configure_keyboard();

    init_background();

    hook_timer(FRAMERATE, draw_frame);

    while (!gameover)
        asm volatile("wfi");

    return 0;
}
