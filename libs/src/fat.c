#include "fat.h"

#include <stdio.h>
#include <string.h>

#include "hexdump.h"
#include "sd.h"
#include "sd_spi.h"

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

static bool is_initialized = false;
static struct FATParameters params;

static bool _load_fat_parameters(uint8_t sd_buffer[512]);

bool init_fat(uint8_t sd_buffer[512]) {
    if (is_initialized)
        return false;

    if (init_sd()) {
        puts("Error initializing SD card");
        return true;
    }

    if (_load_fat_parameters(sd_buffer)) {
        puts("Could not find FAT parameters");
        return true;
    }

    is_initialized = true;
    return false;
}

void close_fat(void) {
    is_initialized = false;
    close_sd();
}

static bool _load_fat_parameters(uint8_t sd_buffer[512]) {
    if (read_mbr(&params.first_sector, &params.length_sectors)) {
        puts("MBR read error");
        return true;
    }

    if (read_sector(sd_buffer, params.first_sector)) {
        puts("Failed to read FAT first sector");
        return true;
    }
    if (sd_buffer[510] != 0x55 || sd_buffer[511] != 0xAA) {
        puts("FAT boot sector has wrong ending");
        return true;
    }

    uint16_t buffer_16;
    uint32_t buffer_32;

    memcpy(params.oem_name, sd_buffer + 3, 8);
    memcpy(&params.bytes_per_sector, sd_buffer + 11, 2);
    params.sectors_per_cluster = sd_buffer[13];
    memcpy(&params.reserved_sectors, sd_buffer + 14, 2);
    params.num_fats = sd_buffer[16];

    memcpy(&buffer_32, sd_buffer + 17, 4);
    if (buffer_32 != 0) {
        puts("Root entry count and 16 bit total sectors should be zero on FAT32");
        return true;
    }
    memcpy(&buffer_16, sd_buffer + 22, 2);
    if (buffer_16 != 0) {
        puts("16 bit one FAT sectors should be zero on FAT32");
        return true;
    }

    memcpy(&params.total_volume_sectors, sd_buffer + 32, 4);
    memcpy(&params.sectors_per_fat, sd_buffer + 36, 4);

    params.active_fat = sd_buffer[40] & 0xF;
    params.mirroring_enabled = sd_buffer[40] & 0x80;

    if (sd_buffer[42] || sd_buffer[43]) {
        puts("Invalid FAT version");
        return true;
    }

    memcpy(&params.root_cluster, sd_buffer + 44, 4);
    memcpy(&params.fsinfo_sector, sd_buffer + 48, 2);
    memcpy(&params.volume_name, sd_buffer + 71, 11);
    params.volume_name[11] = '\0';

    return false;
}

void print_fat_params(void) {
    puts("FAT Parameters:");

    printf("\tPartitition at sector %ld with length %ld,%03ld MiB\n", params.first_sector,
           params.length_sectors / 2048 / 1000, (params.length_sectors / 2048) % 1000);

    printf("\tOEM name: '%.8s'\n", params.oem_name);
    printf("\tBytes per sector: %d\n", params.bytes_per_sector);
    printf("\tSectors per cluster: %d\n", params.sectors_per_cluster);
    printf("\tReserved sectors: %d\n", params.reserved_sectors);
    printf("\tNumber of FATs: %d\n", params.num_fats);
    printf("\tTotal volume sectors: %ld\n", params.total_volume_sectors);
    printf("\tSectors per FAT: %ld\n", params.sectors_per_fat);
    printf("\tActive FAT: %d\n", params.mirroring_enabled);
    printf("\tMirroring enabled: %s\n", params.mirroring_enabled ? "yes" : "no");
    printf("\tRoot cluster: %ld\n", params.root_cluster);
    printf("\tFSINFO sector: %d\n", params.fsinfo_sector);
    printf("\tVolume name: '%s'\n", params.volume_name);
}

void print_sector(uint32_t sector, uint8_t sd_buffer[512]) {
    if (read_sector(sd_buffer, sector))
        printf("Sector read error %ld\n", sector);

    printf("Sector %ld\n", sector);
    hexdump_offset(sd_buffer, sector * 512, 512);
}

uint32_t file_get_sector(const struct FATFile *file) {
    uint32_t sector = params.first_sector + params.reserved_sectors;
    sector += params.num_fats * params.sectors_per_fat;
    sector += (file->next_cluster - 2) * params.sectors_per_cluster + file->next_sector;
    return sector;
}

bool _file_increment_sector(struct FATFile *file) {
    uint32_t fat_start = params.first_sector + params.reserved_sectors;
    file->next_sector++;
    if (file->next_sector >= params.sectors_per_cluster) {
        file->next_sector -= params.sectors_per_cluster;

        uint32_t sector = fat_start + file->next_cluster / 128;
        uint32_t byte = 4 * (file->next_cluster % 128);

        if (send_cmd(17, sector) != 0) {
            return true;
        }

        uint8_t received;
        do {
            received = receive_spi();
        } while (received == 0xFF);
        if (received != 0xFE) {
            return true;
        }

        for (int i = 0; i < byte; i++)
            receive_spi();
        for (int i = 0; i < 4; i++)
            ((uint8_t *)(&file->next_cluster))[i] = receive_spi();
        for (int i = 0; i < 512 - byte; i++)
            receive_spi();

        if ((file->next_cluster & 0x0FFFFFF0) == 0x0FFFFFF0) {
            return true;
        }
    }

    return false;
}

int get_file_next_sector(struct FATFile *file, uint8_t buffer[512]) {
    if ((file->next_cluster & 0x0FFFFFF0) == 0x0FFFFFF0) {
        return 0;
    }

    uint32_t sector = file_get_sector(file);
    if (read_sector(buffer, sector)) {
        puts("Get file read sector error");
        close_fat();
        return -1;
    }

    _file_increment_sector(file);

    if (file->directory) {
        return 512;
    } else {
        if (file->length_remaining < 512) {
            uint16_t len = file->length_remaining;
            file->length_remaining = 0;
            return len;
        }

        file->length_remaining -= 512;
        return 512;
    }
}

static bool in_dma_transfer = false;

bool get_file_next_sector_dma(struct FATFile *file, uint8_t buffer[512]) {
    if ((file->next_cluster & 0x0FFFFFF0) == 0x0FFFFFF0) {
        return true;
    }

    uint32_t sector = file_get_sector(file);
    _file_increment_sector(file);

    if (!file->directory) {
        if (file->length_remaining < 512) {
            file->length_remaining = 0;
        } else {
            file->length_remaining -= 512;
        }
    }

    if (read_sector_dma(buffer, sector)) {
        close_fat();
        return true;
    }

    in_dma_transfer = true;

    return false;
}

bool check_dma_read_complete(void) {
    if (!in_dma_transfer)
        return true;
    if (SD_DMA->CNDTR == 0) {
        SD_DMA->CCR &= ~DMA_CCR_EN;
        SD_SPI->CR1 &= ~SPI_CR1_SPE;
        wait_for_spi();
        SD_SPI->CR1 &= ~SPI_CR1_RXONLY;
        SD_SPI->CR1 |= SPI_CR1_SPE;
        discard_spi_DR();

        // read CRC
        for (int i = 0; i < 4; i++)
            receive_spi();

        in_dma_transfer = false;

        return true;
    }
    return false;
}

void reset_file(struct FATFile *file) {
    file->next_cluster = file->start_cluster;
    file->next_sector = 0;
    file->length_remaining = file->length;
}

void open_root(struct FATFile *file) {
    file->start_cluster = params.root_cluster;
    file->directory = true;
    reset_file(file);
}

struct DIREntry {
    char name[8];
    char extension[3];
    uint8_t attributes;
    uint8_t garbage[8];
    uint16_t cluster_high;
    uint16_t last_modified[2];
    uint16_t cluster_low;
    uint32_t file_size;
} __attribute__((packed));

void ls(struct FATFile *file, uint8_t sd_buffer[512]) {
    if (!file->directory) {
        puts("LS ON FILE!");
        return;
    }

    reset_file(file);

    int length;
    bool done = false;
    uint32_t prev_sect = 0, curr_sect = file_get_sector(file);
    bool previous_was_LFN = false;

    while (!done && (length = get_file_next_sector(file, sd_buffer)) > 0) {
        for (int sec_offset = 0; sec_offset < length; sec_offset += 0x20) {
            struct DIREntry *ent = (struct DIREntry *)(sd_buffer + sec_offset);
            if (ent->attributes == 0x0F) {
                previous_was_LFN = true;
                continue;
            }

            bool LFN = previous_was_LFN;
            previous_was_LFN = false;

            if (ent->attributes == 0 && ent->name[0] == 0) {
                done = true;
                break;
            }

            if (ent->attributes & 0x02)
                continue;
            if (ent->name[0] == 0xE5)
                continue;

            printf("%8lu  ", ent->file_size);
            if (ent->attributes & 0x10) {
                printf("\033[36m");
            }

            if (LFN) {
                bool loaded_sector = false;
                struct DIREntry *lfn_ent = ent;
                while (true) {
                    lfn_ent--;

                    if (prev_sect && (void *)lfn_ent < (void *)sd_buffer) {
                        loaded_sector = true;
                        read_sector(sd_buffer, prev_sect);
                        lfn_ent = (struct DIREntry *)(sd_buffer + 512 - 0x20);
                    }

                    for (int i = 1; i < 0x20; i += 2) {
                        if (!*(((char *)lfn_ent) + i))
                            break;
                        printf("%c", *(((char *)lfn_ent) + i));
                        if (i == 0x09)
                            i += 3;
                        if (i == 0x18)
                            i += 2;
                    }
                    if (lfn_ent->name[0] & 0x40) {
                        break;
                    }
                }
                if (loaded_sector)
                    read_sector(sd_buffer, curr_sect);
            } else {
                int len = 0;
                for (int i = 0; i < 8; i++) {
                    if (ent->name[i] != ' ')
                        len = i + 1;
                }
                printf("%.*s", len, ent->name);
                len = 0;
                for (int i = 0; i < 3; i++) {
                    if (ent->extension[i] != ' ')
                        len = i + 1;
                }
                if (len > 0) {
                    printf(".%.*s", len, ent->extension);
                }
            }

            puts("\033[m");

            // uint32_t cluster = ent->cluster_high << 16;
            // cluster |= ent->cluster_low;

            // printf(": 0x%08lx (0x%08lx)\n", cluster, ent->file_size);
        }
        prev_sect = curr_sect;
        curr_sect = file_get_sector(file);
    }
}

uint32_t _find_dir_entry_matching(const char *name, struct FATFile *file, uint8_t sd_buffer[512],
                                  struct DIREntry **dir_entry) {
    if (!file->directory)
        return 0;

    reset_file(file);

    int length;
    bool done = false;
    bool match;
    uint32_t prev_sector = 0, sector = file_get_sector(file);
    bool previous_was_LFN = false;

    while (!done && (length = get_file_next_sector(file, sd_buffer)) > 0) {
        for (int sec_offset = 0; sec_offset < length; sec_offset += 0x20) {
            struct DIREntry *ent = (struct DIREntry *)(sd_buffer + sec_offset);
            if (ent->attributes == 0x0F) {
                previous_was_LFN = true;
                continue;
            }

            bool LFN = previous_was_LFN;
            previous_was_LFN = false;

            if (ent->attributes == 0 && ent->name[0] == 0) {
                done = true;
                break;
            }

            if (ent->attributes & 0x02)
                continue;
            if (ent->name[0] == 0xE5)
                continue;

            int name_pos = 0;

            match = true;
            if (LFN) {
                bool loaded_sector = false;
                struct DIREntry *lfn_ent = ent;
                while (match) {
                    lfn_ent--;

                    if (prev_sector && (void *)lfn_ent < (void *)sd_buffer) {
                        loaded_sector = true;
                        read_sector(sd_buffer, prev_sector);
                        lfn_ent = (struct DIREntry *)(sd_buffer + 512 - 0x20);
                    }

                    for (int i = 1; i < 0x20; i += 2) {
                        if (!*(((char *)lfn_ent) + i))
                            break;
                        if (name[name_pos++] != *(((char *)lfn_ent) + i)) { // TODO: case insensitive
                            match = false;
                            break;
                        }

                        if (i == 0x09)
                            i += 3;
                        if (i == 0x18)
                            i += 2;
                    }
                    if (lfn_ent->name[0] & 0x40) {
                        break;
                    }
                }
                if (loaded_sector)
                    read_sector(sd_buffer, sector);
            } else {
                int len = 0;
                for (int i = 0; i < 8; i++) {
                    if (ent->name[i] != ' ')
                        len = i + 1;
                }
                for (int i = 0; i < len; i++) {
                    if (name[name_pos++] != ent->name[i]) {  // TODO: case insensitive
                        match = false;
                        break;
                    }
                }

                len = 0;
                for (int i = 0; i < 3; i++) {
                    if (ent->extension[i] != ' ')
                        len = i + 1;
                }
                if (len > 0) {
                    if (name[name_pos++] != '.') {
                        match = false;
                        continue;
                    }
                }
                for (int i = 0; i < len; i++) {
                    if (name[name_pos++] != ent->extension[i]) {  // TODO: case insensitive
                        match = false;
                        break;
                    }
                }
            }

            if (!name[name_pos] && match) {
                *dir_entry = ent;
                return sector;
            }
        }

        prev_sector = sector;
        sector = file_get_sector(file);
    }

    return 0;
}

bool open(const char *name, struct FATFile *file, uint8_t sd_buffer[512]) {
    struct DIREntry *ent;

    if (_find_dir_entry_matching(name, file, sd_buffer, &ent)) {
        file->directory = !!(ent->attributes & 0x10);
        file->start_cluster = (ent->cluster_high << 16) | ent->cluster_low;
        if (!file->start_cluster && file->directory)
            open_root(file);
        else {
            file->length = ent->file_size;
            reset_file(file);
        }
        return false;
    }

    return true;
}

bool set_size(const char *name, const uint32_t size, struct FATFile *file, uint8_t sd_buffer[512]) {
    struct DIREntry *ent;
    uint32_t sector;
    if ((sector = _find_dir_entry_matching(name, file, sd_buffer, &ent))) {
        printf("%p %p\n", sd_buffer, ent);
        ent->file_size = size;
        if (write_sector(sd_buffer, sector)) {
            return true;
        }
        return false;
    }
    return true;
}

uint32_t _get_open_cluster(uint32_t previous_cluster, uint8_t sd_buffer[512]) {
    static uint32_t last_open = 0;

    uint32_t fat_start = params.first_sector + params.reserved_sectors;

    for (uint32_t fat_sector = last_open; fat_sector < params.sectors_per_fat; fat_sector++) {
        read_sector(sd_buffer, fat_start + fat_sector);

        uint32_t *buffer = (uint32_t *)sd_buffer;
        for (int offset = 0; offset < 128; offset++) {
            if (!buffer[offset]) {
                printf("Found cluster: %ld %d\n", fat_sector, offset);
                last_open = fat_sector;
                buffer[offset] = 0x0FFFFFFF;
                if (write_sector(sd_buffer, fat_start + fat_sector)) {
                    return 0;
                }
                return fat_sector * 128 + offset;
            }
        }
    }

    return 0;
}

void new_file(struct FATFile *file, const char *name, const char *ext, bool dir,
              uint8_t sd_buffer[512]) {
    if (!file->directory) {
        puts("WRITE ON FILE!");
        return;
    }

    reset_file(file);

    int length, offset = 0; // initial value to get rid of bogus warning
    bool done = false;

    uint32_t cluster = _get_open_cluster(0, sd_buffer);
    uint32_t sector = file_get_sector(file);

    while (!done && (length = get_file_next_sector(file, sd_buffer)) > 0) {
        for (int sec_offset = 0; sec_offset < length; sec_offset += 0x20) {
            struct DIREntry *ent = (struct DIREntry *)(sd_buffer + sec_offset);

            if (ent->name[0] == 0xE5 || (ent->attributes == 0 && ent->name[0] == 0)) {
                offset = sec_offset;
                done = true;
                break;
            }
        }

        if (!done)
            sector = file_get_sector(file);
    }

    if (!done) {
        // reached end of last cluster without finding empty slot
        uint32_t sector = params.first_sector + params.reserved_sectors + file->next_cluster / 128;
        uint32_t offset = file->next_cluster % 128;
        read_sector(sd_buffer, sector);
        ((uint32_t *)sd_buffer)[offset] = cluster;
        write_sector(sd_buffer, sector);

        cluster = _get_open_cluster(0, sd_buffer);

        file->next_cluster = cluster;
        file->next_sector = 0;
        sector = file_get_sector(file);
    }

    struct DIREntry *new_entry = (void *)(sd_buffer + offset);
    memset(new_entry, 0, 0x20);
    new_entry->attributes = dir ? 0x10 : 0x20;
    int i;
    for (i = 0; i < 8; i++) {
        if (name[i])
            new_entry->name[i] = name[i];
        else
            break;
    }
    for (; i < 8; i++) {
        new_entry->name[i] = ' ';
    }
    for (i = 0; i < 3; i++) {
        if (ext[i])
            new_entry->extension[i] = ext[i];
        else
            break;
    }
    for (; i < 3; i++) {
        new_entry->extension[i] = ' ';
    }
    new_entry->cluster_low = cluster & 0xFFFF;
    new_entry->cluster_high = cluster >> 16;
    new_entry->file_size = 0;

    write_sector(sd_buffer, sector);

    file->directory = dir;
    file->start_cluster = cluster;
    file->length = 0;
    reset_file(file);
}
