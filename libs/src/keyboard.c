#include <stdbool.h>
#include <stdio.h>
#include <stm32f0xx.h>

#include "keyboard.h"
#include "types.h"

static const char map[256] = {
    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,    '\t', '`', 0,   0,   0,
    0,    0,   0,   'q',  '1', 0,   0,   0,   'z', 's', 'a',  'w', '2',  0,    0,   'c', 'x', 'd',
    'e',  '4', '3', 0,    0,   ' ', 'v', 'f', 't', 'r', '5',  0,   0,    'n',  'b', 'h', 'g', 'y',
    '6',  0,   0,   0,    'm', 'j', 'u', '7', '8', 0,   0,    ',', 'k',  'i',  'o', '0', '9', 0,
    0,    '.', '/', 'l',  ';', 'p', '-', 0,   0,   0,   '\'', 0,   '[',  '=',  0,   0,   0,   0,
    '\n', ']', 0,   '\\', 0,   0,   0,   0,   0,   0,   0,    0,   '\b', 0,    0,   0,   0,   0,
    0,    0,   0,   0,    0,   0,   0,   0,   0,   0,   0,    0,   0,    '+',  0,   0,   '*'};

static uint8_t pressed[64] = {0};

#define SCANCODE_BUFFER_SIZE 16
static volatile uint8_t scan_values[SCANCODE_BUFFER_SIZE];
static uint8_t last_read = 0;

#define KEYEVENT_BUFFER_SIZE 16
static KeyEvent events[KEYEVENT_BUFFER_SIZE];
static uint8_t first_event = 0;
static uint8_t last_event = 0;

void configure_keyboard(void) {
    // Setup RCC
    RCC->AHBENR |= RCC_AHBENR_GPIOBEN | RCC_AHBENR_GPIOCEN | RCC_AHBENR_DMA2EN;
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;

    // Setup GPIO B
    GPIOB->MODER &= ~GPIO_MODER_MODER0;
    GPIOB->MODER |= GPIO_MODER_MODER0_1;
    GPIOB->AFR[0] &= ~GPIO_AFRL_AFR0;
    GPIOB->AFR[0] |= 4;

    // Setup GPIO C
    GPIOC->MODER &= ~(GPIO_MODER_MODER4 | GPIO_MODER_MODER5);
    GPIOC->MODER |= GPIO_MODER_MODER4_1 | GPIO_MODER_MODER5_1;
    GPIOC->AFR[0] &= ~(GPIO_AFRL_AFR4 | GPIO_AFRL_AFR5);
    GPIOC->AFR[0] |= (1 << 16) | (1 << 20);

    // setup USART3
    USART3->CR1 &= ~USART_CR1_UE;

    USART3->CR1 &= ~USART_CR1_OVER8;
    USART3->CR1 |= USART_CR1_PCE | USART_CR1_PS | USART_CR1_M | USART_CR1_RXNEIE;
    USART3->CR2 |= USART_CR2_CPOL | USART_CR2_LBCL;
    USART3->BRR = 3840;
    USART3->CR3 |= USART_CR3_DMAR;

    USART3->CR2 &= ~USART_CR2_CLKEN;
    // USART3->CR3 |= USART_CR3_HDSEL;

    USART3->CR1 |= USART_CR1_RE | USART_CR1_UE;

    // setup DMA
    DMA2_Channel2->CCR &= ~DMA_CCR_EN;
    DMA2_Channel2->CCR = (DMA2_Channel2->CCR & ~(DMA_CCR_MEM2MEM | DMA_CCR_MSIZE | DMA_CCR_PSIZE |
                                                 DMA_CCR_PINC | DMA_CCR_DIR)) |
                         DMA_CCR_CIRC | DMA_CCR_MINC;
    DMA2_Channel2->CMAR = (uint32_t)scan_values;
    DMA2_Channel2->CPAR = (uint32_t)&USART3->RDR;
    DMA2_Channel2->CNDTR = SCANCODE_BUFFER_SIZE;
    DMA2->RMPCR = (DMA2->RMPCR & ~(0xF << 8)) | DMA_RMPCR2_CH2_USART3_RX;
    DMA2_Channel2->CCR |= DMA_CCR_EN;

    NVIC_EnableIRQ(USART3_8_IRQn);
}

void USART3_4_5_6_7_8_IRQHandler(void) {
    static bool release_flag = false;
    static int ignore_bytes = 0;

    while (last_read != SCANCODE_BUFFER_SIZE - DMA2_Channel2->CNDTR) {
        uint8_t read = scan_values[last_read++];
        last_read %= SCANCODE_BUFFER_SIZE;

        if (ignore_bytes) {
            ignore_bytes--;
            continue;
        }

        if (read == 0xF0) {
            release_flag = true;
            continue;
        }

        events[last_event].type = release_flag                            ? KEY_UP
                                  : pressed[read / 8] & (1 << (read % 8)) ? KEY_HELD
                                                                          : KEY_DOWN;
        if (events[last_event].type == KEY_DOWN) {
            pressed[read / 8] |= 1 << (read % 8);
        } else if (events[last_event].type == KEY_UP) {
            pressed[read / 8] &= ~(1 << (read % 8));
        }
        release_flag = false;

        if (map[read]) {
            events[last_event].class = ASCII_KEY;
            events[last_event].value = map[read];
        } else if (read == 0xE0) {
            // start of multi char
            continue;
        } else if (read == 0xE1) {
            // ignore pause/break
            ignore_bytes = 7;
            events[last_event].class = PAUSE_KEY;
        } else {
            events[last_event].class = read;
        }

        last_event++;
        last_event %= KEYEVENT_BUFFER_SIZE;
    }
}

char get_shifted_key(char key) {
    key = get_caps_lock_key(key);

    if (key == '1' || key == '3' || key == '4' || key == '5') {
        return key - '1' + '!';
    }

    switch (key) {
    case '\'':
        return '"';
    case ',':
        return '<';
    case '-':
        return '_';
    case '.':
        return '>';
    case '/':
        return '?';
    case '2':
        return '@';
    case '6':
        return '^';
    case '7':
        return '&';
    case '8':
        return '*';
    case '9':
        return '(';
    case '0':
        return ')';
    case '=':
        return '+';
    case '[':
        return '{';
    case ']':
        return '}';
    case '\\':
        return '|';
    case '`':
        return '~';
    case ';':
        return ':';
    }

    return key;
}

char get_control_key(char key) {
    if ('a' <= key && key <= 'z') {
        key += 'A' - 'a';
    }
    return key - 'A' + 1;
}

char get_caps_lock_key(char key) {
    if ('a' <= key && key <= 'z') {
        return key - 'a' + 'A';
    } else if ('A' <= key && key <= 'Z') {
        return key - 'A' + 'a';
    }
    return key;
}

static bool shift_down = false;
static bool control_down = false;
static u8 caps_lock_mode = 0;
static u8 insert_mode = 0;

const KeyEvent *get_keyboard_event(void) {
    if (first_event != last_event) {
        const KeyEvent *event = &events[first_event++];
        first_event %= KEYEVENT_BUFFER_SIZE;

        if (event->class == LSHIFT_KEY || event->class == RSHIFT_KEY) {
            shift_down = event->type != KEY_UP;
        } else if (event->class == CONTROL_KEY) {
            control_down = event->type != KEY_UP;
        } else if (event->class == CAPS_LOCK_KEY) {
            caps_lock_mode ^= event->type == KEY_DOWN;
        } else if (event->class == INSERT_KEY) {
            insert_mode ^= event->type == KEY_DOWN;
        }

        return event;
    }
    return NULL;
}

char get_keyboard_character(void) {
    const KeyEvent *event;

    static char char_buffer[2] = {0};
    static u8 buffer_length = 0;

    if (buffer_length) {
        buffer_length--;
        return char_buffer[buffer_length];
    }

    while ((event = get_keyboard_event())) {
        if (event->type != KEY_UP) {
            if (event->class == ASCII_KEY) {
                char c = event->value;
                if (control_down) {
                    c = get_control_key(c);
                } else {
                    if (shift_down) {
                        c = get_shifted_key(c);
                    }
                    if (caps_lock_mode) {
                        c = get_caps_lock_key(c);
                    }
                }
                return c;
            } else if (event->class == ESCAPE_KEY) {
                return '\033';
            } else if (event->class == LEFT_ARROW_KEY) {
                char_buffer[1] = '[';
                char_buffer[0] = 'D';
                buffer_length = 2;
                return '\033';
            } else if (event->class == RIGHT_ARROW_KEY) {
                char_buffer[1] = '[';
                char_buffer[0] = 'C';
                buffer_length = 2;
                return '\033';
            } else if (event->class == UP_ARROW_KEY) {
                char_buffer[1] = '[';
                char_buffer[0] = 'A';
                buffer_length = 2;
                return '\033';
            } else if (event->class == DOWN_ARROW_KEY) {
                char_buffer[1] = '[';
                char_buffer[0] = 'B';
                buffer_length = 2;
                return '\033';
            } else {
                // printf("D: 0x%2x\n", event->class);
            }
        }
    }

    return NULL;
}

bool is_in_insert_mode(void) {
    return insert_mode;
}
