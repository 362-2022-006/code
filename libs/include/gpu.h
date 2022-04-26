#ifndef __GPU_H__
#define __GPU_H__
#include "types.h"

typedef struct {
    u16 x;
    u16 y;
    const u16 *data;
    u16 meta;
    /*
    bits 15-14:
    case 00:
        constant size
        bits 13-2: unused
    case 01:
        run length
        bits 13-8: unused
        bits  7-2: number of row (limited to 6 bits because cache size)
    case 1x:
        variable size
        bits 13-8: x size
        bits  7-2: y size

    always:
    bits 1-0: initialize to 0
        00 --> x command, x data
        01 --> y command, y data
        10 --> start command, data
    */
} GPU_FIFO;

void init_gpu();
void gpu_buffer_add(u16 x, u16 y, const void *data, u16 meta);

void disable_gpu();
void reenable_gpu();

#endif
