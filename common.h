/*
 * common.h
 *
 * Copyright (C) 2010 Texas Instruments Incorporated - http://www.ti.com/ 
 * 
 * 
 *  Redistribution and use in source and binary forms, with or without 
 *  modification, are permitted provided that the following conditions 
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the   
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS 
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT 
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT 
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, 
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE 
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
*/

#ifndef _COMMON_H
#define _COMMON_H

/* Standard Linux headers */
#include <pthread.h>
#include <semaphore.h>

#include <ti/sdo/dmai/Buffer.h>

#define CLOCK_MONOTONIC 1

#define INLINE  __inline__ __attribute__((always_inline))

#define u8  unsigned char
#define u16 unsigned short
#define u32 unsigned long
#define u64 unsigned long long

#define GIGABYTE 1073741824     // bytes in gygabyte
#define MEGABYTE 1048576        // bytes in megabyte

// enable sound recording to mp4 file
#define SOUND_EN

// #define THREADLOG_EN

/* Error message */
#define ERR(fmt, args...) fprintf(stderr, "Error: " fmt, ## args)
#define WARN(fmt, args...) fprintf(stderr, "Warning: " fmt, ## args)

/* Debug message */
#define _DEBUG_

#ifdef _DEBUG_
    #define debug(fmt, args...) printf(" " fmt, ## args)
#else
    #define debug(fmt, args...)
#endif

#define to_bcd(a)           ( ( ( ((a)%100) / 10) << 4) | ( ((a)%100) % 10) )
#define from_bcd(a)         ( ( ( (a)>>4) * 10) + ( ( (a)&0xF ) ) )
#define to_num(a,b,c,d)     ( ((a)<<24) | ((b)<<16) | ((c)<<8) | (d) )
#define get32v(a)           ( ((a)[0]<<24) | ((a)[1]<<16) | ((a)[2]<<8) | (a)[3] )

/* Function error codes */
#define SUCCESS                 0
#define FAILURE                 -1
#define RESTART                 3

/* Thread error codes */
#define THREAD_SUCCESS          (void *) 0
#define THREAD_FAILURE          (void *) -1

#define NUM_CAP_VIDEO_BUFS      16
// #define NUM_ENC_VIDEO_BUFS      32
// #define NUM_PAC_VIDEO_BUFS      96

#define MIN_VBITRATE            500000
#define MAX_VBITRATE            2000000
// #define MAX_VBITRATE            10000000

/* Sound sampling frequency */
#define SAMPLING_FREQUENCY      16000
#define AUDIO_BITRATE           64000
#define MAX_DURATION            (150*60)                             //150 min in seconds

#define FRAME_WIDTH             720
//#define FRAME_CROP_WIDTH        704
#define FRAME_CROP_WIDTH        FRAME_WIDTH

#define KEY_FRAME_INTERVAL      1
#define MOVIE_TIMESCALE         600

#define SAMPLES_PER_CHUNK       1024
#define CHUNKS_IN_GROUP         4
#define AUDIO_HEADER_SIZE       7
#define AUDIO_CHUNK_GROUP_SIZE  6144
#define AUDIO_CHUNKS_PER_HOUR   56250

#define US_IN_SEC               1000000
#define MS_IN_SEC               1000


/* ========== GPIO numbers ================== */

#define WAKE_UP_VAL             42//port interrupt
#define WAKE_UP_EN_VAL          36
#define CHANGE_DCAM_VAL         200
#define USB_CS_VAL              84
// #define PWR_RF_HOLD_VAL         206         // PWR_RF_EN
#define SW_PWR_ON_VAL           199
#define RF_PWR_ON_VAL           198
#define USB_ON_VAL              197
#define EXT_CHRG_ON_VAL         196
#define RF_RST_VAL              16
#define RF_OE_VAL               27
#define SDR_TEMP                115
#define SD_CD                   78

/* ========== GPIO numbers ================== */

#define MAP_SIZE                4096UL
#define MAP_MASK                (MAP_SIZE - 1)

#define MAX_BUF                 64
#define MAX_STATUS_LENGTH       9

typedef enum
{							// Don't change the sequence !!!
	CC1200_BR_1_2K,			// 0
	CC1200_BR_250K,			// 1
	CC1200_BR_1_2K_WOR,		// 2
	CC1200_BR_57_6K,		// 3
	CC1200_BR_UNKNOWN
} rfSpeedType;

/* Thread priorities */
#define LOGGING_THREAD_PRIORITY         sched_get_priority_max(SCHED_FIFO) - 7
#define UCPSERVICE_THREAD_PRIORITY      sched_get_priority_max(SCHED_FIFO) - 7
#define NETCOMM_THREAD_PRIORITY         sched_get_priority_max(SCHED_FIFO) - 7

#define SETTINGSERVER_THREAD_PRIORITY   sched_get_priority_max(SCHED_FIFO) - 6
#define INDICATION_THREAD_PRIORITY      sched_get_priority_max(SCHED_FIFO) - 6

#define AVRECSERVICE_THREAD_PRIORITY    sched_get_priority_max(SCHED_FIFO) - 5

#define RADIOCOMM_THREAD_PRIORITY       sched_get_priority_max(SCHED_FIFO) - 4
#define RSZVIDEO_THREAD_PRIORITY        sched_get_priority_max(SCHED_FIFO) - 4

#define AUDIO_THREAD_PRIORITY           sched_get_priority_max(SCHED_FIFO) - 3
#define PACKER_THREAD_PRIORITY          sched_get_priority_max(SCHED_FIFO) - 3
#define RESIZE_THREAD_PRIORITY          sched_get_priority_max(SCHED_FIFO) - 3
#define AEWB_THREAD_PRIORITY            sched_get_priority_max(SCHED_FIFO) - 3

#define STREAMBUFFS_THREAD_PRIORITY     sched_get_priority_max(SCHED_FIFO) - 2
#define WISSTREAM_THREAD_PRIORITY       sched_get_priority_max(SCHED_FIFO) - 2
#define RADIODATATX_THREAD_PRIORITY     sched_get_priority_max(SCHED_FIFO) - 2

#define VIDEO_THREAD_PRIORITY           sched_get_priority_max(SCHED_FIFO) - 1

#define ACAPTURE_THREAD_PRIORITY        sched_get_priority_max(SCHED_FIFO) - 0
#define VCAPTURE_THREAD_PRIORITY        sched_get_priority_max(SCHED_FIFO) - 0


volatile int            sd_partitions_flag; // 1 - sd card has one partition; 2 - more than one
volatile int            rsz_height;
volatile int            rsz_width;
volatile int            enc_height;
volatile int            enc_width;
volatile int            frame_height;
volatile int            actual_frame_width;
volatile int            frames_per_hour;
volatile int            framerate;
volatile int            frames_per_sec;
volatile int            frames_per_sec_rsz;
volatile u16            half_vrate;

volatile u8             samba_on;
volatile u8             stop_netconnect;
volatile u8             is_netconnect_on;
volatile u8             wifi_sleep_condition;
volatile u8             rf_sleep_condition;
volatile u32            last_connected_time;
volatile u8             is_file_not_empty;
volatile u8             is_sd_inserted;
volatile u8             is_cam_failed;
volatile u8             charge_low;
volatile u8             rf_off;
volatile u8             charger_level;
volatile u8             charger_present;
volatile u8             cam_channel_num;
volatile u8             is_config_changed_from_wlan;
volatile u8            	video_source_num;
volatile u8             cam_voltage;
volatile u8             is_access_point;
volatile u8             need_to_create_log;
volatile u8             color_video;
volatile u8             sleep_on_radio;
volatile u8             deinterlace_on;
volatile u8             leds_always;
volatile u8             sound_only;

volatile u8             go_to_wor;
volatile u8             led_on;
volatile u8             sleep_finished;
volatile u8             after_wake_up;
volatile u8             set_wor_mode;
volatile u8             analog_mic_enable;
volatile u8             got_key_frame;
volatile u8             is_waked_from_rf;
volatile u8             internal_error;
volatile u8             sd_failed;
volatile u8             is_event_occured;
volatile u8             is_video_tx_started;
volatile u8             is_video_captured;
volatile u8             is_sd_mounted;
volatile u8             is_rsz_started;
volatile u8             is_enc_started;
volatile u8             is_cap_started;
volatile u8             is_rec_started;         // device is recording video or audio file
volatile u8             is_rec_on_cmd;          // device received a command to start recording
volatile int            start_rec;
volatile u8             is_rftx_started;
volatile u8             is_stream_started;
volatile u8             is_rftx_request;
volatile u8             is_rec_request;
volatile u8             is_stream_request;
volatile u8             is_sleep_request;
volatile u8             is_finish_requets;
volatile u8             is_rftx_finishing;
volatile u8             is_rec_finishing;
volatile u8             is_stream_finishing;
volatile u8             is_cap_finishing;
volatile u8             is_enc_finishing;
volatile u8             is_stream_failed;
volatile u8             is_rftx_failed;
volatile u8             is_rec_failed;
volatile u8             is_memory_full;         // memory is full
volatile u8             is_rf_sleep;            // radiochip is sleeping
volatile rfSpeedType    rf_speed;
volatile rfSpeedType    wor_speed;
volatile u8             init_step;              // device is initializing
volatile u32            last_rec_time;
volatile u8             is_usb_on;
volatile u32            video_bitrate;
volatile u32            audio_channels;
volatile u32            cam_brightness;
volatile u32            cam_contrast;
volatile float          cam_saturation;
volatile int            cnt_restart;
volatile int            fd_wdt;
volatile int            digital_mic_gain;
volatile int            analog_mic_gain1;
volatile int            analog_mic_gain2;
volatile int            cnt_snd;
volatile int            cnt_tri;
volatile int            cnt_sd;
volatile int            rftx_stop;
volatile int            strm_error;
//volatile int            chng_mask;

volatile long long int  sd_freespace;
volatile long long int  sd_totalspace;
volatile u32            sd_status;

volatile u8             mp4_vol[512];
volatile int            mp4_vol_size;

pthread_mutex_t         sd_mount_mutex;
pthread_cond_t          sd_mount_cond;  // сигнал, что sd-карта примонтирована (работает в паре с мьютексом)
pthread_mutex_t         mp4_vol_mutex;
pthread_cond_t          mp4_vol_cond;   // сигнал, что получили SPS и PPS (работает в паре с мьютексом)
pthread_mutex_t         rftx_mutex;
pthread_cond_t          rftx_cond;      // сигнал, что трансляция контента по радиоканалу готова 
                                        // (работает в паре с мьютексом)
pthread_mutex_t         socket_mutex;
pthread_cond_t          socket_cond;    // сигнал, что закрыли сокет (работает в паре с мьютексом)
pthread_mutex_t         start_rec_mutex;// ограничение доступа к файлу /opt/start_rec

#ifdef THREADLOG_EN
volatile FILE          *pFifoLogFile;
#endif
volatile char           fifoLogString[256];

volatile char           wifissid[13];
volatile char           wifipass[13];

volatile int            hdmi_active;
volatile int            n_usb_vbus_ok;
volatile int            n_usb_vbus_ok_fd;
volatile int            change_dcam;
volatile int            change_dcam_fd;

volatile time_t         set_wor_time;
volatile time_t         start_time;   
volatile time_t         low_ch_time; 

volatile struct timeval setting_wor_time;
volatile struct timeval setting_wor_time2;
volatile struct timeval indicate_time;
volatile struct timeval indicate_time2;
volatile struct timeval temp_time;
volatile struct timeval temp_time2;
volatile struct timeval prev_time;
volatile struct timeval prev_st_time;

volatile struct timespec net_file_prev_time;

sem_t  semaphore;    // оповещение о новом событии для логгирования
sem_t  wake_up_done; // оповещение о выходе процессора из сна

volatile u32            is_pwr_on;
volatile u32            is_usb_storage_started;

volatile int            wakeup_en;
volatile int            port_int;
volatile int            sdr_temp_pin;
volatile int            sdr_temp_pin_fd;
volatile int            ext_chrg_on; 
volatile int            sw_pwr_on; 
volatile int            usb_cs;
volatile int            ext_chrg_on_fd; 
volatile int            sw_pwr_on_fd; 
volatile int            usb_cs_fd;

volatile int            cnt;
volatile int            cnt_rf;
volatile int            cnt_ch;
volatile int            cnt_mem;
volatile u32            device_addr;
volatile u32            prev_temp_sens;
volatile u32            temp_sens;
volatile u32            is_sdcard_off_status;

typedef enum
{
    READ,
    WRITE
} AccessType;

int get_record_state();
void fix_record_state(int rec_on);

int  readPID (const char *pidfile);
u32  getDeviceAddr();
void log_threads(char *msg);
void makewaittime(struct timespec *tsp, long seconds, long nanoseconds);
u64 uptime_ms();
u32 uptime();

typedef enum
{
    START_REC,              // 0
    STOP_REC,               // 1
    FINISH,                 // 2
    SLEEP,                  // 3
    START_STREAM,           // 4
    STOP_STREAM,            // 5
    START_USB_STORAGE,      // 6
    STOP_USB_STORAGE,       // 7
    START_CAP,              // 8
    STOP_CAP,               // 9
    START_RFTX,             // 10
    STOP_RFTX,              // 11
    START_WIS,              // 12
    STOP_WIS,               // 13
    START_ENC,              // 14
    STOP_ENC,               // 15
    NO_COMMAND              // 16
} Command;     

typedef struct Codec 
{
    /* String name of codec for CE to locate it */
    char    *codecName;

    /* The string to show on the UI for this codec */ 
    char    *uiString;

    /* NULL terminated list of file extensions */
    char   **fileExtensions;

    /* Params to use with codec, if NULL defaults are used */
    void    *params;

    /* Dynamic params to use with codec, if NULL defaults are used */
    void    *dynParams;
} Codec;

typedef struct Engine 
{
    /* Engine string name used by CE to find the engine */
    char    *engineName;

    /* Speech decoders in engine */
    Codec   *speechDecoders;

    /* Audio decoders in engine */
    Codec   *audioDecoders;

    /* Video decoders in engine */
    Codec   *videoDecoders;

    /* Speech encoders in engine */
    Codec   *speechEncoders;

    /* Audio encoders in engine */
    Codec   *audioEncoders;

    /* Video encoders in engine */
    Codec   *videoEncoders;
} Engine;

/* The engine exported from codecs.c for use in main.c */
extern Engine *engine;

typedef enum
{
    NOBODY_QID  = 0,
    REC_QID     = 1 << 0,  // 1
    STRM_QID    = 1 << 1,  // 2
    RFTX_QID    = 1 << 2,  // 4
} quit_id;

/* Global data structure */
typedef struct GlobalData 
{
    int             quit_flags;          /* Global quit flags */
    Command         cmd;                 /* Current user command */
    pthread_mutex_t mutex;               /* Mutex to protect the global data */
} GlobalData;

#define GBL_DATA_INIT { 0 }

/* Global data */
extern GlobalData gbl;

/* Functions to protect the global data */
static inline int gblGetQuit(int flags)
{
    int quit = 0;

    pthread_mutex_lock(&gbl.mutex);
    quit = ((flags & gbl.quit_flags) == flags);
    pthread_mutex_unlock(&gbl.mutex);

    return quit;
}

static inline void gblSetQuit(int flags)
{
    pthread_mutex_lock(&gbl.mutex);
    gbl.quit_flags |= flags;
    pthread_mutex_unlock(&gbl.mutex);
}

static inline void gblResetQuit(int flags)
{
    pthread_mutex_lock(&gbl.mutex);
    gbl.quit_flags &= ~flags;
    pthread_mutex_unlock(&gbl.mutex);
}

static inline Command gblGetCmd(void)
{
    Command cmd;

    pthread_mutex_lock(&gbl.mutex);
    cmd = gbl.cmd;
    pthread_mutex_unlock(&gbl.mutex);

    return cmd;
}

static inline void gblSetCmd(Command cmd)
{
    pthread_mutex_lock(&gbl.mutex);
    gbl.cmd = cmd;
    pthread_mutex_unlock(&gbl.mutex);
}

#define UYVY_BLACK      0x10801080

static inline void blackFill(Buffer_Handle hBuf)
{
    int *bufPtr  = (int*)Buffer_getUserPtr(hBuf);
    int  bufSize = Buffer_getSize(hBuf) / sizeof(int);
    int  i;

    for (i = 0; i < bufSize; i++) 
    {
        bufPtr[i] = UYVY_BLACK;
    }
}

static inline u64 time_in_us()
{
  struct timespec current_time;

  clock_gettime(CLOCK_MONOTONIC, &current_time); 
  u64 mt = (u64)current_time.tv_sec * (u64)US_IN_SEC + ((u64)current_time.tv_nsec + (u64)500) / (u64)1000;
  return mt;
}

/* Cleans up cleanly after a failure */
#define cleanup(x, y)                               \
    if((int)(x) == -1)                              \
    {                                               \
        printf("THREAD FAILURE\r\n");               \
    }                                               \
    else if((int)(x) == 0)                          \
    {                                               \
        printf("THREAD_SUCCESS\r\n");               \
    }                                               \
    status = (x);                                   \
    gblSetQuit(y);                                  \
    goto cleanup

#define csys(x)                                     \
    if (system((x)) != 0) return FAILURE


#endif /* _COMMON_H */
