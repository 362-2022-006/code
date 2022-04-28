#include <math.h>
#include <stdlib.h>
#include <stm32f0xx.h>
#include <string.h>

#include "fat.h"
#include "types.h"

#define N 1024
#define RATE 20000
#include "step.h"

#define TIME 5
#define CHANNELS 10
u16 notes[CHANNELS];
int pos[CHANNELS] = {0};
short int wavetable[N];

extern u8 test_sound[];

static u32 *sd_buffer;
static bool sd_buffer_initialized = false;
static struct FATFile sd_file;
static int sd_last_boundary = -128;
static volatile bool sd_done = true;

static void _verify_file_open(void) {
    if (!sd_buffer_initialized) {
        sd_buffer = malloc(512);

        init_fat((u8 *)sd_buffer);
        open_root(&sd_file);
        if (open("audiofilename", &sd_file, (u8 *)sd_buffer)) { // FIXME: file name
            exit(2); // issue
        }
    }
}

static void _synchronous_load_sector(int index) {
    _verify_file_open();
    if (index >= sd_last_boundary + 128) {
        sd_last_boundary += 128;
        get_file_next_sector(&sd_file, (u8 *)sd_buffer);
    }
}

static bool _read_word(u32 *val, int index) {
    // reads the word at a certain place in the document
    // code here should utilize the SD card reader and check for chunk completion

    if (!sd_done) {
        return true;
    }

    _verify_file_open();

    if (index >= sd_last_boundary + 128) {
        sd_last_boundary += 128;
        get_file_next_sector_dma(&sd_file, (u8 *)sd_buffer, &sd_done);
        return true;
    }

    *val = sd_buffer[index - sd_last_boundary];
    return false;
}

int n_channels;
int parse_command(u32 command) {
    int channel = (command & (0x7 << 28)) >> 28;
    int note_on = command & 0x8000;
    int note = (command & 0x7F00) >> 8;
    int volume = command & 0xFF;
    int ch_sect = channel * (CHANNELS / n_channels);

    if (note_on) {
        int i = ch_sect;
        for (; i < CHANNELS; i++) {
            if (!notes[i])
                break;
        }
        notes[i] = (volume << 8) | note;
    } else {
        int i = ch_sect;
        for (; i < CHANNELS / n_channels; i++) {
            if ((notes[i] & 0xFF) == note)
                break;
        }
        notes[i] = 0x0000;
    }
    return (command & 0x1fff0000) >> 16;
}

void TIM6_DAC_IRQHandler() {
    TIM6->SR &= ~TIM_SR_UIF;
    int out = 0;
    for (int i = 0; i < CHANNELS; i++) {
        if (notes[i]) {
            if (pos[i] >= N << 16)
                pos[i] -= N << 16;
            out += (wavetable[(pos[i] >> 16)] * ((notes[i] & 0xff00) >> 8)) >> 8;
            pos[i] += (step[notes[i] & 0xff]);
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
    NVIC_SetPriority(TIM6_DAC_IRQn, 0);
}

u64 cur_t;
u64 end_t;
int idx;
void TIM7_IRQHandler(void) {
    TIM7->SR &= ~TIM_SR_UIF;
    TIM7->CR1 &= ~TIM_CR1_CEN;
    TIM7->CNT = 0;
    int dt = 0;
    while (!dt) {
        u32 val;
        if (_read_word(&val, idx)) {
            // TODO: sector not loaded
            TIM7->ARR = 1; // low value to try again soon, SD load takes ~2 ms
            TIM7->CR1 |= TIM_CR1_CEN;
            return; // FIXME: probably don't just return, but maybe
        }
        dt = parse_command(val);
        cur_t += dt;
        idx++;
        if (cur_t >= end_t) {
            idx = 4;
            for (int i = 0; i < CHANNELS; i++) {
                notes[i] = 0;
                pos[i] = 0;
            }
            cur_t = 0;
        }
    }
    TIM7->ARR = (dt * 5) - 1;
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
    u64 channels = ((u64)sd_buffer[3] << 32) | sd_buffer[2];
    n_channels = channels & 0xff;
    idx = 4;
}

void start_audio(void) {
    read_header();
    init_wavetable_hybrid2();
    init_dac();
    init_tim6();
    init_tim7();
}
