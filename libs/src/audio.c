#include <math.h>
#include <stm32f0xx.h>
#include <string.h>

#include "types.h"

#define N 1024
#define RATE 20000
#include "step.h"

#define TIME 5
#define CHANNELS 8
u16 notes[CHANNELS];
u8 on[CHANNELS];
int fade[CHANNELS];
int pos[CHANNELS] = {0};
short int wavetable[N];

extern u8 test_sound[];

u32 read_word(int index) {
    // reads the word at a certain place in the document
    // code here should utilize the SD card reader and check for chunk completion
    // NOTE: if cur_t == end_t, load from the beginning of the file
    // if (index == 0)
    //     return (u32)1000; // testing code
    // if (index == 1)
    //     return (u32)0; // testing code
    // if (index % 2)
    //     return (u32)0xFF4C00; // testing code
    // else
    //     return (u32)0xFFCC45; // testing code

    u32 val;
    memcpy(&val, test_sound + (index * 4), 4);
    return val;
}

static int n_channels;
static u64 channels;
int parse_command(u32 command) {
    int channel = (command & (0x7 << 28)) >> 28;
    int note_on = command & 0x8000;
    int note = (command & 0x7F00) >> 8;
    int volume = command & 0xFF;
    int ch_sect = channel * (CHANNELS / n_channels);

    if (note_on && (channels & (0xff00<<channel))) {
        int i = ch_sect;
        for (; i < ch_sect + CHANNELS/n_channels; i++) {
            if (!on[i])
                break;
        }
        notes[i] = (volume << 8) | note;
        on[i] = 1;
    } else if (channels & (0xff00<<channel)) {
        int i = ch_sect;
        for (; i < ch_sect + CHANNELS/n_channels; i++) {
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
            out += ((((wavetable[(pos[i]>>16)] * ((notes[i] & 0xff00) >> 8)) >> 8) * fade[i]) >> 5);
            fade[i] = fade[i] + 1 < 32 ? fade[i] + 1 : 32;
        } else if (notes[i]){
            pos[i] += (step[notes[i] & 0xff]);
            if (pos[i] >= N << 16)
                pos[i] -= N << 16;
            out += ((((wavetable[(pos[i]>>16)] * ((notes[i] & 0xff00) >> 8)) >> 8) * fade[i]) >> 5);
            fade[i] = fade[i] - 1 > 0 ? fade[i] - 1 : 0;
            if (fade[i] == 0) notes[i] = 0;
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
        dt = parse_command(read_word(idx));
        cur_t += dt;
        idx++;
        if (cur_t >= end_t) {
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
    TIM7->ARR = (dt * TIME) - 1;
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
    for(x=0; x<N; x++) {
        wavetable[x] = 32767 * sin(2 * M_PI * x / N);
    }
}

void read_header(void) {
    end_t = ((u64)read_word(1) << 32) | read_word(0);
    channels = ((u64)read_word(3) << 32) | read_word(2);
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
