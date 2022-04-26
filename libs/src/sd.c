#include "sd.h"

#include <stdio.h>
#include <stm32f0xx.h>
#include <string.h>

#include "sd_spi.h"

// #define DEBUG_SD

uint8_t send_cmd_crc(uint8_t cmd, uint32_t arg, uint8_t crc) {
    flush_spi();

    send_spi(0x40 | cmd); // cmd
    send_spi(arg >> 24);  // arg
    send_spi(arg >> 16);  // arg
    send_spi(arg >> 8);   // arg
    send_spi(arg);        // arg
    send_spi(crc | 0x01); // crc

    uint8_t result;
    int count = 0;
    do {
        result = receive_spi();
    } while (result == 0xFF && count++ < 9);
    return result;
}

uint8_t send_cmd(uint8_t cmd, uint32_t arg) { return send_cmd_crc(cmd, arg, 0); }

bool send_cmd8(void) {
    const static uint32_t arg = 0x000001A5;

    uint8_t read = send_cmd_crc(8, arg, 0x69);
    if (read != 1) {
        printf("CMD8 response error (0x%02x != 0x01)\n", read);
        return true;
    }

    int shift = 32;
    do {
        shift -= 8;
        read = receive_spi();
        if (read != ((uint8_t)(arg >> shift))) {
            printf("CMD8 echo error (0x%02x != 0x%02x)\n", read, ((uint8_t)(arg >> shift)));
            return true;
        }
    } while (shift);

    return false;
}

uint32_t send_cmd58(bool idle) {
    uint8_t read = send_cmd_crc(58, 0, 0xfd);
    if (read != (idle ? 1 : 0)) {
        printf("CMD58 response error (0x%02x)\n", read);
        return 0;
    }

    uint32_t result = 0;
    for (int i = 0; i < 4; i++) {
        result <<= 8;
        result |= receive_spi();
    }

    return result;
}

uint8_t send_acmd(uint8_t cmd, uint32_t arg) {
    if (send_cmd_crc(55, 0x00000000, 0x65) != 0x01) {
        puts("CMD55 response error");
        return true;
    }

    return send_cmd_crc(cmd, arg, 0x77);
}

bool init_sd(void) {
    uint8_t status;
    uint32_t reg;
    int count;

    init_spi();
    send_clocks(11);

    // CMD0 -- go idle (reset)
    count = 0;
    do {
        status = send_cmd_crc(0, 0x00000000, 0x95);
    } while (++count < 250 && status != 0x01);
    if (status != 0x01) {
        puts("CMD0 response error");
        return true;
    } else if (count != 1) {
        printf("CMD0 took %d tries\n", count);
    }

    // CMD8 -- send interface condition
    if (send_cmd8()) {
        return true;
    }

    // CMD58 -- read OCR (for valid operating voltage)
    reg = send_cmd58(true);
    if (!(reg & (1 << 20)) || !(reg & (1 << 21))) {
        puts("Operating voltage error");
    }

    // ACMD41 -- send host capacity support information (also activate initialization)
    count = 1;
    do {
        status = send_acmd(41, 0x40000000);
        if (status == 0)
            break;
    } while (count++ < 250);
    if (status != 0) {
        puts("ACMD41 error");
        return true;
    } else {
#ifdef DEBUG_SD
        printf("ACMD41 success, %d attempts\n", count);
#endif
    }

    // CMD58 -- read OCR (for card capacity status)
    reg = send_cmd58(false);
    if (!(reg & (1 << 31))) {
        puts("Power up routine not finshed? Error.");
        return true;
    }
    if (reg & (1 << 30)) {
#ifdef DEBUG_SD
        puts("High capacity or extended capacity card identified");
#endif
    } else {
        puts("Standard capacity card identified");
        return true;
    }

    return false;
}

void close_sd(void) { send_clocks(2); }

bool read_cid(struct CID *cid) {
    if (send_cmd(10, 0) != 0x00) {
        return true;
    }

    do {
        cid->MID = receive_spi();
    } while ((cid->MID & 0xF0) == 0xF0);

    receive_string(cid->OID, 2);
    receive_string(cid->PNM, 5);

    cid->PRV_H = receive_spi();
    cid->PRV_L = cid->PRV_H & 0xF;
    cid->PRV_H >>= 4;

    cid->PSN = 0;
    for (int i = 0; i < 4; i++) {
        cid->PSN <<= 8;
        cid->PSN |= receive_spi();
    }

    cid->MDT_y = (receive_spi() & 0xF) << 4;
    cid->MDT_m = receive_spi();
    cid->MDT_y |= cid->MDT_m >> 4;
    cid->MDT_m &= 0xF;
    cid->MDT_y += 2000;

    cid->CID_CRC = receive_spi();

    return false;
}

void print_cid(const struct CID *cid) {
    puts("CID");
    printf("\tMID: 0x%02x\n", cid->MID);
    printf("\tOID: %s\n", cid->OID);
    printf("\tPNM: %s\n", cid->PNM);
    printf("\tPNM: %d.%d\n", cid->PRV_H, cid->PRV_L);
    printf("\tPSN: %ld\n", cid->PSN);
    printf("\tMDT: %d.%02d\n", cid->MDT_y, cid->MDT_m);
}

const static uint8_t CSD_TIME_MAP[16] = {0,  10, 12, 13, 15, 20, 25, 30,
                                         35, 40, 45, 50, 55, 60, 70, 80};

bool read_csd(struct CSD *csd) {
    if (send_cmd(9, 0) != 0x00) {
        return true;
    }

    uint8_t read;
    do {
        read = receive_spi();
    } while ((read & 0xF0) == 0xF0);
    if (read >> 6 != 0b01) {
        return true;
    }

    csd->TAAC = receive_spi();
    csd->NSAC = receive_spi();
    csd->TRAN_SPEED = receive_spi();

    csd->CCC = receive_spi() << 4;
    read = receive_spi();
    csd->CCC |= read >> 4;
    csd->READ_BL_LEN = read & 0xF;

    read = receive_spi();
    csd->READ_BL_PARTIAL = read & 0x80;
    csd->WRITE_BLK_MISALIGN = read & 0x40;
    csd->READ_BLK_MISALIGN = read & 0x20;
    csd->DSR_IMP = read & 0x10;

    csd->C_SIZE = (read & 0x3F) << 16;
    csd->C_SIZE |= receive_spi() << 8;
    csd->C_SIZE |= receive_spi();

    read = receive_spi();
    csd->ERASE_BLK_EN = (read & 0b01000000) >> 6;
    csd->SECTOR_SIZE = (read & 0b00111111) << 1;

    read = receive_spi();
    csd->SECTOR_SIZE |= read >> 7;
    csd->WP_GRP_SIZE = read & 0b01111111;

    read = receive_spi();
    csd->WP_GRP_ENABLE = read >> 7;
    csd->R2W_FACTOR = (read & 0b00011100) >> 2;
    csd->WRITE_BL_LEN = (read & 0b00000011) << 2;

    read = receive_spi();
    csd->WRITE_BL_LEN |= read >> 6;
    csd->WRITE_BL_PARTIAL |= (read & 0b00100000) >> 5;

    read = receive_spi();
    csd->COPY = (read & 0b01000000) >> 6;
    csd->PERM_WRITE_PROTECT = (read & 0b00100000) >> 5;
    csd->TMP_WRITE_PROTECT = (read & 0b00010000) >> 4;
    csd->FILE_FORMAT = (read >> 7) | ((read & 0b00001100) >> 2);

    csd->CSD_CRC = receive_spi();

    // FIXME: discard stuff??
    receive_spi();
    receive_spi();
    receive_spi();
    receive_spi();

    return false;
}

void print_csd(const struct CSD *csd) {
    puts("CSD");

    int time_unit = csd->TAAC & 0b111;
    int time = time_unit % 3 == 2 ? 100 : time_unit % 3 == 1 ? 10 : 1;
    time *= CSD_TIME_MAP[(csd->TAAC & 0b0111000) >> 3];
    printf("\tAsynchronous time part: %d.%d", time / 10, time % 10);
    printf(" %s\n", time_unit / 3 == 0 ? "ns" : time_unit / 3 == 1 ? "us" : "ms");

    printf("\tClock-dependent part: %d.%dk clock cycles\n", csd->NSAC / 10, csd->NSAC % 10);

    time_unit = csd->TRAN_SPEED & 0b111;
    time = time_unit % 3 == 0 ? 100 : time_unit % 3 == 1 ? 10 : 1;
    time *= CSD_TIME_MAP[(csd->TRAN_SPEED & 0b0111000) >> 3];
    printf("\tTransfer speed: %d", time);
    printf(" %sbit/s\n", !time_unit ? "k" : (time_unit - 1) / 3 == 0 ? "M" : "G");

    printf("\tCCC: %x\n", csd->CCC);

    int pow = 1;
    for (int i = csd->READ_BL_LEN; i; i--) {
        pow *= 2;
    }
    printf("\tRead block length: %d B\n", pow);

    printf("\tCapacity: %ld MiB\n", (csd->C_SIZE + 1) / 2);
    printf("\tCapacity: %022lx\n", csd->C_SIZE);

    printf("\tFile format: %d\n", csd->FILE_FORMAT);
}

bool read_sector(uint8_t *buf, uint32_t sector) {
    if (send_cmd(17, sector) != 0) {
        puts("CMD17 error");
        return true;
    }

    uint8_t received;
    do {
        received = receive_spi();
    } while (received == 0xFF);

    if (received != 0xFE) {
        puts("Block read error");
        return true;
    }

    for (int i = 0; i < 512; i++) {
        buf[i] = receive_spi();
    }

    // discard CRC
    receive_spi();
    receive_spi();

    return false;
}

bool write_sector(uint8_t *buf, uint32_t sector) {
    if (send_cmd(24, sector) != 0) {
        puts("CMD24 error");
        return true;
    }

    send_spi(0xFE);

    for (int i = 0; i < 512; i++) {
        send_spi(buf[i]);
    }

    uint8_t read;
    do {
        read = receive_spi();
    } while (read == 0xFF);

    if ((read & 0x1F) != 0x05) {
        puts("SD write error");
        return true;
    }

    flush_spi();

    return false;
}

volatile bool *dma_complete_flag;
void DMA1_CH2_3_DMA2_CH1_2_IRQHandler(void) {
    static bool state = 0;
    static uint8_t crc[4];

    DMA1->IFCR |= DMA_IFCR_CTCIF2; // acknowledge interrupt

    if (!state) {
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        DMA1_Channel2->CMAR = (uint32_t)crc;
        DMA1_Channel2->CNDTR = 2;
        DMA1_Channel2->CCR |= DMA_CCR_EN;
    } else {
        *dma_complete_flag = true;
        DMA1_Channel2->CCR &= ~DMA_CCR_EN;
        SD_SPI->CR1 &= ~SPI_CR1_SPE;
        wait_for_spi();
        SD_SPI->CR1 &= ~SPI_CR1_RXONLY;
        SD_SPI->CR1 |= SPI_CR1_SPE;
        discard_spi_DR();
    }

    state ^= 1;
}

bool read_sector_dma(uint8_t *buf, uint32_t sector, volatile bool *done) {
    while (DMA1_Channel2->CCR & DMA_CCR_EN)
        ;

    if (send_cmd(17, sector) != 0) {
        puts("CMD17 error");
        return true;
    }

    receive_spi_no_wait();

    DMA1_Channel2->CMAR = (uint32_t)buf;
    DMA1_Channel2->CNDTR = 512;
    *done = false;
    dma_complete_flag = done;

    wait_for_spi();
    uint8_t received = get_spi();
    while (received == 0xFF) {
        received = receive_spi();
    }

    if (received != 0xFE) {
        puts("Block read error");
        return true;
    }

    SD_SPI->CR1 &= ~SPI_CR1_SPE;
    SD_SPI->CR1 |= SPI_CR1_RXONLY;
    DMA1_Channel2->CCR |= DMA_CCR_EN;
    SD_SPI->CR1 |= SPI_CR1_SPE;

    return false;
}

bool read_mbr(uint32_t *partition_start, uint32_t *partition_length) {
    if (send_cmd(17, 0) != 0) {
        puts("CMD17 Error");
        return true;
    }

    uint8_t received;
    do {
        received = receive_spi();
    } while (received == 0xFF);
    if (received != 0xFE) {
        puts("Block start error");
        goto read_mbr_error;
    }

    for (int i = 0; i < 450; i++) {
        receive_spi(); // discard begininning
    }

    uint8_t part_type = receive_spi();
    if (part_type != 0x0B && part_type != 0x0C) {
        printf("Wrong partition type: 0x%02x\n", part_type);
        goto read_mbr_error;
    }

    receive_spi();
    receive_spi();
    receive_spi();

    uint8_t *write_loc = (uint8_t *)partition_start;
    *(write_loc++) = receive_spi();
    *(write_loc++) = receive_spi();
    *(write_loc++) = receive_spi();
    *write_loc = receive_spi();

    write_loc = (uint8_t *)partition_length;
    *(write_loc++) = receive_spi();
    *(write_loc++) = receive_spi();
    *(write_loc++) = receive_spi();
    *write_loc = receive_spi();

    for (int i = 0; i < 48; i++) {
        receive_spi(); // discard gap
    }

    if (receive_spi() != 0x55 || receive_spi() != 0xAA) {
        puts("MBR signature error");
        goto read_mbr_error;
    }

    // discard CRC
    receive_spi();
    receive_spi();

    return false;

read_mbr_error:
    for (int i = 0; i < 260; i++) {
        receive_spi();
    }
    return true;
}
