#ifndef SD_SPI_H
#define SD_SPI_H

#include <inttypes.h>
#include <stm32f0xx.h>

#include "types.h"

#define SD_SPI SPI2
#define SD_DMA DMA1_Channel4

void init_spi(void);

static inline void wait_for_spi(void) {
    while (SD_SPI->SR & SPI_SR_BSY)
        ;
}
static inline uint8_t get_spi(void) {
    return *((uint8_t *)&SD_SPI->DR);
}
static inline void set_spi(uint8_t byte) {
    *((uint8_t *)&SD_SPI->DR) = byte;
}
static inline void discard_spi_DR(void) {
    while (SD_SPI->SR & SPI_SR_FRLVL) {
        SD_SPI->DR;
    }
}

void send_spi(uint8_t byte);
uint8_t receive_spi(void);
void receive_spi_no_wait(void);

void flush_spi(void);
void send_clocks(int n);

void receive_string(char *loc, int length);

void sd_spi_set_full_speed(void);

#endif
