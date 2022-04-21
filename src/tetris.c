#include <stdbool.h>
#include <stdio.h>
#include <stm32f0xx.h>

#include "gpu.h"
#include "hook.h"
#include "keyboard.h"
#include "random.h"
#include "types.h"

void handle_rotation(void (*fn)(int, int));
void draw_next_piece();

const extern u8 white[], black[], red[], green[], yellow[], purple[], orange[], blue[], cyan[];
const u8 *colors[] = {black, red, green, yellow, purple, orange, blue, cyan};

#define BOARD_HEIGHT 20
u32 board[BOARD_HEIGHT];

// piece definitions
const u8 pieces[] = {
    0036, // Z
    0063, // S
    0033, // O
    0072, // T
    0074, // L
    0071, // J
    0170, // I
};

// frames between fall
int rate = 15;
bool fast_fall = false;

int score = 0;
volatile int lose = 0;

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
        gpu_buffer_add(0, ypos, colors[0], 0);
        gpu_buffer_add(16, ypos, colors[0], 0);
        gpu_buffer_add(208, ypos, colors[0], 0);
        gpu_buffer_add(224, ypos, colors[0], 0);
    }
    draw_next_piece();
}

// handle clearing rows
void update_background() {
    for (int i = BOARD_HEIGHT; i >= 0; i--) {
        // iterate through columns
        int clear = 1;
        for (int j = 0; j < 30; j += 3) {
            // iterate through blocks
            if (!((board[i] >> j) & 7)) {
                // if no piece, skip rest of row
                clear = 0;
                break;
            }
        }
        if (clear) {
            // row was full, clear it and move others down
            for (int k = i; k > 0; k--) {
                board[k] = board[k - 1];
            }
            board[0] = 0;
            i++; // check the row that we just cleared again, stuff was moved down
        }
    }
}

typedef struct {
    int x;
    int y;
    u8 piece; // 3x2 grid, bit 6 is reserved for the line
    u8 color;
    u8 rotation;
    u8 next_rotation;
    u8 rotation_style; // 0 --> normal, 1 --> line, 2 --> disabled
} Piece;

Piece current_piece;
Piece next_piece = {
    .x = -2,
    .y = 0,
};

// draws the color of the current piece at the given coordinates
void draw_piece(int xpos, int ypos) {
    gpu_buffer_add((200 - 16) - (xpos * 16), ypos * 16, colors[current_piece.color], 0);
}

// draws the next piece at the top right
void draw_next_piece() {
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            if (x == 3 && y != 1) // this bit only used for line piece
                break;
            else if (x == 3 && y == 1 && !(next_piece.piece >> 6))
                break;
            if ((next_piece.piece >> (3 * y + x)) & 1) {
                gpu_buffer_add((200 - 24) - ((-y - 2) * 16), x * 16, colors[next_piece.color], 0);
            }
        }
    }
}

// draws black at the given coordinates
void erase_piece(int xpos, int ypos) {
    gpu_buffer_add((200 - 16) - (xpos * 16), ypos * 16, colors[0], 0);
}

void get_new_piece() {
    static int idx = 0;

    if (!next_piece.piece) {
        idx = get_random() % 7;
        next_piece = (Piece){
            .piece = pieces[idx],
            .color = idx + 1,
            .rotation = 1,
            .x = -3,
            .y = 0,
        };
    }

    current_piece = (Piece){
        .piece = next_piece.piece,
        .x = 3,
        .y = 0,
        .color = next_piece.color, // color 0 is black
        .rotation = 0,
        .next_rotation = 0,
    };

    idx = get_random() % 7;
    next_piece = (Piece){
        .piece = pieces[idx],
        .color = idx + 1,
    };
}

int invalid_move = 0;
// sets "invalid_move" global variable if the current position is invalid
void check_valid_move(int xpos, int ypos) {
    if ((board[ypos] >> (3 * xpos)) & 7 || !(xpos <= 9 && xpos >= 0) || ypos >= BOARD_HEIGHT) {
        invalid_move = 1;
    }
}

// adds the color of the current piece to the background at the given coordinates
void add_to_background(int xpos, int ypos) {
    board[ypos] &= ~(7 << (3 * xpos));
    board[ypos] |= current_piece.color << (3 * xpos);
}

// iterates over the current piece and passes its current coordinates to the given function
void handle_rotation(void (*fn)(int, int)) {
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            if (x == 3 && y != 1) // this bit only used for line piece
                break;
            else if (x == 3 && y == 1 && !(current_piece.piece >> 6))
                break;
            int xpos, ypos;
            switch (current_piece.rotation) {
            case 0:
                xpos = (current_piece.x + x);
                ypos = (current_piece.y + y);
                break;
            case 1:
                xpos = (current_piece.x + y);
                ypos = (current_piece.y + 2 - x);
                break;
            case 2:
                xpos = (current_piece.x + 2 - x);
                ypos = (current_piece.y + 2 - y);
                break;
            case 3:
            default:
                xpos = (current_piece.x + 2 - y);
                ypos = (current_piece.y + x);
                break;
            }
            if ((current_piece.piece >> (3 * y + x)) & 1) {
                fn(xpos, ypos);
            }
        }
    }
}

// 1 for left, -1 for right
void move_current_piece(int dir) {
    current_piece.x += dir;
    handle_rotation(check_valid_move);
    if (invalid_move) {
        invalid_move = 0;
        current_piece.x -= dir;
    } else {
        current_piece.x -= dir;
        handle_rotation(erase_piece);
        current_piece.x += dir;
        handle_rotation(draw_piece);
    }
}

// rotates current piece (if valid)
void rotate_current_piece() {
    if (current_piece.piece == 0033) {
        return; // square
    }
    int starting = current_piece.rotation;
    current_piece.rotation = (current_piece.rotation + 1) % 4;
    handle_rotation(check_valid_move);
    if (invalid_move) {
        invalid_move = 0;
        current_piece.rotation = starting;
    } else {
        current_piece.next_rotation = current_piece.rotation;
        current_piece.rotation = starting;
        handle_rotation(erase_piece);
        current_piece.rotation = current_piece.next_rotation;
        handle_rotation(draw_piece);
    }
}

void lower_piece() {
    current_piece.y++;
    handle_rotation(check_valid_move);
    if (invalid_move) {
        invalid_move = 0;
        current_piece.y--;
        if (current_piece.y == 0) {
            lose = 1;
            unhook_timer();
        }
        handle_rotation(add_to_background);
        get_new_piece();
        update_background();
        draw_background();
    } else {
        current_piece.y--;
        handle_rotation(erase_piece);
        current_piece.y++;
        handle_rotation(draw_piece);
    }
}

void draw_frame(void) {
    static int state = 0; // frames since last fall

    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->class == DOWN_ARROW_KEY)
            fast_fall = event->type != KEY_UP;
        else if (event->type == KEY_UP)
            continue;
        else if (event->class == LEFT_ARROW_KEY)
            move_current_piece(1);
        else if (event->class == RIGHT_ARROW_KEY)
            move_current_piece(-1);
        else if (event->class == UP_ARROW_KEY)
            rotate_current_piece();
    }

    state += fast_fall ? 3 : 1;
    if (state >= rate) {
        state = 0;
        lower_piece();
    }
}

int run_tetris() {
    init_gpu();
    fill_white();
    configure_keyboard();
    int i = 0;
    while (!get_keyboard_event()) {
        i++;
    }
    mix_random(i);
    hook_timer(30, draw_frame);
    get_new_piece();
    draw_background();
    while (!lose)
        ;
    return score;
}
