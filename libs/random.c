#include "random.h"

#include "tinymt32.h"

tinymt32_t random_state;
u8 initialized = 0;

static inline void _seed_random(u32 seed) {
    tinymt32_init(&random_state, seed);
    initialized = 1;
}

void mix_random(u32 seed) {
    if (initialized)
        _seed_random(seed ^ get_random());
    else
        _seed_random(seed);
}

u32 get_random(void) {
    if (!initialized)
        _seed_random(0);
    return tinymt32_generate_uint32(&random_state);
}
