#include <inttypes.h>
#include <stdio.h>

static uint8_t CRC7(const uint8_t *data, uint8_t n) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint8_t d = data[i];
        for (uint8_t j = 0; j < 8; j++) {
            crc <<= 1;
            if ((d & 0x80) ^ (crc & 0x80)) {
                crc ^= 0x09;
            }
            d <<= 1;
        }
    }
    return (crc << 1) | 1;
}

int main(void) {
    uint8_t data[6] = {59 | 0x40, 0x00, 0x00, 0x00, 0x00};
    printf("CRC: 0x%02x\n", CRC7(data, 5));
    return 0;
}
