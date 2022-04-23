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

#define FRAMERATE 30

// frames between fall
#define STARTING_RATE 15
int rate = STARTING_RATE;
bool fast_fall = false;

bool animation = false;
u32 rows_to_clear = 0;

int score = 0;
volatile int end_game = 0;

// fills the screen with white, adds black border
void init_background() {
    for (int y = 0; y < 320; y += 16) {

        for (int x = 0; x < 240; x += 16)
            gpu_buffer_add(x, y, white, 0);

        gpu_buffer_add(0, y, colors[0], 0);
        gpu_buffer_add(16, y, colors[0], 0);
        gpu_buffer_add(208, y, colors[0], 0);
        gpu_buffer_add(224, y, colors[0], 0);
    }
}

void print_to_screen(const char *fstring, u32 number) {
    set_lcd_flag(TEXT_SENDING);
    while (!check_lcd_flag(GPU_DISABLE))
        ;
    printf(fstring, number);
    clear_lcd_flag(TEXT_SENDING);
    reenable_gpu();
}

// draws background from "board[]" global variable
void draw_background() {
    // iterate through rows
    for (int i = 0; i < BOARD_HEIGHT; i++) {
        u16 ypos = i * 16; // board[0] is at the top of the screen
        // always 10 tiles per row
        for (int j = 0; j < 10; j++) {
            // msb describes leftmost tile
            // 3 bits of color information per tile
            gpu_buffer_add((200 - 16) - j * 16, ypos, colors[(board[i] >> (j * 3)) & 7], 0);
        }
    }

    print_to_screen("\033[48;2;0;0;0m\033[0;28H Cleared: %d         \n", score);
}

// handle clearing rows
void update_background() {
    for (int i = 0; i < BOARD_HEIGHT; i++) {
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

            // update score
            score++;
            // updated = 1;

            rows_to_clear |= 1 << i;
            animation = true;
        }
    }
    int temp = STARTING_RATE - 2 * score / 10;
    rate = temp > 0 ? temp : 1;
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

void erase_next_piece() {
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            if (x == 3 && y != 1) // this bit only used for line piece
                break;
            else if (x == 3 && y == 1 && !(next_piece.piece >> 6))
                break;
            if ((next_piece.piece >> (3 * y + x)) & 1) {
                gpu_buffer_add((200 - 24) - ((-y - 2) * 16), x * 16, colors[0], 0);
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
    static int newidx = 0;

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

    newidx = get_random() % 7;
    if (newidx == idx) {
        idx = get_random() % 7;
    } else {
        idx = newidx;
    }

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
            end_game = 1;
            unhook_timer();
        }

        handle_rotation(add_to_background);

        erase_next_piece();
        get_new_piece();

        update_background();
        draw_next_piece();
    } else {
        current_piece.y--;
        handle_rotation(erase_piece);
        current_piece.y++;
        handle_rotation(draw_piece);
    }
}

void draw_frame(void);

void tetris_paused(void) {
    const KeyEvent *event;
    while ((event = get_keyboard_event())) {
        if (event->class == PAUSE_KEY) {
            hook_timer(FRAMERATE, draw_frame);
        }
    }
}

void draw_frame(void) {
    static int state = 0; // frames since last fall
    static int animation_state = 4;

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
        else if (event->class == ASCII_KEY && event->value == ' ')
            rotate_current_piece();
        else if (event->class == PAUSE_KEY)
            hook_timer(5, tetris_paused);
        else if (event->class == ASCII_KEY && event->value == '\t') {
            end_game = 2;
            unhook_timer();
            return;
        } else if (event->class == ESCAPE_KEY) {
            end_game = 3;
            unhook_timer();
            return;
        }
    }

    if (animation) {
        for (int i = 0; i < BOARD_HEIGHT; i++) {
            if ((rows_to_clear >> i) & 1) {
                gpu_buffer_add((200 - 16) - (animation_state * 16), i * 16, colors[0], 0);
                gpu_buffer_add((200 - 16) - ((9 - animation_state) * 16), i * 16, colors[0], 0);
            }
        }
        animation_state--;
        if (animation_state < 0) {
            animation_state = 4;
            animation = false;
            draw_background();
            rows_to_clear = 0;
        }
    } else {
        state += fast_fall ? 3 : 1;
        if (state >= rate) {
            state = 0;
            lower_piece();
            print_to_screen("\033[2;0H%%: %d  \n", 100 - (100 * TIM2->CNT) / (TIM2->ARR + 1));
        }
    }
}

inline static void _wait_for_key_press(void) {
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

int run_tetris() {
    init_gpu();
    init_background();
    draw_background();

    configure_keyboard();

run_tetris_start:
    _wait_for_key_press();

    get_new_piece();
    draw_next_piece();

    hook_timer(FRAMERATE, draw_frame);

    while (!end_game)
        asm volatile("wfi");

    const KeyEvent *event;
    while (end_game == 1) {
        while ((event = get_keyboard_event()))
            if (event->class == ASCII_KEY && event->value == '\t')
                end_game = 2;
            else if (event->class == ESCAPE_KEY)
                end_game = 3;
        asm volatile("wfi");
    }

    if (end_game == 2) {
        rate = STARTING_RATE;
        fast_fall = false;
        score = 0;
        end_game = 0;
        memset(board, 0, sizeof board);

        init_gpu();
        init_background();
        draw_background();

        mix_random(0xdecafbad); // totally random

        goto run_tetris_start;
    }

    return score;
}
