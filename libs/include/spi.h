#ifndef SPI_H
#define SPI_H

#include <inttypes.h>
#include <stdbool.h>
#include <stm32f0xx.h>

void init_spi(void);

static inline void wait_for_spi(void) {
    while (SPI1->SR & SPI_SR_BSY)
        ;
}
static inline uint8_t get_spi(void) {
    return *((uint8_t *)&SPI1->DR);
}
static inline void set_spi(uint8_t byte) {
    *((uint8_t *)&SPI1->DR) = byte;
}
static inline void discard_spi_DR(void) {
    while (SPI1->SR & SPI_SR_FRLVL) {
        SPI1->DR;
    }
}

void send_spi(uint8_t byte);
uint8_t receive_spi(void);
void receive_spi_no_wait(void);

void flush_spi(void);
void send_clocks(int n);

void receive_string(char *loc, int length);

#endif
