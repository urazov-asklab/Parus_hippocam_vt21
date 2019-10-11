
#include <string.h>

#include "common.h"

#include "getrnd64.h"

// генерируем случаюную последовательность с помощью аудио/видео содержимого

int GetRandom64(u8 *video_buffer, int width, int height, int y_offset, u8 *result)
{
    if ((width < (VIDEO_BORDER_SIZE * 2 + 100)) || (height < (VIDEO_BORDER_SIZE * 2 + 90)))
    {
        return FAILURE;
    }

    u16                 accum[4];
    struct  timespec    monotime;

    int                 cycles      = 0;
    int                 step        = 0;
    int                 cycle_count = 0;
    int                 x           = 0;
    int                 y0          = VIDEO_BORDER_SIZE;
    int                 addr        = 0;
    int                 bc          = 0;
    int                 ic          = 0;
    u8                  y           = 0;

    memset(accum, 0, 8);

    clock_gettime(CLOCK_MONOTONIC, &monotime);
    cycles      = monotime.tv_nsec;
    step        = (cycles & 7) + 9;
    cycle_count = (((cycles >> 2) & 3) + 5) * 64;
    x           = VIDEO_BORDER_SIZE + step;
    addr        = (y0 * width + x) * 2 + y_offset;

    while (ic < cycle_count)
    {
        y = video_buffer[addr];
        if (y > 0x10)
        {
            if ((y & 1) != 0)
            {
          	    accum[bc >> 4] ^= (1 << (bc & 0x0f));
            }
            bc++;
            bc &= 0x3f;
            ic++;
        }
        x       += step;
        addr    += (step + step);
        if (x >= (width - VIDEO_BORDER_SIZE))
        {
            x -= (width - VIDEO_BORDER_SIZE - VIDEO_BORDER_SIZE);
            y++;
            if (y < (height - VIDEO_BORDER_SIZE))
            {
                addr += (width + width);
            }
            else
            {
                y       = VIDEO_BORDER_SIZE;
                addr    = (y * width + x) * 2 + y_offset;
            }
        }
    }
    memcpy(result, accum, 8);
    return SUCCESS;
}

int GetRandomAudio64(u8 *audio_buffer, int width, int y_offset, u8 *result)
{
    u16                 accum[4];
    struct  timespec    monotime;

    int                 cycles      = 0;
    int                 step        = 0;
    int                 cycle_count = 0;
    int                 x           = 0;
    int                 addr        = 0;
    int                 bc          = 0;
    int                 ic          = 0;
    u8                  y           = 0;

    memset(accum, 0, 8);

    clock_gettime(CLOCK_MONOTONIC, &monotime);
    cycles      = monotime.tv_nsec;
    step        = (cycles & 7) + 9;
    cycle_count = (((cycles >> 2) & 3) + 5) * 64;
    x           = step;
    addr        = x * 2 + y_offset;

    while (ic < cycle_count)
    {
        y = audio_buffer[addr];
        if (y > 0x10)
        {
            if ((y & 1) != 0)
            {
          	    accum[bc >> 4] ^= (1 << (bc & 0x0f));
            }
            bc++;
            bc &= 0x3f;
            ic++;
        }
        x       += step;
        addr    += step;
        if (x >= width)
        {
            x   -= (width - 3);
            addr = x * 2 + y_offset;
        }
    }
    memcpy(result, accum, 8);
    return SUCCESS;
}
