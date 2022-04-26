#ifndef SD_H
#define SD_H

#include <inttypes.h>
#include <stdbool.h>

bool init_sd(void);
void close_sd(void);

uint8_t send_cmd_crc(uint8_t cmd, uint32_t arg, uint8_t crc);
uint8_t send_cmd(uint8_t cmd, uint32_t arg);

bool read_sector(uint8_t *buf, uint32_t sector);
bool write_sector(uint8_t *buf, uint32_t sector);
bool read_sector_dma(uint8_t *buf, uint32_t sector, volatile bool *done);

bool read_mbr(uint32_t *partition_start, uint32_t *partition_length);

struct CID {
    uint8_t MID;
    char OID[3];
    char PNM[6];
    uint8_t PRV_H, PRV_L;
    uint32_t PSN;
    uint16_t MDT_y;
    uint8_t MDT_m;
    uint8_t CID_CRC;
};

struct CSD {
    uint8_t TAAC, NSAC, TRAN_SPEED;
    uint16_t CCC;
    uint8_t READ_BL_LEN;
    bool READ_BL_PARTIAL, WRITE_BLK_MISALIGN, READ_BLK_MISALIGN, DSR_IMP;
    uint32_t C_SIZE;
    bool ERASE_BLK_EN;
    uint8_t SECTOR_SIZE, WP_GRP_SIZE;
    bool WP_GRP_ENABLE;
    uint8_t R2W_FACTOR, WRITE_BL_LEN;
    bool WRITE_BL_PARTIAL;
    bool COPY, PERM_WRITE_PROTECT, TMP_WRITE_PROTECT;
    enum { CSD_FMT_PART_TABLE, CSD_FMT_DOS_FAT, CSD_FMT_UNIVERSAL, CSD_FMT_OTHER } FILE_FORMAT;
    uint8_t CSD_CRC;
};

bool read_cid(struct CID *cid);
void print_cid(const struct CID *cid);
bool read_csd(struct CSD *csd);
void print_csd(const struct CSD *csd);

#endif
