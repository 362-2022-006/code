#include <stm32f0xx.h>

#include "gpu.h"
#include "lcd.h"
#include "types.h"

#define DEFAULT_DMA_BUFFER()                                                                       \
    do {                                                                                           \
        DMA->CMAR = (u32)buffer;                                                                   \
        DMA->CNDTR = 2;                                                                            \
    } while (0)

#define GPU_FIFO_SIZE 8
static GPU_FIFO gpu_fifo[GPU_FIFO_SIZE];
static volatile u8 gpu_fifo_start = 0; // first valid data
static volatile u8 gpu_fifo_end = 0;   // point to insert data

// queues the sprite at "data" to be drawn at "x, y"
// metadata described in "gpu.h" (0 for 16x16 sprite)
void gpu_buffer_add(u16 x, u16 y, const u8 *data, u16 meta) {
    // hang until free space in buffer
    while ((gpu_fifo_end + 1) % GPU_FIFO_SIZE == gpu_fifo_start)
        ;
    // add to buffer
    gpu_fifo[gpu_fifo_end] = (GPU_FIFO){.x = x, .y = y, .data = data, .meta = meta};
    gpu_fifo_end = (gpu_fifo_end + 1) % GPU_FIFO_SIZE;
    // gpu auto disables when buffer empty, reenable when new data inserted
    if (!(DMA->CCR & DMA_CCR_EN))
        // DMA->CCR |= DMA_CCR_EN;
        // DMA1_CH4_5_6_7_DMA2_CH3_4_5_IRQHandler();
        reenable_gpu();
}

// set up screen for sprite display
void init_gpu() {
    init_lcd_spi();
    SPI->CR2 |= SPI_CR2_TXDMAEN;
    init_lcd_dma();

    init_screen(2);
}

void disable_gpu() {
    SPI->CR2 &= ~SPI_CR2_TXDMAEN;
    DMA->CCR &= ~DMA_CCR_EN;
}

void reenable_gpu() {
    clear_lcd_flag(GPU_DISABLE);
    SPI->CR2 |= SPI_CR2_TXDMAEN;
    DMA->CCR |= DMA_CCR_EN;
    DMA1_CH4_5_6_7_DMA2_CH3_4_5_IRQHandler();
}

void DMA1_CH4_5_6_7_DMA2_CH3_4_5_IRQHandler(void) {
    // to allow dma to transfer temporary variables
    static u16 buffer[2];

    // for constant size:
    //     not used
    // for run length:
    //     offset from start of data
    // for variable size:
    //     width (x)
    static u8 cache1;

    // for constant size:
    //     not used
    // for run length:
    //     current row
    // for variable size:
    //     height (y)
    // bonus:
    //     top bit set to indicate invalid cache
    static u8 cache2 = 0x80;

    // coordinates
    // caching these because in the future this is going to be implemented as a fifo
    // and single variable access is faster than arr[elem].val access
    static u16 cachex;
    static u16 cachey;

    // if (check_lcd_flag(TEXT_SENDING)) {
    //     set_lcd_flag(GPU_DISABLE);
    //     disable_gpu();
    //     return;
    // }

    // acknowledge interrupt and disable dma
    DMA2->IFCR |= DMA_IFCR_CTCIF4;
    DMA->CCR &= ~DMA_CCR_EN;
    // RESET_LOW;

    // nothing left in buffer, disable
    if (gpu_fifo_start == gpu_fifo_end || check_lcd_flag(TEXT_SENDING) || check_lcd_flag(GPU_DISABLE)) {
        while (SPI->SR & SPI_SR_BSY)
            ;
        CS_HIGH();
        set_lcd_flag(GPU_DISABLE);
        disable_gpu();
        return;
    }

    // update cache here because we have to wait later anyway
    if (cache2 >> 7) {
        cachex = gpu_fifo[gpu_fifo_start].x;
        cachey = gpu_fifo[gpu_fifo_start].y;
        if (gpu_fifo[gpu_fifo_start].meta >> 14 == 2) {
            // variable width image
            cache1 = (gpu_fifo[gpu_fifo_start].meta >> 8) & 0x3f;
            cache2 = (gpu_fifo[gpu_fifo_start].meta >> 2) & 0x3f;
        } else {
            cache1 = 0;
            cache2 = 0;
        }
    }

    // funky stuff with spi if this isn't here
    // something about requesting more data when transfer buffer reaches half empty
    // then we think dma immediately writes a value on returning from interrupt
    // so we overwrite with 2 values, discarding previous value
    // tldr: have to wait because otherwise spi skips and send out of order
    while (SPI->SR & SPI_SR_BSY)
        ;

    // bad value to force to 8 bit
    SPI->CR2 &= ~SPI_CR2_DS;
    // assert reg select
    DC_LOW();

    switch (gpu_fifo[gpu_fifo_start].meta >> 14) {

    case 0: // constant size (16x16)
        switch (gpu_fifo[gpu_fifo_start].meta & 3) {
        case 0:
            CS_LOW();
            *(u8 *)&SPI->DR = X_CMD;
            buffer[1] = cachex + 15;
            buffer[0] = cachex;
            DEFAULT_DMA_BUFFER();
            break;
        case 1:
            *(u8 *)&SPI->DR = Y_CMD;
            buffer[1] = cachey + 15;
            buffer[0] = cachey;
            DEFAULT_DMA_BUFFER();
            break;
        case 2:
            *(u8 *)&SPI->DR = S_CMD;
            DMA->CMAR = (u32)gpu_fifo[gpu_fifo_start].data;
            DMA->CNDTR = 16 * 16;
            // done sending image
            cache2 |= 0x80; // set cache invalid
            gpu_fifo_start = (gpu_fifo_start + 1) % GPU_FIFO_SIZE;
            goto END_OF_GPU_INTERRUPT;
        default:
            // something went wrong..?
            return;
        }
        gpu_fifo[gpu_fifo_start].meta++;
        break;

    case 1: // row length
        switch (gpu_fifo[gpu_fifo_start].meta & 3) {
        case 0:
            CS_LOW();
            *(u8 *)&SPI->DR = X_CMD;
            // SPI->DR = X_CMD;
            // cache1 is current index of rle file
            // data[ind] is starting pixel
            // data[ind + 1] is length of row
            buffer[0] = cachex + gpu_fifo[gpu_fifo_start].data[cache1];
            buffer[1] = buffer[0] + gpu_fifo[gpu_fifo_start].data[cache1 + 1];
            gpu_fifo[gpu_fifo_start].meta++;
            DEFAULT_DMA_BUFFER();
            break;
        case 1:
            *(u8 *)&SPI->DR = Y_CMD;
            // SPI->DR = Y_CMD;
            buffer[1] = cachey + cache2;
            buffer[0] = cachey + cache2;
            gpu_fifo[gpu_fifo_start].meta++;
            DEFAULT_DMA_BUFFER();
            break;
        case 2:
            *(u8 *)&SPI->DR = S_CMD;
            // SPI->DR = S_CMD;
            // address of first data in row
            DMA->CMAR = (u32)(gpu_fifo[gpu_fifo_start].data + cache1 + 2);
            DMA->CNDTR = gpu_fifo[gpu_fifo_start].data[cache1 + 1];
            // increment starting point to metadata of next row
            cache1 += (gpu_fifo[gpu_fifo_start].data[cache1 + 1] << 1) + 2;
            // increment row
            cache2++;
            gpu_fifo[gpu_fifo_start].meta &= ~0x3;
            if (cache2 >= ((gpu_fifo[gpu_fifo_start].meta >> 2) & 0x3f)) {
                // done sending image
                cache2 |= 0x80; // set cache invalid
                gpu_fifo_start = (gpu_fifo_start + 1) % GPU_FIFO_SIZE;
                goto END_OF_GPU_INTERRUPT;
            }
            break;
        default:
            // something went wrong
            return;
        }
        break;

    case 2: // variable size image
        switch (gpu_fifo[gpu_fifo_start].meta & 3) {
        case 0:
            CS_LOW();
            *(u8 *)&SPI->DR = X_CMD;
            buffer[1] = cachex + cache1 - 1;
            buffer[0] = cachex;
            DEFAULT_DMA_BUFFER();
            break;
        case 1:
            *(u8 *)&SPI->DR = Y_CMD;
            buffer[1] = cachey + cache2 - 1;
            buffer[0] = cachey;
            DEFAULT_DMA_BUFFER();
            break;
        case 2:
            *(u8 *)&SPI->DR = S_CMD;
            DMA->CMAR = (u32)gpu_fifo[gpu_fifo_start].data;
            DMA->CNDTR = cache1 * cache2;
            // done sending image
            cache2 |= 0x80; // set cache invalid
            gpu_fifo_start = (gpu_fifo_start + 1) % GPU_FIFO_SIZE;
            goto END_OF_GPU_INTERRUPT;
        default:
            // something went wrong
            return;
        }
        gpu_fifo[gpu_fifo_start].meta++;
        break;
    }

END_OF_GPU_INTERRUPT:
    // while (SPI->SR & SPI_SR_BSY)
    // ;
    // unassert reg select
    DC_HIGH();
    // back to 16 bit mode
    SPI->CR2 |= SPI_CR2_DS;
    // reenable dma
    DMA->CCR |= DMA_CCR_EN;
}
