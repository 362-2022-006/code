#include <math.h>
#include <stdlib.h>
#include <stm32f0xx.h>
#include <string.h>

#include "fat.h"
#include "types.h"

#define N 1024
#define RATE 20000
#include "step.h"

#define CHANNELS 8
u16 notes[CHANNELS];
u8 on[CHANNELS];
int fade[CHANNELS];
int pos[CHANNELS] = {0};
short int wavetable[N];

static int time_mult;

static u32 *sd_buffer;
static struct FATFile sd_file;
static int sd_last_boundary = -128;
static volatile bool sd_done = true;

static void _open_file(char *filename) {
    sd_buffer = malloc(512);

    init_fat((u8 *)sd_buffer);
    open_root(&sd_file);
    if (open(filename, &sd_file, (u8 *)sd_buffer)) {
        exit(100002); // issue
    }
}

static void _synchronous_load_sector(int index) {
    if (index >= sd_last_boundary + 128) {
        sd_last_boundary += 128;
        get_file_next_sector(&sd_file, (u8 *)sd_buffer);
    }
}

static bool _read_word(u32 *val, int index) {
    // reads the word at a certain place in the document
    // code here should utilize the SD card reader and check for chunk completion

    // if (!check_dma_read_complete()) {
    //     return true;
    // }

    if (index >= sd_last_boundary + 128) {
        sd_last_boundary += 128;
        if (get_file_next_sector(&sd_file, (u8 *)sd_buffer) < 0) {
            exit(100003);
        }
        return true;
    } else if (index < sd_last_boundary) {
        if (index < 128) {
            sd_last_boundary = 0;
            reset_file(&sd_file);
            if (get_file_next_sector(&sd_file, (u8 *)sd_buffer) < 0) {
                exit(100003);
            }
        } else {
            exit(100004);
        }
    }

    *val = sd_buffer[index - sd_last_boundary];
    return false;
}

static int n_channels;
static u64 channels;
int parse_command(u32 command) {
    int channel = (command & (0x7 << 28)) >> 28;
    int note_on = command & 0x8000;
    int note = (command & 0x7F00) >> 8;
    int volume = command & 0xFF;
    int ch_sect = channel * (CHANNELS / n_channels);

    if (note_on && (channels & (0xff00 << channel))) {
        int i = ch_sect;
        for (; i < ch_sect + CHANNELS / n_channels; i++) {
            if (!on[i])
                break;
        }
        notes[i] = (volume << 8) | note;
        on[i] = 1;
    } else if (channels & (0xff00 << channel)) {
        int i = ch_sect;
        for (; i < ch_sect + CHANNELS / n_channels; i++) {
            if ((notes[i] & 0xFF) == note)
                break;
        }
        on[i] = 0;
    }
    return (command & 0x1fff0000) >> 16;
}

void TIM6_DAC_IRQHandler() {
    TIM6->SR &= ~TIM_SR_UIF;
    int out = 0;
    for (int i = 0; i < CHANNELS; i++) {
        if (on[i]) {
            pos[i] += (step[notes[i] & 0xff]);
            if (pos[i] >= N << 16)
                pos[i] -= N << 16;
            out +=
                ((((wavetable[(pos[i] >> 16)] * ((notes[i] & 0xff00) >> 8)) >> 8) * fade[i]) >> 5);
            fade[i] = fade[i] + 1 < 32 ? fade[i] + 1 : 32;
        } else if (notes[i]) {
            pos[i] += (step[notes[i] & 0xff]);
            if (pos[i] >= N << 16)
                pos[i] -= N << 16;
            out +=
                ((((wavetable[(pos[i] >> 16)] * ((notes[i] & 0xff00) >> 8)) >> 8) * fade[i]) >> 5);
            fade[i] = fade[i] - 1 > 0 ? fade[i] - 1 : 0;
            if (fade[i] == 0)
                notes[i] = 0;
        } else {
            pos[i] = 0;
        }
    }
    out = (out >> 4) + 2048;
    if (out > 4095)
        out = 4095;
    else if (out < 0)
        out = 0;
    DAC->DHR12R1 = out;
}

void init_tim6() {
    int arr_val = 48000000 / (RATE * 100) - 1;
    int psc_val = 100 - 1;
    RCC->APB1ENR |= RCC_APB1ENR_TIM6EN;
    TIM6->ARR = arr_val;
    TIM6->PSC = psc_val;
    TIM6->DIER |= TIM_DIER_UIE;
    TIM6->CR2 |= TIM_CR2_MMS_1;
    TIM6->CR1 |= TIM_CR1_CEN;
    NVIC->ISER[0] |= 1 << TIM6_DAC_IRQn;
    NVIC_SetPriority(TIM6_DAC_IRQn, 3);
}

static u64 cur_t;
static u64 end_t;
static int idx;
void TIM7_IRQHandler(void) {
    TIM7->SR &= ~TIM_SR_UIF;
    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->CNT = 0;
    int dt = 0;
    while (!dt) {
        u32 val;
        if (_read_word(&val, idx)) {
            // sector not loaded yet
            TIM7->ARR = 1; // low value to try again soon, SD load takes ~2 ms
            TIM7->CR1 |= TIM_CR1_CEN;
            return;
        }
        dt = parse_command(val);
        cur_t += dt;
        idx++;
        if (cur_t + 1 >= end_t) {
            idx = 4;
            for (int i = 0; i < CHANNELS; i++) {
                notes[i] = 0;
                pos[i] = 0;
                on[i] = 0;
            }
            cur_t = 0;
            dt = 0;
        }
    }
    TIM7->ARR = (dt * time_mult) - 1;
    TIM7->CR1 |= TIM_CR1_CEN;
}

void init_tim7(void) {
    cur_t = 0;
    RCC->APB1ENR |= RCC_APB1ENR_TIM7EN;
    TIM7->ARR = 5 - 1;
    TIM7->PSC = 48000 - 1;
    TIM7->DIER |= TIM_DIER_UIE;
    TIM7->CR1 |= TIM_CR1_CEN;
    NVIC->ISER[0] |= 1 << TIM7_IRQn;
    NVIC_SetPriority(TIM6_DAC_IRQn, 0);
}

void init_dac(void) {
    RCC->APB1ENR |= RCC_APB1ENR_DACEN;
    DAC->CR |= (DAC_CR_TEN1 | DAC_CR_EN1);
}

void init_wavetable_hybrid2(void) {
    int x;
    for (x = 0; x < N; x++) {
        wavetable[x] = 32767 * sin(2 * M_PI * x / N);
    }
}

void read_header(void) {
    _synchronous_load_sector(0);
    end_t = ((u64)sd_buffer[1] << 32) | sd_buffer[0];
    channels = ((u64)sd_buffer[3] << 32) | sd_buffer[2];
    n_channels = channels & 0xff;
    idx = 4;
}

static void _start(void) {
    read_header();
    init_wavetable_hybrid2();
    init_dac();
    init_tim6();
    init_tim7();
}

void start_audio(char *filename) {
    _open_file(filename);
    time_mult = 5;
    _start();
}

void play_audio(struct FATFile file, int rate) {
    sd_file = file;
    sd_buffer = malloc(512);
    time_mult = rate;
    _start();
}
