#include <stm32f0xx.h>
#include <stddef.h>

static void (*hooked_fn_tim2)(void);

// hooks call_fn() into TIM2 at rate Hz
void hook_timer(int rate, void (*call_fn)(void)) {
    hooked_fn_tim2 = call_fn;

    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;
    TIM2->CR1 &= TIM_CR1_CEN;
    TIM2->CR1 |= TIM_CR1_DIR;
    TIM2->PSC = 0;
    TIM2->ARR = (48000000 / rate) - 1;
    TIM2->DIER |= TIM_DIER_UIE;
    NVIC_EnableIRQ(TIM2_IRQn);
    NVIC_SetPriority(TIM2_IRQn, 3);
    TIM2->CR1 |= TIM_CR1_CEN;
}

void unhook_timer(void) {
    hooked_fn_tim2 = NULL;
    TIM2->CR1 &= ~TIM_CR1_CEN;
}

void TIM2_IRQHandler(void) {
    TIM2->SR &= ~TIM_SR_UIF;
    if (hooked_fn_tim2) {
        hooked_fn_tim2();
    }
}
