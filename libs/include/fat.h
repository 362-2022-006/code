#ifndef FAT_H
#define FAT_H

#include <inttypes.h>

#include "types.h"

struct FATFile {
    uint32_t start_cluster, next_cluster;
    uint32_t length, length_remaining;
    uint8_t next_sector;
    bool directory;
};

bool init_fat(uint8_t sd_buffer[512]);
void close_fat(void);

void print_fat_params(void);
void print_sector(uint32_t sector, uint8_t sd_buffer[512]);

uint32_t file_get_sector(const struct FATFile *file);
int get_file_next_sector(struct FATFile *, uint8_t buffer[512]);

bool get_file_next_sector_dma(struct FATFile *file, uint8_t buffer[512]);
bool check_dma_read_complete(void);

void reset_file(struct FATFile *);

void open_root(struct FATFile *);
void ls(struct FATFile *, uint8_t sd_buffer[512]);
bool open(const char *name, struct FATFile *dir, uint8_t sd_buffer[512]);

void new_file(struct FATFile *file, const char *name, const char *ext, bool dir,
              uint8_t sd_buffer[512]);

bool set_size(const char *name, const uint32_t size, struct FATFile *file, uint8_t sd_buffer[512]);

#endif
