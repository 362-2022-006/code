#include "stm32f0xx.h"
#include "types.h"
#include <stdio.h>

#include "gpu.h"

#include "hook.h"

const extern u8 white[], black[], red[], green[], yellow[], purple[], orange[], blue[], cyan[];
const u8 *colors[] = {black, red, green, yellow, purple, orange, blue, cyan};

u32 board[20];

// fills the screen with white
void fill_white() {
    for (int x = 0; x < 240; x += 16)
        for (int y = 0; y < 320; y += 16)
            gpu_buffer_add(x, y, white, 0);
}

// draws background from "board[]" global variable
void draw_background() {
    // iterate through rows
    for (int i = 0; i < sizeof(board) / sizeof(board[0]); i++) {
        u16 ypos = i * 16; // board[0] is at the top of the screen
        // always 10 tiles per row
        for (int j = 0; j < 10; j++) {
            // msb describes leftmost tile
            // 3 bits of color information per tile
            gpu_buffer_add((200 - 16) - j * 16, ypos, colors[(board[i] >> (j * 3)) & 7], 0);
        }
    }
}

// simple function to demo timer hooking
void demo(void) {
    static u16 xpos = 0;
    static u16 ypos = 0;
    static u8 idx = 0;
    gpu_buffer_add(xpos, ypos, colors[idx], 0);
    xpos += 16;
    if (xpos >= 240) {
        xpos = 0;
        ypos += 16;
    }
    idx = (idx + 1) % (sizeof(colors) / sizeof(colors[0]));
}

struct {
    u8 x;
    u8 y;
    u8 piece; // 3x2 grid, bit 6 is reserved for the line
    u8 color;
    u8 rotation;
    u8 next_rotation;
    u8 rotation_style; // 0 --> normal, 1 --> line, 2 --> disabled
} current_piece = {
    .x = 0,
    .y = 1,
    // .piece = 0074, // L
    .piece = 0071, // J
    // .piece = 0072, // T
    // .piece = 0063, // S
    // .piece = 0036, // Z
    // .piece = 0170, // I
    // .piece = 0033, // O
    .color = 5, // cyan
    .rotation = 0,
    .next_rotation = 0,
    .rotation_style = 0,
};

// frames between fall
int rate = 10;

// draws the piece in "current_piece" global variable
// if "erase" is true, erases instead of drawing
void draw_piece(int erase) {
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            if (x == 3 && y != 1) // this bit only used for line piece
                break;
            else if (x == 3 && y == 1 && !(current_piece.piece >> 6))
                break;
            u16 xpos, ypos;
            switch (current_piece.rotation) {
            case 0:
                xpos = (200 - 16) - (current_piece.x + x) * 16;
                ypos = (current_piece.y + y) * 16;
                break;
            case 1:
                xpos = (200 - 16) - (current_piece.x + y) * 16;
                ypos = (current_piece.y + 2 - x) * 16;
                break;
            case 2:
                xpos = (200 - 16) - (current_piece.x + 2 - x) * 16;
                ypos = (current_piece.y + 2 - y) * 16;
                break;
            case 3:
            default:
                xpos = (200 - 16) - (current_piece.x + 2 - y) * 16;
                ypos = (current_piece.y + x) * 16;
                break;
            }
            if (erase) {
                gpu_buffer_add(xpos, ypos, colors[0], 0);
            } else if ((current_piece.piece >> (3 * y + x)) & 1) {
                gpu_buffer_add(xpos, ypos, colors[current_piece.color], 0);
            }
        }
    }
}

void lower_piece() { current_piece.y++; }

void draw_frame(void) {
    static int state = 0; // frames since last fall

    state++;
    if (state >= rate) {
        draw_piece(1); // erase piece
        state = 0;
        lower_piece();
        draw_piece(0); // draw piece
    } else if (current_piece.rotation != current_piece.next_rotation) {
        draw_piece(1);
        current_piece.rotation = current_piece.next_rotation;
        draw_piece(0);
    }

    // current_piece.next_rotation = (current_piece.rotation + 1) % 4;
}

int run_tetris() {
    init_gpu();
    fill_white();
    draw_background();
    hook_timer(30, draw_frame);
}
