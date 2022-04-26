#ifndef FAT_H
#define FAT_H

#include <inttypes.h>

#include "types.h"

struct FATParameters {
    uint32_t first_sector;
    uint32_t length_sectors;
    char oem_name[8];
    // --
    uint16_t bytes_per_sector;
    uint16_t reserved_sectors;
    // --
    uint32_t total_volume_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    // --
    uint8_t sectors_per_cluster;
    uint8_t num_fats;
    uint8_t active_fat;
    bool mirroring_enabled;
    // --
    uint16_t fsinfo_sector;
    char volume_name[12];
};

struct FATFile {
    uint32_t start_cluster, next_cluster;
    uint32_t length, length_remaining;
    uint8_t next_sector;
    bool directory;
};

bool init_fat(struct FATParameters *params, uint8_t sd_buffer[512]);

bool find_fat_parameters(struct FATParameters *, uint8_t sd_buffer[512]);
void print_fat_params(const struct FATParameters *);
void print_sector(uint32_t sector, uint8_t sd_buffer[512]);

uint32_t file_get_sector(const struct FATParameters *params, const struct FATFile *file);
int get_file_next_sector(const struct FATParameters *, struct FATFile *, uint8_t buffer[512]);
void reset_file(struct FATFile *);

void open_root(const struct FATParameters *, struct FATFile *);
void ls(const struct FATParameters *, struct FATFile *, uint8_t sd_buffer[512]);
bool open(const char *name, const struct FATParameters *, struct FATFile *dir,
          uint8_t sd_buffer[512]);

void new_file(const struct FATParameters *params, struct FATFile *file, const char *name,
              const char *ext, bool dir, uint8_t sd_buffer[512]);

bool set_size(const char *name, const uint32_t size, const struct FATParameters *params,
              struct FATFile *file, uint8_t sd_buffer[512]);

#endif
