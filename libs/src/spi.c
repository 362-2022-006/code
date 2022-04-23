#include "spi.h"

void init_spi(void) {
    // GPIO
    RCC->AHBENR |= RCC_AHBENR_GPIOAEN;
    GPIOA->MODER &=
        ~(GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7 | GPIO_MODER_MODER15);
    GPIOA->MODER |=
        (GPIO_MODER_MODER5_1 | GPIO_MODER_MODER6_1 | GPIO_MODER_MODER7_1 | GPIO_MODER_MODER15_1);
    GPIOA->AFR[0] &= ~(GPIO_AFRL_AFR5 | GPIO_AFRL_AFR6 | GPIO_AFRL_AFR7);
    GPIOA->AFR[1] &= ~GPIO_AFRH_AFR15;

    // SPI (with NSS)
    RCC->APB2ENR |= RCC_APB2ENR_SPI1EN;
    SPI1->CR1 &= ~SPI_CR1_SPE;                               // turn off SPI
    SPI1->CR1 &= ~(SPI_CR1_BR | SPI_CR1_SSM);                // clear bitrate, NSS management
    SPI1->CR1 |= SPI_CR1_MSTR | SPI_CR1_BR_1 | SPI_CR1_BR_2; // 375 kHz bitrate, master mode
    SPI1->CR2 =
        (SPI1->CR2 & ~(SPI_CR2_DS)) | 0x0700 | SPI_CR2_FRXTH; // 8 bit data size, RXNE at 1/4 full
    SPI1->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE;                 // SS enable
    SPI1->CR1 |= SPI_CR1_SPE;                                 // turn on SPI

    // DMA
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;
    DMA1_Channel2->CCR &= ~DMA_CCR_EN;
    DMA1_Channel2->CCR = DMA_CCR_TCIE | DMA_CCR_MINC;
    DMA1_Channel2->CPAR = (uint32_t)&SPI1->DR;
    DMA1_Channel2->CNDTR = 0;
    NVIC_EnableIRQ(DMA1_Ch2_3_DMA2_Ch1_2_IRQn);
    SPI1->CR2 |= SPI_CR2_RXDMAEN;
}

void send_spi(uint8_t byte) {
    wait_for_spi();
    set_spi(byte);
}

void _set_ss(bool on) {
    wait_for_spi();
    SPI1->CR1 &= ~SPI_CR1_SPE;
    discard_spi_DR();
    if (on) {
        SPI1->CR2 |= SPI_CR2_NSSP | SPI_CR2_SSOE; // SS enable
    } else {
        SPI1->CR2 &= ~(SPI_CR2_NSSP | SPI_CR2_SSOE); // SS disable
    }
    wait_for_spi();
    SPI1->CR1 |= SPI_CR1_SPE;
}

void receive_spi_no_wait(void) {
    wait_for_spi();
    discard_spi_DR();
    set_spi(0xFF);
}

uint8_t receive_spi(void) {
    receive_spi_no_wait();
    wait_for_spi();
    return get_spi();
}

void flush_spi() {
    while (receive_spi() != 0xFF)
        ;
}

void send_clocks(int n) {
    _set_ss(false);

    for (int i = 0; i < n; i++) {
        send_spi(0xFF);
    }

    _set_ss(true);
}

void receive_string(char *loc, int length) {
    int i;
    for (i = 0; i < length; i++) {
        loc[i] = receive_spi();
    }
    loc[i] = '\0';
}
