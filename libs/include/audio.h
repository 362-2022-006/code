#ifndef AUDIO_H
#define AUDIO_H

#include "fat.h"

void start_audio(char *filename);
void play_audio(struct FATFile file, int rate);

#endif
