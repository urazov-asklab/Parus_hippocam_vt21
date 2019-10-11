
#ifndef _GETRND64_H
#define _GETRND64_H

#include "common.h"

#define VIDEO_BORDER_SIZE 	16

int GetRandom64( u8 *video_buffer, int width, int height, int y_offset, u8 *result);    
int GetRandomAudio64( u8 *audio_buffer, int width, int y_offset, u8 *result);

#endif /* _GETRND64_H */
