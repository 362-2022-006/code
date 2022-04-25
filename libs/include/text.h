#ifndef __TEXT_H__
#define __TEXT_H__

#include "types.h"

void init_text(bool use_buffer);
void set_screen_text_buffer(bool on);

void blank_screen(void);

u16 get_current_line(void);
u16 get_current_column(void);

#endif
