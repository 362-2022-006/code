#include "stm32f0xx.h"
#include <stdbool.h>
#include <stdio.h>

#include "keyboard.h"

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
            puts("Pressed pause/break");
            ignore_bytes = 7;
            continue;
        } else {
            events[last_event].class = read;
        }

        last_event++;
        last_event %= KEYEVENT_BUFFER_SIZE;
    }
}

char get_shifted_key(char key) {
    if (key == '1' || key == '3' || key == '4' || key == '5') {
        return key - '1' + '!';
    } else if ('a' <= key && key <= 'z') {
        return key - 'a' + 'A';
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

const KeyEvent *get_keyboard_event(void) {
    if (first_event != last_event) {
        KeyEvent *event = &events[first_event++];
        first_event %= KEYEVENT_BUFFER_SIZE;
        return event;
    }
    return NULL;
}
