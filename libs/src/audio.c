#include <math.h>
#include <stm32f0xx.h>
#include <string.h>

#include "types.h"

#define TIME 5
#define CHANNELS 10
#define N 1000
#define RATE 20000
u16 notes[CHANNELS];
int pos[CHANNELS] = {0};
short int wavetable[N];

// N powers of the the 12th-root of 2.
#define STEP1 1.05946309436
#define STEP2 (STEP1 * STEP1)
#define STEP3 (STEP2 * STEP1)
#define STEP4 (STEP3 * STEP1)
#define STEP5 (STEP4 * STEP1)
#define STEP6 (STEP5 * STEP1)
#define STEP7 (STEP6 * STEP1)
#define STEP8 (STEP7 * STEP1)
#define STEP9 (STEP8 * STEP1)

// Macros for computing the fixed point representation of
// the step size used for traversing a wavetable of size N
// at a rate of RATE to produce tones of various doublings
// and halvings of 440 Hz.  (The "A" above middle "C".)
#define A14 ((13.75 * N / RATE) * (1 << 16))      /* A0 */
#define A27 ((27.5 * N / RATE) * (1 << 16))       /* A1 */
#define A55 ((55.0 * N / RATE) * (1 << 16))       /* A2 */
#define A110 ((110.0 * N / RATE) * (1 << 16))     /* A3 */
#define A220 ((220.0 * N / RATE) * (1 << 16))     /* A4 */
#define A440 ((440.0 * N / RATE) * (1 << 16))     /* A5 */
#define A880 ((880.0 * N / RATE) * (1 << 16))     /* A6 */
#define A1760 ((1760.0 * N / RATE) * (1 << 16))   /* A7 */
#define A3520 ((3520.0 * N / RATE) * (1 << 16))   /* A8 */
#define A7040 ((7040.0 * N / RATE) * (1 << 16))   /* A9 */
#define A14080 ((14080.0 * N / RATE) * (1 << 16)) /* A10 */

// A table of steps for each of 128 notes.
// step[60] is the step size for middle C.
// step[69] is the step size for 440 Hz.
const int step[] = {
        A14 / STEP9,    // C                         C-1
        A14 / STEP8,    // C# / Db
        A14 / STEP7,    // D
        A14 / STEP6,    // D# / Eb
        A14 / STEP5,    // E
        A14 / STEP4,    // F
        A14 / STEP3,    // F# / Gb
        A14 / STEP2,    // G
        A14 / STEP1,    // G# / Ab
        A14,            // A14                       A0
        A14 * STEP1,    // A# / Bb
        A14 * STEP2,    // B
        A14 * STEP3,    // C                         C0
        A14 * STEP4,    // C# / Db
        A14 * STEP5,    // D
        A27 * STEP6,    // D# / Eb
        A27 / STEP5,    // E
        A27 / STEP4,    // F
        A27 / STEP3,    // F# / Gb
        A27 / STEP2,    // G
        A27 / STEP1,    // G# / Ab
        A27,            // A27                       A1
        A27 * STEP1,    // A# / Bb
        A27 * STEP2,    // B
        A27 * STEP3,    // C                         C1
        A27 * STEP4,    // C# / Db
        A27 * STEP5,    // D
        A27 * STEP6,    // D# / Eb
        A55 / STEP5,    // E
        A55 / STEP4,    // F
        A55 / STEP3,    // F# / Gb
        A55 / STEP2,    // G
        A55 / STEP1,    // G# / Ab
        A55,            // A55                       A2
        A55 * STEP1,    // A# / Bb
        A55 * STEP2,    // B
        A55 * STEP3,    // C                         C2
        A55 * STEP4,    // C# / Db
        A55 * STEP5,    // D
        A55 * STEP6,    // D# / Eb
        A110 / STEP5,   // E
        A110 / STEP4,   // F
        A110 / STEP3,   // F# / Gb
        A110 / STEP2,   // G
        A110 / STEP1,   // G# / Ab
        A110,           // A110                     A3
        A110 * STEP1,   // A# / Bb
        A110 * STEP2,   // B
        A110 * STEP3,   // C                        C3
        A110 * STEP4,   // C# / Db
        A110 * STEP5,   // D
        A110 * STEP6,   // D# / Eb
        A220 / STEP5,   // E
        A220 / STEP4,   // F
        A220 / STEP3,   // F# / Gb
        A220 / STEP2,   // G
        A220 / STEP1,   // G# / Ab
        A220,           // A220                     A4
        A220 * STEP1,   // A# / Bb
        A220 * STEP2,   // B
        A220 * STEP3,   // C (middle C)             C4 (element #60)
        A220 * STEP4,   // C# / Db
        A220 * STEP5,   // D
        A220 * STEP6,   // D# / Eb
        A440 / STEP5,   // E
        A440 / STEP4,   // F
        A440 / STEP3,   // F# / Gb
        A440 / STEP2,   // G
        A440 / STEP1,   // G# / Ab
        A440,           // A440                     A5
        A440 * STEP1,   // A# / Bb
        A440 * STEP2,   // B
        A440 * STEP3,   // C                        C5
        A440 * STEP4,   // C# / Db
        A440 * STEP5,   // D
        A440 * STEP6,   // D# / Eb
        A880 / STEP5,   // E
        A880 / STEP4,   // F
        A880 / STEP3,   // F# / Gb
        A880 / STEP2,   // G
        A880 / STEP1,   // G# / Ab
        A880,           // A880                     A6
        A880 * STEP1,   // A# / Bb
        A880 * STEP2,   // B
        A880 * STEP3,   // C                        C6
        A880 * STEP4,   // C# / Db
        A880 * STEP5,   // D
        A880 * STEP6,   // D# / Eb
        A1760 / STEP5,  // E
        A1760 / STEP4,  // F
        A1760 / STEP3,  // F# / Gb
        A1760 / STEP2,  // G
        A1760 / STEP1,  // G# / Ab
        A1760,          // A1760                   A7
        A1760 * STEP1,  // A# / Bb
        A1760 * STEP2,  // B
        A1760 * STEP3,  // C                       C7
        A1760 * STEP4,  // C# / Db
        A1760 * STEP5,  // D
        A1760 * STEP6,  // D# / Eb
        A3520 / STEP5,  // E
        A3520 / STEP4,  // F
        A3520 / STEP3,  // F# / Gb
        A3520 / STEP2,  // G
        A3520 / STEP1,  // G# / Ab
        A3520,          // A3520                   A8
        A3520 * STEP1,  // A# / Bb
        A3520 * STEP2,  // B
        A3520 * STEP3,  // C                       C8
        A3520 * STEP4,  // C# / Db
        A3520 * STEP5,  // D
        A3520 * STEP6,  // D# / Eb
        A7040 / STEP5,  // E
        A7040 / STEP4,  // F
        A7040 / STEP3,  // F# / Gb
        A7040 / STEP2,  // G
        A7040 / STEP1,  // G# / Ab
        A7040,          // A7040                   A9
        A7040 * STEP1,  // A# / Bb
        A7040 * STEP2,  // B
        A7040 * STEP3,  // C                       C9
        A7040 * STEP4,  // C# / Db
        A7040 * STEP5,  // D
        A7040 * STEP6,  // D# / Eb
        A14080 / STEP5, // E
        A14080 / STEP4, // F
        A14080 / STEP3, // F# / Gb
        A14080 / STEP2, // G
};

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
    memcpy(&val, test_sound + (index % 378 * 4), 4);
    return val;
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
        for (; i < CHANNELS; i++)
        {
            if (!notes[i]) break;
        }
        notes[i] = (volume << 8) | note;
    } else {
        int i = ch_sect;
        for (; i < CHANNELS / n_channels; i++)
        {
            if ((notes[i] & 0xFF) == note) break;
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
            if (pos[i] > N)
                pos[i] -= N;
            out += (wavetable[pos[i]] * ((notes[i] & 0xff00) >> 8)) >> 8;
            pos[i] += (step[notes[i] & 0xff] >> 16);
        }
    }
    out = (out>>4) + 2048;
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
        dt = parse_command(read_word(idx));
        cur_t += dt;
        idx++;
        if (cur_t >= end_t)
        {
            idx = 4;
            for (int i = 0; i < CHANNELS; i++)
            {
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
    for(x=0; x<N; x++) {
        wavetable[x] = 32767 * sin(2 * M_PI * x / N);
    }
}

void read_header(void) {
    end_t = ((u64)read_word(1) << 32) | read_word(0);
    //u64 channels = ((u64)read_word(3) << 32) | read_word(2);
    //n_channels = channels & 0xff; // may use this for instrument voicing
    n_channels = 1; //testing
    idx = 4;
}

void start_audio(void) {
    read_header();
    init_wavetable_hybrid2();
    init_dac();
    init_tim6();
    init_tim7();
}
