#ifndef HEXDUMP_H
#define HEXDUMP_H

#include <inttypes.h>

void hexdump_offset(const void *data, uint32_t offset, uint32_t length);
void hexdump(const void *data, uint32_t length);

#endif
