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
    (int)(A14 / STEP9) % N,    // C                         C-1
    (int)(A14 / STEP8) % N,    // C# / Db
    (int)(A14 / STEP7) % N,    // D
    (int)(A14 / STEP6) % N,    // D# / Eb
    (int)(A14 / STEP5) % N,    // E
    (int)(A14 / STEP4) % N,    // F
    (int)(A14 / STEP3) % N,    // F# / Gb
    (int)(A14 / STEP2) % N,    // G
    (int)(A14 / STEP1) % N,    // G# / Ab
    (int)(A14) % N,            // A14                       A0
    (int)(A14 *STEP1) % N,     // A# / Bb
    (int)(A14 *STEP2) % N,     // B
    (int)(A14 *STEP3) % N,     // C                         C0
    (int)(A14 *STEP4) % N,     // C# / Db
    (int)(A14 *STEP5) % N,     // D
    (int)(A27 *STEP6) % N,     // D# / Eb
    (int)(A27 / STEP5) % N,    // E
    (int)(A27 / STEP4) % N,    // F
    (int)(A27 / STEP3) % N,    // F# / Gb
    (int)(A27 / STEP2) % N,    // G
    (int)(A27 / STEP1) % N,    // G# / Ab
    (int)(A27) % N,            // A27                       A1
    (int)(A27 *STEP1) % N,     // A# / Bb
    (int)(A27 *STEP2) % N,     // B
    (int)(A27 *STEP3) % N,     // C                         C1
    (int)(A27 *STEP4) % N,     // C# / Db
    (int)(A27 *STEP5) % N,     // D
    (int)(A27 *STEP6) % N,     // D# / Eb
    (int)(A55 / STEP5) % N,    // E
    (int)(A55 / STEP4) % N,    // F
    (int)(A55 / STEP3) % N,    // F# / Gb
    (int)(A55 / STEP2) % N,    // G
    (int)(A55 / STEP1) % N,    // G# / Ab
    (int)(A55) % N,            // A55                       A2
    (int)(A55 *STEP1) % N,     // A# / Bb
    (int)(A55 *STEP2) % N,     // B
    (int)(A55 *STEP3) % N,     // C                         C2
    (int)(A55 *STEP4) % N,     // C# / Db
    (int)(A55 *STEP5) % N,     // D
    (int)(A55 *STEP6) % N,     // D# / Eb
    (int)(A110 / STEP5) % N,   // E
    (int)(A110 / STEP4) % N,   // F
    (int)(A110 / STEP3) % N,   // F# / Gb
    (int)(A110 / STEP2) % N,   // G
    (int)(A110 / STEP1) % N,   // G# / Ab
    (int)(A110) % N,           // A110                     A3
    (int)(A110 *STEP1) % N,    // A# / Bb
    (int)(A110 *STEP2) % N,    // B
    (int)(A110 *STEP3) % N,    // C                        C3
    (int)(A110 *STEP4) % N,    // C# / Db
    (int)(A110 *STEP5) % N,    // D
    (int)(A110 *STEP6) % N,    // D# / Eb
    (int)(A220 / STEP5) % N,   // E
    (int)(A220 / STEP4) % N,   // F
    (int)(A220 / STEP3) % N,   // F# / Gb
    (int)(A220 / STEP2) % N,   // G
    (int)(A220 / STEP1) % N,   // G# / Ab
    (int)(A220) % N,           // A220                     A4
    (int)(A220 *STEP1) % N,    // A# / Bb
    (int)(A220 *STEP2) % N,    // B
    (int)(A220 *STEP3) % N,    // C (middle C)             C4 (element #60)
    (int)(A220 *STEP4) % N,    // C# / Db
    (int)(A220 *STEP5) % N,    // D
    (int)(A220 *STEP6) % N,    // D# / Eb
    (int)(A440 / STEP5) % N,   // E
    (int)(A440 / STEP4) % N,   // F
    (int)(A440 / STEP3) % N,   // F# / Gb
    (int)(A440 / STEP2) % N,   // G
    (int)(A440 / STEP1) % N,   // G# / Ab
    (int)(A440) % N,           // A440                     A5
    (int)(A440 *STEP1) % N,    // A# / Bb
    (int)(A440 *STEP2) % N,    // B
    (int)(A440 *STEP3) % N,    // C                        C5
    (int)(A440 *STEP4) % N,    // C# / Db
    (int)(A440 *STEP5) % N,    // D
    (int)(A440 *STEP6) % N,    // D# / Eb
    (int)(A880 / STEP5) % N,   // E
    (int)(A880 / STEP4) % N,   // F
    (int)(A880 / STEP3) % N,   // F# / Gb
    (int)(A880 / STEP2) % N,   // G
    (int)(A880 / STEP1) % N,   // G# / Ab
    (int)(A880) % N,           // A880                     A6
    (int)(A880 *STEP1) % N,    // A# / Bb
    (int)(A880 *STEP2) % N,    // B
    (int)(A880 *STEP3) % N,    // C                        C6
    (int)(A880 *STEP4) % N,    // C# / Db
    (int)(A880 *STEP5) % N,    // D
    (int)(A880 *STEP6) % N,    // D# / Eb
    (int)(A1760 / STEP5) % N,  // E
    (int)(A1760 / STEP4) % N,  // F
    (int)(A1760 / STEP3) % N,  // F# / Gb
    (int)(A1760 / STEP2) % N,  // G
    (int)(A1760 / STEP1) % N,  // G# / Ab
    (int)(A1760) % N,          // A1760                   A7
    (int)(A1760 *STEP1) % N,   // A# / Bb
    (int)(A1760 *STEP2) % N,   // B
    (int)(A1760 *STEP3) % N,   // C                       C7
    (int)(A1760 *STEP4) % N,   // C# / Db
    (int)(A1760 *STEP5) % N,   // D
    (int)(A1760 *STEP6) % N,   // D# / Eb
    (int)(A3520 / STEP5) % N,  // E
    (int)(A3520 / STEP4) % N,  // F
    (int)(A3520 / STEP3) % N,  // F# / Gb
    (int)(A3520 / STEP2) % N,  // G
    (int)(A3520 / STEP1) % N,  // G# / Ab
    (int)(A3520) % N,          // A3520                   A8
    (int)(A3520 *STEP1) % N,   // A# / Bb
    (int)(A3520 *STEP2) % N,   // B
    (int)(A3520 *STEP3) % N,   // C                       C8
    (int)(A3520 *STEP4) % N,   // C# / Db
    (int)(A3520 *STEP5) % N,   // D
    (int)(A3520 *STEP6) % N,   // D# / Eb
    (int)(A7040 / STEP5) % N,  // E
    (int)(A7040 / STEP4) % N,  // F
    (int)(A7040 / STEP3) % N,  // F# / Gb
    (int)(A7040 / STEP2) % N,  // G
    (int)(A7040 / STEP1) % N,  // G# / Ab
    (int)(A7040) % N,          // A7040                   A9
    (int)(A7040 *STEP1) % N,   // A# / Bb
    (int)(A7040 *STEP2) % N,   // B
    (int)(A7040 *STEP3) % N,   // C                       C9
    (int)(A7040 *STEP4) % N,   // C# / Db
    (int)(A7040 *STEP5) % N,   // D
    (int)(A7040 *STEP6) % N,   // D# / Eb
    (int)(A14080 / STEP5) % N, // E
    (int)(A14080 / STEP4) % N, // F
    (int)(A14080 / STEP3) % N, // F# / Gb
    (int)(A14080 / STEP2) % N, // G
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

int parse_command(u32 command) {
    int channel = (command & (0x7 << 28)) >> 28;
    int note_on = command & 0x8000;
    int note = (command & 0x7F00) >> 8;
    int volume = command & 0xFF;
    int ch_empty = notes[channel * 2] ? channel * 2 + 1 : channel * 2;
    if (note_on) {
        notes[ch_empty] = (volume << 8) | note;
    } else {
        if ((notes[ch_empty ^ 1] & 0xFF) == note)
            notes[ch_empty ^ 1] = 0x0000;
        else
            notes[ch_empty] = 0x0000;
    }
    return (command & 0x1fff0000) >> 16;
}

void TIM6_DAC_IRQHandler() {
    TIM6->SR &= ~TIM_SR_UIF;
    u16 out = 0;
    for (int i = 0; i < CHANNELS; i++) {
        if (notes[i]) {
            pos[i] += step[notes[i] & 0xff];
            if (pos[i] > N)
                pos[i] -= N;
            out += (wavetable[pos[i]] * ((notes[i] & 0xff00) >> 8)) >> 8;
        }
    }
    // DAC->DHR12R1 = out & 0xfff;
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
    for (x = 0; x < N; x++)
        wavetable[x] =
            3 * 8191 * sin(2 * M_PI * x / N) + (double)(8191.0 * (x - N / 2) / ((float)N));
}

u64 end_t;
void read_header(void) {
    end_t = ((u64)read_word(1) << 32) | read_word(0);
    // u64 channels = ((u64)read_word(3) << 32) | read_word(2);
    // int n_channels = channels & 0xff; // may use this for instrument voicing
    idx = 4;
}

void start_audio(void) {
    read_header();
    init_wavetable_hybrid2();
    init_dac();
    init_tim6();
    init_tim7();
}
