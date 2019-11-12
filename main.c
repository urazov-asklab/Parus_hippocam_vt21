
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/ioctl.h>
#include <linux/watchdog.h>
#include <xdc/std.h>
#include <sys/mman.h>

#include <ti/sdo/ce/CERuntime.h>
#include <ti/sdo/dmai/Dmai.h>
#include <ti/sdo/dmai/Rendezvous.h>
#include <ti/sdo/linuxutils/cmem/src/interface/cmem.h>

#include "common.h"
#include "gpio_func.h"
#include "mp4ff.h"
#include "video.h"
#include "acapture.h"
#include "vcapture.h"
#include "packer.h"
//#include "resize.h"
#include "audio.h"
//#include "ask_rf.h"
#include "radiocomm.h"
#include "stream_buffs.h"
#include "wis_stream.h"
//#include "radiodatatx.h"
#include "logging.h"
#include "indication.h"
#include "transport_buffers.h"
//#include "cc1200_func.h"
#include "eth_iface.h"
#include "avrec_service.h"
#include "net_connection.h"

/* The flags of initialization */
#define AEWBTHREADCREATED           0x1
#define INDICATIONTHREADCREATED     0x10
#define LOGGINGTHREADCREATED        0x20
#define ACAPTURETHREADCREATED       0x40
#define VCAPTURETHREADCREATED       0x80
#define VIDEOTHREADCREATED          0x100
#define AUDIOTHREADCREATED          0x200
#define PACKERTHREADCREATED         0x400
#define RESIZETHREADCREATED         0x800
#define RADIOCOMMTHREADCREATED      0x1000
#define RSZVIDEOTHREADCREATED       0x2000
#define RADIODATATXTHREADCREATED    0x4000
#define STREAMBUFFSTHREADCREATED    0x8000
#define NETCOMMTHREADCREATED        0x10000

// для каждой нити - массив переменных, передаваемых в неё
//AEWBEnv                     aewbEnv;
ACaptureEnv                 aCaptureEnv;
VCaptureEnv                 vCaptureEnv;
VideoEnv                    videoEnv;
VideoEnv                    rszVideoEnv;
AudioEnv           	        audioEnv;
PackerEnv                   packerEnv;
//ResizeEnv                   resizeEnv;
StreamBuffsEnv              streamBuffsEnv;
LoggingEnv                  loggingEnv;
IndicationEnv               indicationEnv;
RadioCommEnv                radioCommEnv;
NetCommEnv                  netCommEnv;

Rendezvous_Handle           hRendezvousInitCap          = NULL;
Rendezvous_Handle           hRendezvousInitEnc          = NULL;
Rendezvous_Handle           hRendezvousInitVR           = NULL;
//Rendezvous_Handle           hRendezvousInitRFTX         = NULL;
Rendezvous_Handle           hRendezvousInitRC           = NULL;
Rendezvous_Handle           hRendezvousInitLG           = NULL;
Rendezvous_Handle           hRendezvousInitI            = NULL;
Rendezvous_Handle           hRendezvousFinishCap        = NULL;
Rendezvous_Handle           hRendezvousFinishEnc        = NULL;
Rendezvous_Handle           hRendezvousFinishVR         = NULL;
//Rendezvous_Handle           hRendezvousFinishRFTX       = NULL;
Rendezvous_Handle           hRendezvousFinishRC     	= NULL;
Rendezvous_Handle           hRendezvousFinishLG         = NULL;
Rendezvous_Handle           hRendezvousFinishI          = NULL;
Rendezvous_Handle           hRendezvousCapture          = NULL;
Rendezvous_Handle           hRendezvousFinishSTRM   	= NULL;
Rendezvous_Handle           hRendezvousFinishNC         = NULL;

pthread_t                   aCaptureThread;
pthread_t                   vCaptureThread;
pthread_t                   videoThread;
//pthread_t                   rszVideoThread;
pthread_t                   audioThread;
pthread_t                   packerThread;
//pthread_t                   resizeThread;
pthread_t                   streamBuffsThread;
//pthread_t                   radioDataTXThread;
pthread_t                   loggingThread;
pthread_t                   indicationThread;
pthread_t                   radioCommThread;
pthread_t                   netCommThread;

pthread_mutex_t             encEngineLock;
int                         pairSync;

Rendezvous_Attrs            rzvAttrs                    = {0};
unsigned int                initMask                    = 0;
Command                     currentCommand;

GlobalData gbl      = GBL_DATA_INIT;
RingBuffer evtBuf;

//int load_settings_from_cfg_file();
int init_mutexes();
int init_thread_env();
int init_threads();

int get_usb_status()
{
    u32 value = 0xFFFFFFFF;

    gpio_get_value(usb_vbus_ok_pin, &value);
    
    if(value == 0)
        return 1;
    else if (value == 1)
            return 0;
        else
            return FAILURE;
    
}

void check_temp_sensor()
{
    u32 temp_sens  = 0;

    prev_temp_sens = temp_sens;
    gpio_get_value(sdr_temp_pin, &temp_sens);
    
    if(prev_temp_sens != temp_sens)
    {
        if(temp_sens == 1)
        {
            logEvent(log_SYSTEM_OVERHEAT);               // RAM sensor overheat
            WARN("SDR_TEMP_SENSOR: OVERHEAT\r\n");
        }
        else if(temp_sens == 0)
        {
            logEvent(log_SYSTEM_UNDERHEAT);              // return to correct temperature mode
            debug("SDR_TEMP_SENSOR: UNDERHEAT\r\n");
        }
    }
}

int check_charge_status()
{
    char    buf[MAX_BUF];
    char    ch[MAX_STATUS_LENGTH];
    // int     hours;
    // int     mins;
    // int     secs;
    int     fd;
//    int     len;
    int     time_diff;
    int     value           = 0;
    char    full_status[]   = "Full";
    char    normal_status[] = "Normal";
    char 	bat_name[] = "max77818";

    snprintf(buf, sizeof(buf), "/sys/class/power_supply/max77818/online"); 
    
    fd  = open(buf, O_RDONLY);
    if(fd < 0) 
    {
        ERR("Can't open /sys/class/power_supply/max77818/online\r\n");
        return fd;
    }
    read(fd, &ch, 1);
    charger_present = (ch[0] != '0') ? 1 : 0;
    close(fd);
    

    /* закомментирован код для определения статуса аккумулятора с помощью fuel gauge
       требуется отладка и тестирование*/
    // len = snprintf(buf, sizeof(buf), "/sys/class/power_supply/max77818/device/fg_state_of_charge_pct"); 
    // if(len <= 0)
    // {
    //     ERR("Can't open /sys/class/power_supply/max77818/device/fg_state_of_charge_pct\r\n");
    //     return FAILURE;
    // }
    // else
    // {
    //     fd  = open(buf, O_RDONLY);
    //     if (fd < 0) 
    //     {
    //         ERR("Can't open /sys/class/power_supply/max77818/device/fg_state_of_charge_pct\r\n");
    //         return fd;
    //     }
     
    //     read(fd, &ch, MAX_STATUS_LENGTH);

    //     value = atoi(ch);

    //     value = value >> 8;

    //     //debug("STATE OF CHARGE %i %\r\n", value);
    
    //     close(fd);
    // }

    // len = snprintf(buf, sizeof(buf), "/sys/class/power_supply/max77818/device/fg_time_to_empty_hms"); 
    // if(len <= 0)
    // {
    //     ERR("Can't open /sys/class/power_supply/max77818/device/fg_time_to_empty_hms\r\n");
    //     return FAILURE;
    // }
    // else
    // {
    //     fd  = open(buf, O_RDONLY);
    //     if (fd < 0) 
    //     {
    //         ERR("Can't open /sys/class/power_supply/max77818/device/fg_time_to_empty_hms\r\n");
    //         return fd;
    //     }
     
    //     read(fd, &ch, MAX_STATUS_LENGTH);

    //     value = atoi(ch);

    //     hours   = ((value & 0xFC00) >> 10) * 1.6;
    //     mins    = ((value & 0x03F0) >> 4) * 1.5;
    //     secs    = (value & 0x000F) * 5.625;

    //     //debug("TIME TO EMPTY %ih %im %is\r\n", hours, mins, secs);

    //     if(mins > 59)
    //     {
    //         mins = 59;
    //     }

    //     if(secs > 59)
    //     {
    //         secs = 59;
    //     }
    
    //     close(fd);
    // }
    /* закомментирован код для определения статуса аккумулятора с помощью fuel gauge
       требуется отладка и тестирование*/

    value = 1;

    snprintf(buf, sizeof(buf), "/sys/class/power_supply/%s/capacity_level", bat_name); 

    fd  = open(buf, O_RDONLY);
    if(fd < 0) 
    {
        ERR("Can't open /sys/class/power_supply/%s/capacity_level\r\n", bat_name);
        return fd;
    }

    read(fd, &ch, 9);

    if(memcmp(ch, full_status, 4) == 0)
    {
        charger_level   = 2;
        value           = 1;
        cnt_ch          = 0;
    }
    else if (memcmp(ch, normal_status, 6) == 0)
    {
        charge_low      = 0;
        charger_level   = 1;
        value           = 1;
        cnt_ch          = 0;
    }
    else
    {
        charger_level = 0;

        if(charger_present == 0)
        {
            if(cnt_ch == 0)
            {
                low_ch_time = time(NULL);
                cnt_ch = 1;
            }

            time_diff = time(NULL) - low_ch_time;

            value = (time_diff >= 600) ? 0 : 1;  // 10 min
        }
    }
    
    close(fd);
    
    if(charger_present == 1)
    {
        charge_low  = 0;
        value       = 1;
        cnt_ch      = 0;
    }
    return value;
}

int export_general_gpios()
{
    //change_dcam     = CHANGE_DCAM_VAL;

    sw_pwr_on_pin   = SW_PWR_ON_VAL;
    usb_cs_pin      = USB_CS_VAL;
    sdr_temp_pin    = SDR_TEMP;
    wakeup_en_pin   = WAKE_UP_EN_VAL;
    port_int_pin    = WAKE_UP_VAL;
    rf_pwr_on_pin	= RF_PWR_ON_VAL;

    gpio_export(rf_pwr_on_pin);                 //от радиоканала
    gpio_set_dir(rf_pwr_on_pin, 0);

    gpio_export(sw_pwr_on_pin);             // от движкового переключателя
    gpio_set_dir(sw_pwr_on_pin, 0);
    
    gpio_export(usb_cs_pin);                // usb chip select
    gpio_set_dir(usb_cs_pin, 1);
    gpio_set_value(usb_cs_pin, 0);

    gpio_export(sdr_temp_pin);              // от датчика температуры
    gpio_set_dir(sdr_temp_pin, 0);

    /*gpio_export(change_dcam);               // к мультиплексору, переключающему на одну из 2-х цифровых камер
    gpio_set_dir(change_dcam, 1);
    gpio_set_value(change_dcam, 0);    */

    usb_vbus_ok_pin = USB_ON_VAL;
    ext_chrg_on_pin   = EXT_CHRG_ON_VAL;
    if(!usb_vbus_ok_pin || !ext_chrg_on_pin)
    {
        ERR("Wrong GDO gpio numbers\r\n");
        return FAILURE;
    }

    gpio_export(usb_vbus_ok_pin);              // подключено usb
    gpio_set_dir(usb_vbus_ok_pin, 0);

    gpio_export(ext_chrg_on_pin);               // подключено внешнее питание
    gpio_set_dir(ext_chrg_on_pin, 0);

    //запрет пробудки
    gpio_export(wakeup_en_pin);
    gpio_set_dir(wakeup_en_pin, 1);
    gpio_set_value(wakeup_en_pin, 0);

    //прерывание с PORT EXPANDER дубли
    gpio_export(port_int_pin);
    gpio_set_dir(port_int_pin, 0);

    return SUCCESS;
}

//SD_CDn pin - GPIO78
int check_sd_card_inserted()
{
    int fd;
    off_t target;
    int bit;
    void *map_base, *virt_addr;
    unsigned long read_result;

    target = 0x49052038;//GPIO3_DATAIN reg addr
    bit = 14;

    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) 
        return -1;

    map_base = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, target & ~MAP_MASK);
    if(map_base == (void *) -1)
    {
        close(fd);
        return -1;
    }
    virt_addr = map_base + (target & MAP_MASK);
    read_result = *((unsigned long *) virt_addr);   
    munmap(map_base, MAP_SIZE);
    close(fd);
    return ((read_result &  (1<<bit)) == 0);
}

int check_sd_card()
{
    struct stat info;
    FILE       *pFile   = NULL;
    int         len;
    char        size[15];
    int         ret     = 0;
    int         i       = 0;

    sd_partitions_flag = 2;

    while(1)
    {
        // ищем sd-карту - первую по порядку
        ret = stat("/media/mmcblk0p1/", &info); // если на карте несколько разделов - ищем первый
        if((ret != 0) || (info.st_size <= 160))
        {
            sd_partitions_flag = 1;

            ret = stat("/media/mmcblk0/", &info);   // если не разбита на разделы
            if((ret != 0) || (info.st_size <= 160))
            {
                i++;
                sd_partitions_flag = 0;

                if(i > 10)  // если карта не определилась сразу - совершаем несколько попыток её обнаружения
                {
                    sd_status       = SD_STATUS_EMPTY;
                    sd_totalspace   = 0;
                    sd_freespace    = 0;
                    //ERR("SD card does not exist\r\n");
                    logEvent(log_REC_APL_SD_NO);
                    return FAILURE;
                }
            }
            else
            {
                break;
            }
        }
        else
        {
            break;
        }
        usleep(10000);
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
    }

    if(is_sd_mounted)
    {
        return SUCCESS;
    }

    i = 0;

    while(1)
    {
        if(sd_partitions_flag == 2)
        {
            pFile = fopen("/sys/block/mmcblk0/mmcblk0p1/size","r");
            //pFile = fopen("/sys/block/mmcblk0/mmcblk0p2/size","r");
        }
        else if(sd_partitions_flag == 1)
        {
            pFile = fopen("/sys/block/mmcblk0/size","r");
        }

        if(pFile != NULL)
        {
            break;
        }
        else
        {
            i++;
            if(i > 100)
            {
                sd_status = SD_STATUS_ERROR;
                ERR("SD card is not formatted\r\n");
                return FAILURE;
            }
        }
        usleep(10000);
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
    }

    // узнаем размер свободного места на карте
    len = fread(size, 1, 15, pFile);
    if (len <= 0)
    {
        sd_status = SD_STATUS_ERROR;
        ERR("SD card is not accessible\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    else if((len == 1) && (size[0] == '0'))
    {
        ERR("SD card is full\r\n");
        if(is_memory_full == 0)
        {
            is_memory_full  = 1;
            sd_status       = SD_STATUS_FULL;
            logEvent(log_REC_APL_SD_CARD_FULL);
        }
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(pFile != NULL)
    {
        fclose(pFile);
        pFile = NULL;
    }

    i = 0;
    
    while(1)
    {
        if(sd_partitions_flag == 2)
        {
            pFile = fopen("/dev/mmcblk0p1","r");
            // pFile = fopen("/dev/mmcblk0p2","r");
        }
        else if (sd_partitions_flag == 1)
        {
            pFile = fopen("/dev/mmcblk0","r");
        }

        if(pFile != NULL)
        {
            break;
        }
        else
        {
            i++;
            if(i > 80)
            {
                sd_status = SD_STATUS_ERROR;
                ERR("SD card device is breaked\r\n");
                return FAILURE;
            }
        }
        usleep(10000);
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);           // reset wdt
    }
    if(pFile != NULL)
    {
        fclose(pFile);
        pFile = NULL;
    }

    if(sd_partitions_flag == 2)
    {
        debug("MMCBLK0P1\r\n");
        system("/bin/mount /dev/mmcblk0p1 /media/card");
        //system("/bin/mount /dev/mmcblk0p2 /media/card/");
    }
    else if(sd_partitions_flag == 1)
    {
        debug("MMCBLK0\r\n");
        system("/bin/mount /dev/mmcblk0 /media/card/");
    }
    is_sd_mounted = 1;
    sd_totalspace = 0;
    if(CheckCardFreeSpace(NULL) < 0)
    {
        WARN("Memory is full\r\n");
    }

    // если на карте есть файлы для восстановления видео после ошибки - восстанавливаем
    ret = RepairSDCard("/media/card");

    if(ret < 0)
    {
        ret = remove("/media/card/new_video_file.mp4");
        if(ret !=0)
        {
            WARN("File new_video_file.mp4 cannot be deleted: %s\r\n", strerror(errno));
        }
        ret = remove("/media/card/new_video_file.temp");
        if(ret !=0)
        {
            WARN("File new_video_file.temp cannot be deleted: %s\r\n", strerror(errno));
        }
        sync();
        sd_status = SD_STATUS_ERROR;
        return FAILURE;
    }

    sd_status = SD_STATUS_READY;
    pthread_mutex_lock(&sd_mount_mutex);
    pthread_cond_broadcast(&sd_mount_cond);
    pthread_mutex_unlock(&sd_mount_mutex);
    return SUCCESS; 
}

void led_blink(int interval, int times)
{
	int i;

	for(i = times; i > 0; i--)
	{
	    system("/bin/echo 0 > /sys/devices/platform/leds-gpio/leds/led_stat/brightness");
	    usleep(interval);
	    system("/bin/echo 1 > /sys/devices/platform/leds-gpio/leds/led_stat/brightness");
	    usleep(interval);
	}
}


// индикация перед уходом в сон и полным отключением (сейчас не используется)
void final_indication()
{
	if((leds_always == 0) && (is_usb_on == 0))
    {
        system("/bin/echo 0 > /sys/devices/platform/leds-gpio/leds/led_stat/brightness");
    }
    else if(internal_error == 1)
    {
    	led_blink(100000, 5);
    }
    else if (is_usb_on == 0)
    {
        // debug("WOR_MODE\r\n");
        led_blink(300000, 2);
    }
    else
    {
        system("/bin/echo 1 > /sys/devices/platform/leds-gpio/leds/led_stat/brightness");
    }
}

void dump_var(u32 chrage_status_logged, u32 is_chrg_on_status)
{
#define PRINT_VAR(a)    printf(#a ": %i\n", a)
#define PRINT_VARc(a)   printf(#a ": %s\n", a)
#define PRINT_VARll(a)  printf(#a ": %lli\n", a)
#define VARDUMP_FILE    "/tmp/vardump"

    struct stat st;
    if(stat(VARDUMP_FILE, &st) != 0)
        return;
    remove(VARDUMP_FILE);

    PRINT_VAR(is_video_tx_started);
    PRINT_VAR(is_video_captured);
    PRINT_VAR(is_rec_started);
    PRINT_VAR(is_rec_on_cmd);
//    PRINT_VAR(is_rftx_started);
    PRINT_VAR(is_stream_started);
    PRINT_VAR(is_cap_started);
    PRINT_VAR(is_enc_started);
    PRINT_VAR(is_rec_finishing);
    //PRINT_VAR(is_rftx_finishing);
    PRINT_VAR(is_stream_finishing);
    PRINT_VAR(is_cap_finishing);
    PRINT_VAR(is_enc_finishing);
    //PRINT_VAR(is_rsz_started);
    PRINT_VAR(is_rec_failed);
    //PRINT_VAR(is_rftx_failed);
    PRINT_VAR(is_stream_failed);
    PRINT_VAR(is_memory_full);
    PRINT_VAR(is_sd_mounted);
    PRINT_VAR(is_usb_on);
    PRINT_VAR(start_rec);
    PRINT_VAR((int)currentCommand);
    PRINT_VAR((int)is_chrg_on_status);
    PRINT_VAR((int)chrage_status_logged);
    PRINT_VAR(radiocomm_sleeping);
    PRINT_VAR(rf_sleep_condition);
    PRINT_VAR(wifi_sleep_condition);
    PRINT_VAR(is_netconnect_on);
}

void process_rf_sleep_condition()
{
    if(!radiocomm_sleeping)
    {
        rf_sleep_condition = 0;
        return;
    }
    rf_sleep_condition = (uptime() - radiocomm_last_cmd_time > 10);
}

// формируем команду для управления основным автоматом программы
void change_command(Command* currCommand, int* start_rec_flag, u32  is_chrager_on, u32  charge_status_fixed) 
                                //, unsigned int* initMask, int* finish_app
{
    // if((is_rec_started == 1)&&(sound_only == 0)&&(video_failed == 1))
    // {
    //      if(is_rec_started == 1)
    //         {
    //             is_rec_request = 0;
    //         }
    //         else
    //         {
    //             gblSetCmd(FINISH);
    //             *(currCommand) = FINISH;
    //         }
    // }

    if((is_chrager_on == 0) || (charge_status_fixed == 1))
    {
    	charge_low = 1;
    	debug("Low charge level. Will shutdown now...\r\n");
    }

    if((wis_video_hw_started || wis_audio_hw_started) && !charge_low && !is_stream_request)
    {
        is_stream_request = 1;
    }
    else if (!wis_video_hw_started && !wis_audio_hw_started && is_stream_request)
    {
        is_stream_request = 0;
    }

    if((*start_rec_flag) && !is_memory_full && !charge_low && !is_sdcard_off_status)
    {
    	//*start_rec_flag = 0; //==
        is_rec_request 	= 1;
    }
    else
    {
        is_rec_request  = 0;
    }

    if(!is_sd_mounted || is_sdcard_off_status
        || ((gblGetQuit(REC_QID)) && !is_rec_failed && is_rec_started))
    {
        is_rec_request = 0; //rec_stop_cmd
    }

    if(charge_low && (is_rec_started || is_stream_started)) //|| is_rftx_started
    {
        is_rec_request      = 0;
//        is_rftx_request     = 0;
        is_stream_request   = 0;
    }

    if(is_stream_request && !is_stream_started && is_enc_started
        && !is_stream_finishing && !is_enc_finishing && !is_cap_finishing
        && !charge_low)
    {
        gblSetCmd(START_WIS);
        *(currCommand) = START_WIS;
    }

/*    if(is_rftx_request && is_cap_started  && !is_rftx_started
        && !is_rftx_finishing && !is_enc_finishing && !is_cap_finishing 
        && !charge_low)
    {
        gblSetCmd(START_RFTX);
        *(currCommand) = START_RFTX;
    }*/
    
    if(is_rec_request && !is_rec_started && is_enc_started && is_sd_mounted
        && !is_rec_finishing && !is_enc_finishing && !is_cap_finishing
        && !charge_low)
    {
        gblSetCmd(START_REC);
        *(currCommand) = START_REC;
    }

    if((is_stream_request || is_rec_request) && !is_enc_started && is_cap_started
        && !is_enc_finishing && !is_cap_finishing
        && !charge_low)
    {
        gblSetCmd(START_ENC);
        *(currCommand) = START_ENC;
    }

    if((is_stream_request || is_rec_request /*|| is_rftx_request*/) && (is_cap_started == 0)
        && !is_cap_finishing && !charge_low)
    {
        gblSetCmd(START_CAP);
        *(currCommand) = START_CAP;
    }

    if(((!is_stream_request && !is_rec_request /*&& !is_rftx_request*/) || is_cap_finishing)
        && is_cap_started && !is_enc_started && !is_rec_started // && !is_rftx_started 
        && !is_stream_started)
    {
        gblSetCmd(STOP_CAP);
        *(currCommand) = STOP_CAP;
    }

    if(((!is_stream_request && !is_rec_request) || is_enc_finishing || is_cap_finishing)
        && is_enc_started && !is_rec_started && !is_stream_started)
    {
        gblSetCmd(STOP_ENC);
        *(currCommand) = STOP_ENC;
    }

    if((!is_rec_request || is_rec_finishing || is_enc_finishing || is_cap_finishing)
        && is_rec_started)
    {
        gblSetCmd(STOP_REC);
        *(currCommand) = STOP_REC;
    }

/*    if((!is_rftx_request || is_rftx_finishing || is_enc_finishing || is_cap_finishing)
        && is_rftx_started)
    {
        gblSetCmd(STOP_RFTX);
        *(currCommand) = STOP_RFTX;
    }*/

    if((!is_stream_request || is_stream_finishing || is_enc_finishing || is_cap_finishing)
        && is_stream_started)
    {
        gblSetCmd(STOP_WIS);
        *(currCommand) = STOP_WIS;
    }

    // if(cnt_restart > 11)
    // {
    //     // gblSetCmd(FINISH);
    //     // *(currCommand) = FINISH;
    // }

    if(!is_cap_started && !is_enc_started && !is_rec_started && !is_rec_request //&& !is_rftx_started
    	&& !is_usb_on && wifi_sleep_condition && rf_sleep_condition
        && (*(currCommand) != FINISH)
        /*&&(!is_rec_failed || is_memory_full)*/)
    {
        gblSetCmd(SLEEP);
        *(currCommand) = SLEEP;
    }
}

int  poll_port_expander(u8 read_pe_anyway)
{
    u32 val = 1;
    int change_mask = 0;

    gpio_get_value(port_int_pin, &val);
    if(!val || read_pe_anyway)
    {//port expander interrupt active        
        change_mask = get_pe_change_mask();
        if(change_mask < 0)
            return 0;
        debug("port expander interrupt(ch.mask: 0x%.2x)\n", change_mask);

        if(change_mask == 0)
        {
            if(!read_pe_anyway)
                debug("strange: interrupt is active but change_mask is 0\n");
            return 0;
        }
        
        if(change_mask & PE_SW_PWR_ON)
        {
            debug("switch interrupt\n");
            // Вкл/выкл записи по движковому переключателю        
            gpio_get_value(sw_pwr_on_pin, (u32 *)&is_pwr_on);

            if(last_sw_pwr_on != is_pwr_on)
            {
                fix_record_state(is_pwr_on);
                start_rec = is_pwr_on;
            }
            last_sw_pwr_on = is_pwr_on;
        }

        if(change_mask & PE_USB_VBUS_OK)
        {
            int stat = get_usb_status();
            debug("usb_ok interrupt\n");            
            if(stat != FAILURE)
                is_usb_on = stat;
        }

        if(change_mask & PE_RF_PWR_ON)
        {
            u32 rf_wkup = 0;

            debug("rf interrupt\n");
            gpio_get_value(rf_pwr_on_pin, &rf_wkup);
            if(rf_wkup)
            {
                sem_post(&rfSem);//wakeup radiocomm
            }
        }

        if(change_mask & PE_BUT_PWR_ON)
        {
            debug("but pwr on interrupt\n");
        }
    }
    return 1;
}

int main(int argc, char *argv[])
{
    // список нитей, которые запускаются отсюда
//    pthread_t                   aewbThread;
    pthread_attr_t              attr;
   	struct sched_param          schedParam;
    Codec                      *videoEncoder;
    Codec                      *audioEncoder;

    int                         err                         = 0;
    int                         target_qid                  = 0;
    int                         i                           = 0;
    u32                         conv;
   	int                         numCapThreads;
    int                         numEncThreads;
    //int                         numRFTXDataThreads;
    void                       *ret;
    VideoStd_Type               captureStd                  = VideoStd_AUTO;    
    int                         status                      = SUCCESS;
    int                         iters                       = 0;
    int                         video_failed                = 0;
    int                         stop_rec_reset              = 0;
    struct timeval              start_thread_time;
    //struct stat                 netfileinfo;
    int                         sd_fails_cnt                = 0;
    // u32                         regValue;

    int                         prev_sdcard_status          = 1;
    u32                         is_chrg_on_status           = 0;
    u32                         chrage_status_logged        = 0;
    int                         timeout                     = 60; // сек
//    int 				        finish_app 					= 0;
    time_t                      start_tsens_time;
    time_t                      check_sd_time;

    samba_on                = 0;
    sd_partitions_flag      = -1;
    strm_error              = 0;
    is_sd_inserted          = 0;
    is_file_not_empty       = 0;
    charge_low              = 0;
    sd_freespace            = 0;
    sd_totalspace           = 0;
    sd_status               = SD_STATUS_EMPTY;

    stop_netconnect         = 1;//0xFF;
    wifi_sleep_condition    = 0;
    rf_sleep_condition      = 0;
    radiocomm_last_cmd_time = 0;
    radiocomm_sleeping      = 0;
    is_cam_failed           = 0;
    cam_channel_num 		= 0;
    charger_present 		= 0;
    charger_level           = 2;
    go_to_wor  				= 0;
    after_wake_up 			= 0;
    video_source_num        = 0;
    is_stream_request       = 0;
    is_rec_request          = 0;
    start_rec               = 0;
//    is_rftx_request         = 0;
    got_key_frame           = 0;
    deinterlace_on          = 1;//
    mp4_vol_size            = 0;
    cnt_tri                 = 0;
    cnt_sd                  = 0;
    cnt_snd                 = 0;
    led_on                  = 0;
    leds_always             = 1;
    color_video             = 1;
    digital_mic_gain       	= 0;
    analog_mic_gain1		= 50;
    analog_mic_gain2		= 50;
    cam_brightness          = 50;
    cam_contrast            = 50;
    cam_saturation          = 2;
    cam_voltage             = 12;
    init_step               = 1;
    is_video_tx_started     = 0;
    is_video_captured       = 0;
    is_rec_started          = 0;
    is_rec_on_cmd           = 0;
//    is_rftx_started         = 0;
    is_stream_started       = 0;
    is_cap_started          = 0;
    is_enc_started          = 0;
    is_rec_finishing        = 0;
    //is_rftx_finishing       = 0;
    is_stream_finishing     = 0;
    is_cap_finishing        = 0;
    is_enc_finishing        = 0;
    //is_rsz_started          = 0;
    is_rec_failed           = 0;
//    is_rftx_failed          = 0;
    is_stream_failed        = 0;
    is_memory_full          = 0;
    is_sd_mounted           = 0;
    last_rec_time           = 0;
    sound_only              = 0;
    need_to_create_log      = 0;
    is_access_point         = 0;
    sleep_on_radio          = 1;
    is_usb_on               = 0;
    internal_error          = 0;
    is_waked_from_rf        = 0;
    cnt_restart             = 1;
    actual_frame_width      = FRAME_CROP_WIDTH;
    frame_height            = 576;
    frames_per_hour         = 90000;
    framerate               = 25000;
    frames_per_sec          = (framerate / 1000);
    frames_per_sec_rsz      = 0;
    video_bitrate           = 2000000;
    audio_channels          = 1;
    device_addr             = 0xFFFFFFFF;
    half_vrate 				= 0;
    analog_mic_enable 		= 1;

    net_file_prev_time.tv_sec   = 0;
    net_file_prev_time.tv_nsec  = 0;

    last_sw_pwr_on          = 0;
    is_pwr_on               = 0;
    cnt_ch                  = 0;
    prev_temp_sens          = 0;
    is_usb_storage_started  = 0;

    sd_failed               = 0;
    hdmi_active             = 0;

    rsz_height              = 0;
    rsz_width               = 0;

    pairSync                = 2;


    // гасим все светодиоды, устанавливаем ограничение по току 
    system("/bin/echo 0 > /sys/devices/platform/leds-gpio/leds/led_bat/brightness");
    system("/bin/echo 0 > /sys/devices/platform/leds-gpio/leds/led_charge/brightness");
    system("/bin/echo 0 > /sys/devices/platform/leds-gpio/leds/led_stat/brightness");
    system("/bin/echo 1260 > /sys/devices/platform/omap/omap_i2c.2/i2c-2/2-0066/wireless_input_current_limit_mA");
    //pm settings
    system("echo 1 > /sys/kernel/debug/pm_debug/enable_off_mode");
    system("echo disabled > /sys/devices/platform/omap/omap_uart.0/power/wakeup");
    system("echo disabled > /sys/devices/platform/omap/omap_uart.1/power/wakeup");
    system("echo disabled > /sys/devices/platform/omap/omap_uart.2/power/wakeup");
    system("echo disabled > /sys/devices/platform/omap/omap_uart.3/power/wakeup");
    system("echo 1 > /sys/devices/platform/omap/omap_uart.0/sleep_timeout");
    system("echo 1 > /sys/devices/platform/omap/omap_uart.1/sleep_timeout");
    system("echo 1 > /sys/devices/platform/omap/omap_uart.2/sleep_timeout");
    system("echo 1 > /sys/devices/platform/omap/omap_uart.3/sleep_timeout");

    set_pe_interrupt_mask(PE_USB_VBUS_OK | PE_RF_PWR_ON | PE_SW_PWR_ON | PE_CHRG_INT | PE_BUT_PWR_ON);

    memset((void*)&fifoLogString[0], 0, 256);

    start_tsens_time    = time(NULL);
    check_sd_time       = time(NULL);

    gettimeofday((struct timeval *)&prev_time, NULL);
    gettimeofday((struct timeval *)&indicate_time, NULL);
    gettimeofday((struct timeval *)&indicate_time2, NULL); 
    gettimeofday((struct timeval *)&temp_time, NULL); 
    gettimeofday((struct timeval *)&temp_time2, NULL); 

    if(init_mutexes() != SUCCESS)
    {
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    if(export_general_gpios() < 0)
    {
        ERR("Failed to export GPIOs\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    prev_sdcard_status = is_sdcard_off_status;
    if(check_sd_card_inserted() <= 0) // check sd card presence
        is_sdcard_off_status = 1;
    else
        is_sdcard_off_status = 0;

    if(is_sdcard_off_status)
    {
        sd_failed       = 1;
        is_sd_mounted   = 0;
        sd_totalspace   = 0;
        sd_freespace    = 0;
        sd_status       = SD_STATUS_EMPTY;
        if(prev_sdcard_status == 0)
        {
            WARN("SD card isn't inserted\r\n");
            logEvent(log_REC_APL_SD_NO);
        }
    }
    else
    {
        if(check_sd_card() != SUCCESS)
        {
            WARN("Failed to open SD card(line: %i)\r\n", __LINE__);
        }
    }

    AVRecLoadSettings();

    if(is_sd_mounted == 1)
    {
        if(CheckCardFreeSpace(NULL) < 0)
        {
            WARN("Memory is full\r\n");
        }
    }

    //открываем файл watchdog таймера, теперь его надо периодически сбрасывать, иначе устройство перезагрузится
    fd_wdt = open("/dev/watchdog", O_WRONLY);
    if(fd_wdt < 0) 
    {
        ERR("Can't open watchdog file\r\n");
        cleanup(FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    ioctl(fd_wdt, WDIOC_SETTIMEOUT, &timeout); // setting time for wdt reset

    // если при вызове передали аргумент в виде числа, то это число рассматривается как количество неудачных 
    // перезапусков программы до этого
    conv = (argc > 1) ? strtol(argv[1], NULL, 10) : 0; 

#ifdef THREADLOG_EN // временный лог - доп. отладка
    system("/bin/mkdir /mnt/ramdisk");
    system("/bin/mount -t tmpfs -o size=320000k tmpfs /mnt/ramdisk");
    pFifoLogFile = fopen("/mnt/ramdisk/tmp_log", "w+");
    if (pFifoLogFile == NULL)
    {
        ERR("Cannot open fifo log file\r\n");
        return FAILURE;
    }
#endif

    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

    sem_init(&semaphore, 0, 0);
    sem_init(&rfSem, 0, 0);

    gblSetCmd(NO_COMMAND);
    currentCommand  = NO_COMMAND;

    if(conv > 1)
    {
    	WARN("RESTART WITH FAIL %lu times\r\n", conv - 1);
    	internal_error 		= 1;
    	cnt_restart    		= conv + 1;
    }

    gpio_get_value(sw_pwr_on_pin, (u32 *)&is_pwr_on);
    last_sw_pwr_on = is_pwr_on;
    is_usb_on = get_usb_status();            

    if(conv == 0)
    {
    	if(is_usb_on)
	    {
	        logEvent(log_SYSTEM_STARTUP_USB);
	    }
	    else
	    {
    		logEvent(log_SYSTEM_STARTUP_EXTERNAL);
    	}
    }

    initEventRingBuf();   
    init_thread_env();
    /* Set the priority of this whole process to max (requires root) */
    setpriority(PRIO_PROCESS, 0, -20);
    /* Initialize Codec Engine runtime */
    CERuntime_init();
    /* Initialize Davinci Multimedia Application Interface */
    Dmai_init();
    InitTransportBuffers();
    //numRFTXDataThreads  = 5;
    if(init_threads(&attr, &schedParam) != SUCCESS)
    {
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(FAILURE, REC_QID | STRM_QID | RFTX_QID);
    }

    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

    gblSetQuit(REC_QID | STRM_QID | RFTX_QID);

    start_rec = get_record_state();

    // if(video_source_num == 2)
    // {
    //     actual_frame_width  = 1264;
    //     frame_height        = 720;
    // }

    enc_width   = actual_frame_width;
    enc_height  = frame_height;

    // if(video_source_num == 2)
    // {
    //     captureStd  = VideoStd_720PR_30;
    // }
    // else
    // {
        captureStd  = VideoStd_D1_PAL;
    // }

    while(1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

        dump_var(chrage_status_logged, is_chrg_on_status);

        poll_port_expander(0);
        process_rf_sleep_condition();

    	usleep(16700);
    	iters++;
    	log_threads("(main)start_main_iter\r\n");
    	gettimeofday(&start_thread_time, NULL);

        if(time(NULL) - start_tsens_time >= 300)  	  // check sensor every 5 min.
        {
            start_tsens_time = time(NULL);
            check_temp_sensor();
        }
        
        prev_sdcard_status = is_sdcard_off_status;
        if(check_sd_card_inserted() <= 0) // check sd card presence
            is_sdcard_off_status = 1;
        else
            is_sdcard_off_status = 0;

        if(is_sdcard_off_status)
        {
            sd_failed       = 1;
            is_sd_mounted   = 0;
            sd_totalspace   = 0;
            sd_freespace    = 0;
            sd_status       = SD_STATUS_EMPTY;
            if(prev_sdcard_status == 0)
            {
                WARN("SD card isn't inserted\r\n");
                logEvent(log_REC_APL_SD_NO);
            }
        }
        else
        {
            if(time(NULL) - check_sd_time >= 3)           // check sensor every 3 sec.
            {
                check_sd_time = time(NULL);
                if(check_sd_card() != SUCCESS)
                {
                    WARN("Failed to open SD card(line: %i)\r\n", __LINE__);
                    is_sd_mounted   = 0;
                    sd_totalspace   = 0;
                    sd_freespace    = 0;
                    sd_status       = SD_STATUS_EMPTY;
                }
            }
        }        

        if(internal_error == 0)
        {
        	cnt_restart    = 1;
        }

        // if(is_stream_failed == 1)
        // {
        //     is_stream_started = 0;
        // }

        if(cnt_restart > 6)  // если запись видео заканчивалась неуспешно больше 6 раз - запускаем только аудио
        {
        	sound_only = 1;
        }

        currentCommand = gblGetCmd();

	    is_chrg_on_status   = check_charge_status();

	   	if(is_chrg_on_status == 0)
	    {
	        if(chrage_status_logged == 0)
	        {
	            logEvent(log_SYSTEM_DEADPOWER);
	            chrage_status_logged = 1;
	        }
	    }
        else
        {
            chrage_status_logged = 0;
        }

		change_command(&currentCommand, (int *)&start_rec, is_chrg_on_status, chrage_status_logged); //,&initMask,  (int *)&finish_app

        log_threads("(main)analyze cmd\r\n");
        if(currentCommand != NO_COMMAND)
        {
        	ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 
            if(currentCommand == SLEEP) // действия для перехода в сон
            {
                debug("Start sleeping\r\n");
                gpio_set_value(wakeup_en_pin, 1);//блокируем прерывание на пробудку
                if(poll_port_expander(1))//очищаем прерывание port_expander
                {
                    gpio_set_value(wakeup_en_pin, 0);
                    continue;//port_expander has some interrupt, don't go to sleep
                }

                logEvent(log_SYSTEM_SLEEP);

                if (initMask & LOGGINGTHREADCREATED)  // выключаем нить логгирования
                {
                    if(hRendezvousFinishLG != NULL)
                    {
                        Rendezvous_meet(hRendezvousFinishLG);
                    }
                    if(pthread_join(loggingThread, &ret) == 0) 
                    {
                        if(ret == THREAD_FAILURE) 
                        {
                            status = FAILURE;
                            ERR("Failed to join logging thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                        }
                        initMask = initMask & ~LOGGINGTHREADCREATED;
                    }

                    if(hRendezvousInitLG) 
                    {
                        Rendezvous_force(hRendezvousInitLG);
                    }
                    if(hRendezvousInitLG) 
                    {
                        Rendezvous_delete(hRendezvousInitLG);
                        hRendezvousInitLG = NULL;
                    }
                    if(hRendezvousFinishLG) 
                    {
                        Rendezvous_delete(hRendezvousFinishLG);
                        hRendezvousFinishLG = NULL;
                    }
                }

                if(initMask & NETCOMMTHREADCREATED)     // выключаем нить отвечающую за установку сети
                {
                    if(hRendezvousFinishNC)
                    {
                        Rendezvous_meet(hRendezvousFinishNC);
                    }

                    if(pthread_join(netCommThread, &ret) == 0) 
                    {
                        if(ret == THREAD_FAILURE) 
                        {
                            status = FAILURE;
                            ERR("Failed to join netcommunication thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                        }
                        initMask = initMask & ~NETCOMMTHREADCREATED;
                    }
                    if(hRendezvousFinishNC) 
                    {
                        Rendezvous_delete(hRendezvousFinishNC);
                        hRendezvousFinishNC = NULL;
                    }
                }

                is_sd_mounted   = 0;

                system("/bin/umount -v -f /media/card/");

                sd_status       = SD_STATUS_EMPTY;
                sd_totalspace   = 0;
                sd_freespace    = 0;

                system("/sbin/rmmod wlcore_sdio");  // выгружаем модуль wifi
                
                system("devmem2 0x48004E00 w 0x00000000 > /dev/null");//disable TV clock to allow dss go to off mode

                gpio_set_value(usb_cs_pin, 0);
                usleep(16700);

                sync();
                final_indication();
                log_threads("GO TO SLEEP\r\n");
                debug("GO TO SLEEP\r\n");
                
                gpio_set_dir(wakeup_en_pin, 0);//set input, разрешаем прерывание
                system("/bin/echo mem > /sys/power/state");
                // вcё - ушли в сон...
                //================================SLEEP================================
                // вышли из сна
                gpio_set_dir(wakeup_en_pin, 1);
                gpio_set_value(wakeup_en_pin, 0);

                // log_threads("WAKE UP\r\n");

                gblSetCmd(NO_COMMAND);

                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                if(!(initMask & LOGGINGTHREADCREATED))
                {
                    hRendezvousInitLG              = Rendezvous_create(pairSync, &rzvAttrs);
                    hRendezvousFinishLG            = Rendezvous_create(pairSync, &rzvAttrs);

                    loggingEnv.hRendezvousInit     = hRendezvousInitLG;
                    loggingEnv.hRendezvousFinishLG = hRendezvousFinishLG;

                    if (pthread_create(&loggingThread, &attr, loggingThrFxn, &loggingEnv)) 
                    {
                        ERR("Failed to create logging thread\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                    }
                    else
                    {
                        initMask |= LOGGINGTHREADCREATED;

                        // Wait loggingThread initialization
                        if(hRendezvousInitLG != NULL)
                        {
                            Rendezvous_meet(hRendezvousInitLG);
                        }
                    }
                }

                //enable TV clock
                system("devmem2 0x48004E00 w 0x00000004 > /dev/null");

                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

                system("/sbin/modprobe wlcore_sdio");

                usleep(1000000);

                prev_sdcard_status = is_sdcard_off_status;
                if(check_sd_card_inserted() <= 0) // check sd card presence
                    is_sdcard_off_status = 1;
                else
                    is_sdcard_off_status = 0;

                if(is_sdcard_off_status)
                {
                    sd_failed       = 1;
                    is_sd_mounted   = 0;
                    sd_totalspace   = 0;
                    sd_freespace    = 0;
                    sd_status       = SD_STATUS_EMPTY;
                    if(prev_sdcard_status == 0)
                    {
                        WARN("SD card isn't inserted\r\n");
                        logEvent(log_REC_APL_SD_NO);
                    }
                }
                else
                {
                    while(1)
                    {
                        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
                        if(check_sd_card() == SUCCESS)
                        {
                            sd_fails_cnt = 0;
                            break;
                        }
                        else
                        {
                            sd_fails_cnt++;
                            if(sd_fails_cnt >= 10)
                            {
                                sd_failed       = 1;
                                WARN("Failed to open SD card(line: %i)\r\n", __LINE__);
                                is_sd_mounted   = 0;
                                sd_totalspace   = 0;
                                sd_freespace    = 0;
                                sd_status       = SD_STATUS_EMPTY;
                                break;
                            }
                        }
                        usleep(10000);                 
                    }
                }

                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                if(!(initMask & NETCOMMTHREADCREATED))
                {
                    hRendezvousFinishNC            = Rendezvous_create(pairSync, &rzvAttrs);
                    netCommEnv.hRendezvousFinishNC = hRendezvousFinishNC;

                    if (pthread_create(&netCommThread, &attr, netCommThrFxn, &netCommEnv)) 
                    {
                        ERR("Failed to create netcommunication thread\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                    }
                    else
                    {
                        initMask |= NETCOMMTHREADCREATED;
                    }
                }                

                //======== reset if netsettings.txt was modified
                // для того, чтобы применить новые настройки, если они есть
                /*err = stat("/media/card/netsettings.txt", &netfileinfo);
                if(err != 0)
                {
                    WARN("Cannot get status info from netsettings.txt \r\n");
                }
                else
                {
                    if(netfileinfo.st_mtim.tv_sec > net_file_prev_time.tv_sec)
                    {
                        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
                        sync();
                        debug("Restart because of netsettings.txt modified...\r\n");
                        system("/sbin/init 6");
                    }
                }*/

                //========
                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                debug("WAKE UP DONE \r\n");
            }
            /*else if(currentCommand == START_USB_STORAGE)
            {
                if((is_usb_storage_started == 0) && (is_sd_mounted))
                {
                    debug("Start usb storage ...\r\n");
                    sync();
                    gpio_set_value(usb_cs_pin, 1);

                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

                    // save log file on the sd card
                    if(need_to_create_log == 1)
                    {
                        system("/bin/cp /opt/device.log /mnt/nand/");
                    }

                    usleep(3000000);

                    if(sd_partitions_flag == 1)
                    {
                        system("/bin/umount /dev/mmcblk0");
                    }
                    else if(sd_partitions_flag == 2)
                    {
                        system("/bin/umount /dev/mmcblk0p1");
                    }

                    system("/sbin/modprobe -r g_ether");

                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                    system("/sbin/modprobe musb_hdrc");
                    system("/sbin/modprobe musbhsdma");
                    system("/sbin/modprobe omap2430");

                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                    if(sd_partitions_flag == 1)
                    {
                        system("/sbin/modprobe g_file_storage file=/dev/mmcblk0 removable=1");
                    }
                    else if(sd_partitions_flag == 2)
                    {
                        system("/sbin/modprobe g_file_storage file=/dev/mmcblk0p1 removable=1");
                    }

                    usleep(3000000);
                    is_usb_storage_started = 1;
                }
            }

            else if(currentCommand == STOP_USB_STORAGE)
            {
                if( is_usb_storage_started == 1)
                {
                    debug("Stop usb storage ...\r\n");

                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

                    system("/sbin/modprobe -r g_file_storage");
                    system("/sbin/modprobe -r omap2430");
                    system("/sbin/modprobe -r musbhsdma");
                    system("/sbin/modprobe -r musb_hdrc"); 

                    if(sd_partitions_flag == 2)
                    {
                        system("/bin/mount /dev/mmcblk0p1 /media/mmcblk0p1");
                        usleep(3000000);
                        debug("MMCBLK0P1\r\n");
                        system("/bin/mount /dev/mmcblk0p1 /media/card");
                        //system("/bin/mount /dev/mmcblk0p2 /media/card/");
                    }
                    else if(sd_partitions_flag == 1)
                    {
                        system("/bin/mount /dev/mmcblk0 /media/mmcblk0");
                        usleep(3000000);
                        debug("MMCBLK0\r\n");
                        system("/bin/mount /dev/mmcblk0 /media/card/");
                    }
                    is_sd_mounted = 1;
                    sd_totalspace = 0;

                    usleep(3000000);

                    is_usb_storage_started = 0;
                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

                    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

                    if(CheckCardFreeSpace(NULL) < 0)
                    {
                        WARN("Memory is full\r\n");
                        is_rec_failed = 0;
                    }

                    sd_status = SD_STATUS_READY;

                    // if (!(initMask & RADIOCOMMTHREADCREATED) && (is_usb_on == 0))
                    // {
                    //  finish_app = 1;
                    // }
                }
            }*/

            else if(currentCommand == START_CAP)
            {
                /* Determine the number of threads for synchronization */
			    if(sound_only == 1)
                {
                    debug("SOUND_ONLY\r\n");
                    numCapThreads     = 2;
                }
                else
			    {
#ifdef SOUND_EN
			        numCapThreads     = (video_source_num == 2) ? 4 : 3;
#else // ifndef SOUND_EN
			        numCapThreads     = (video_source_num == 2) ? 3 : 2;
#endif // SOUND_EN
			    }

                if(is_cap_started == 0)
                {
                    // сбрасываем настройки media-ctl, т.к. при инициализации нити всегда происходит новая настройка
                    system("/usr/bin/media-ctl -r"); 
                    usleep(10000);
                    
                    debug("Start audio and video capture...\r\n");  

                    // сбрасываем флаги останова для тех нитей, которые сейчас будут запущены

                    if(is_stream_request)
                    {
                        target_qid = STRM_QID;
                    }
                    else if(is_rec_request)
                    {
                        target_qid      = REC_QID;
                        is_rec_on_cmd   = 1;
                    }
                    // else if(is_rftx_request)
                    // {
                    //     target_qid = RFTX_QID;
                    // }
                    
                    gblResetQuit(target_qid);          

                    hRendezvousCapture           = Rendezvous_create(pairSync, &rzvAttrs);
                    hRendezvousInitCap           = Rendezvous_create(numCapThreads, &rzvAttrs);
    				hRendezvousFinishCap         = Rendezvous_create(numCapThreads, &rzvAttrs);

    				if (hRendezvousInitCap == NULL || hRendezvousFinishCap  == NULL || 
    					hRendezvousCapture == NULL) 
				    {
				        ERR("Failed to create Rendezvous objects\r\n");
				        logEvent(log_REC_APL_INIT_FAILED);
                        internal_error = 1;
				    }

                    if(sound_only != 1)
                    {
                        /* Set the video capture thread priority */
                        schedParam.sched_priority = VCAPTURE_THREAD_PRIORITY;
                        if (pthread_attr_setschedparam(&attr, &schedParam)) 
                        {
                            ERR("Failed to set scheduler parameters\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        /* Create the video capture thread */
                        vCaptureEnv.hRendezvousCapture          = hRendezvousCapture;
                        vCaptureEnv.hRendezvousFrameFormat      = NULL;
                        vCaptureEnv.hRendezvousInit             = hRendezvousInitCap;
                        vCaptureEnv.hRendezvousAllCapFinished   = hRendezvousFinishCap;
                        vCaptureEnv.videoStd                    = captureStd;

                        if (!(initMask & VCAPTURETHREADCREATED))
                        {
                            if (pthread_create(&vCaptureThread, &attr, vCaptureThrFxn, &vCaptureEnv)) 
                            {
                                ERR("Failed to create video capture thread\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }
                        
                            initMask |= VCAPTURETHREADCREATED;
                        }

/*                        if(video_source_num == 2)   // digital camera
                        {
                            schedParam.sched_priority = AEWB_THREAD_PRIORITY;
                            if (pthread_attr_setschedparam(&attr, &schedParam)) 
                            {
                                ERR("Failed to set scheduler parameters\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }

                            aewbEnv.hRendezvousInit    = hRendezvousInitCap;
                            aewbEnv.hRendezvousCleanup = hRendezvousFinishCap;

                            if(!(initMask & AEWBTHREADCREATED))
                            {
                                if (pthread_create(&aewbThread, &attr, aewbThrFxn, &aewbEnv)) 
                                {
                                    ERR("Failed to create aewb thread\r\n");
                                    logEvent(log_REC_APL_INIT_FAILED);
                                    continue;
                                }
                            
                                initMask |= AEWBTHREADCREATED;
                            }
                        }*/
                    }
#ifdef SOUND_EN
                    /* Set the audio capture thread priority */
                    schedParam.sched_priority = ACAPTURE_THREAD_PRIORITY;
                    if (pthread_attr_setschedparam(&attr, &schedParam)) 
                    {
                        ERR("Failed to set scheduler parameters\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                        continue;
                    }

                    /* Create the audio capture thread */
                    aCaptureEnv.hRendezvousCapture          = hRendezvousCapture;
                    aCaptureEnv.hRendezvousInit             = hRendezvousInitCap;
                    aCaptureEnv.hRendezvousAllCapFinished   = hRendezvousFinishCap;

                    if (!(initMask & ACAPTURETHREADCREATED))
                    {
                        if (pthread_create(&aCaptureThread, &attr, aCaptureThrFxn, &aCaptureEnv)) 
                        {
                            ERR("Failed to create audio capture thread\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        initMask |= ACAPTURETHREADCREATED;
                    }
#endif //SOUND_EN
                    /* Проверяем, все ли нити группы захвата проинициализированы */
                    if(hRendezvousInitCap != NULL)
                    {
                    	Rendezvous_meet(hRendezvousInitCap);
                    }

                    is_cap_started  = 1;
                    debug("[%llu] STARTED AUDIO AND VIDEO CAPTURE\r\n", time_in_us());
                }
            }

            else if(currentCommand == START_ENC)
            {
                /* Determine the number of threads for synchronization */
                if(sound_only == 1)
                {
                    debug("SOUND_ONLY\r\n");
                    numEncThreads     = 2;
                }
                else
                {
#ifdef SOUND_EN
                    numEncThreads     = 3;
#else // ifndef SOUND_EN
                    numEncThreads     = 2;
#endif // SOUND_EN
                }

                if(is_enc_started == 0)
                {
                    debug("Start audio and video encoder...\r\n");     

                    // сбрасываем флаги останова для тех нитей, которые сейчас будут запущены

                    if(is_stream_request)
                    {
                        target_qid = STRM_QID;
                    }
                    else if(is_rec_request)
                    {
                        target_qid = REC_QID;
                    }
                    
                    gblResetQuit(target_qid);          

                    hRendezvousInitEnc           = Rendezvous_create(numEncThreads, &rzvAttrs);
                    hRendezvousFinishEnc         = Rendezvous_create(numEncThreads, &rzvAttrs);

                    if (hRendezvousInitEnc == NULL || hRendezvousFinishEnc  == NULL) 
                    {
                        ERR("Failed to create Rendezvous objects\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                        internal_error = 1;
                    }

                    if(sound_only != 1)
                    {
                        /* Set the video thread priority */
                        schedParam.sched_priority = VIDEO_THREAD_PRIORITY;
                        if (pthread_attr_setschedparam(&attr, &schedParam)) 
                        {
                            ERR("Failed to set scheduler parameters\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        videoEncoder = engine->videoEncoders;

                        /* Create the video thread */
                        videoEnv.hRendezvousInit            = hRendezvousInitEnc;
                        videoEnv.hRendezvousAllEncFinished  = hRendezvousFinishEnc;
                        videoEnv.hRendezvousFrameFormat     = NULL;
                        videoEnv.videoEncoder               = videoEncoder->codecName;
                        videoEnv.params                     = videoEncoder->params;
                        videoEnv.dynParams                  = videoEncoder->dynParams;
                        videoEnv.engineName                 = engine->engineName;
                        videoEnv.encEngineLock              = &encEngineLock;
                        videoEnv.src_bgroup_num             = eVCAPTURE_SRC;
                        videoEnv.videoStd                   = captureStd;

                        if (!(initMask & VIDEOTHREADCREATED))
                        {
                            if (pthread_create(&videoThread, &attr, videoThrFxn, &videoEnv)) 
                            {
                                ERR("Failed to create video thread\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }

                            initMask |= VIDEOTHREADCREATED;
                        }
                    }
#ifdef SOUND_EN
                    /* Set the thread priority */
                    schedParam.sched_priority = AUDIO_THREAD_PRIORITY;
                    if (pthread_attr_setschedparam(&attr, &schedParam)) 
                    {
                        ERR("Failed to set scheduler parameters\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                        continue;
                    }

                    audioEncoder = engine->audioEncoders;

                    /* Create the audio thread */
                    audioEnv.hRendezvousInit            = hRendezvousInitEnc;
                    audioEnv.hRendezvousAllEncFinished  = hRendezvousFinishEnc;
                    audioEnv.audioEncoder               = audioEncoder->codecName;
                    audioEnv.engineName                 = engine->engineName;
                    audioEnv.encEngineLock              = &encEngineLock;

                    if (!(initMask & AUDIOTHREADCREATED))
                    {
                        if (pthread_create(&audioThread, &attr, audioThrFxn, &audioEnv)) 
                        {
                            ERR("Failed to create audio thread\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        initMask |= AUDIOTHREADCREATED;
                    }

                    /*
                     * Wait for the codec to be created in the video and audio threads before
                     * launching the packer thread (otherwise we don't know which size
                     * of buffers to use).
                     */
#endif // SOUND_EN
                    /* Signal that initialization is done and wait for other threads */
                    if(hRendezvousInitEnc != NULL)
                    {
                        Rendezvous_meet(hRendezvousInitEnc);
                    }

                    is_enc_started  = 1;
                    debug("[%llu] STARTED AUDIO AND VIDEO ENCODER\r\n", time_in_us());

                    wis_hw_started = 1; // used in live/appro/wis-streamer.cpp
                }
            }

            else if(currentCommand == START_REC)
            {
                is_rec_on_cmd   = 1;            	
            	is_rec_failed 	= 0;

                if(is_rec_started == 0)
                {
                    gblResetQuit(REC_QID);

                    //debug("Start recording ...\r\n");
                    hRendezvousInitVR           = Rendezvous_create(pairSync, &rzvAttrs);
    				hRendezvousFinishVR         = Rendezvous_create(pairSync, &rzvAttrs);

    				if (hRendezvousInitVR    == NULL  || hRendezvousFinishVR  == NULL) 
				    {
				        ERR("Failed to create Rendezvous objects\r\n");
				        logEvent(log_REC_APL_INIT_FAILED);
                        internal_error = 1;
				    }

                    /* Set the packer thread priority */
                    schedParam.sched_priority = PACKER_THREAD_PRIORITY;
                    if (pthread_attr_setschedparam(&attr, &schedParam)) 
                    {
                        ERR("Failed to set scheduler parameters\r\n");
                        logEvent(log_REC_APL_INIT_FAILED);
                        continue;
                    }

                    /* Create the packer thread */
                    packerEnv.hRendezvousInit           = hRendezvousInitVR;
                    packerEnv.hRendezvousAllVRFinished  = hRendezvousFinishVR;

                    if (!(initMask & PACKERTHREADCREATED))
                    {
                        if (pthread_create(&packerThread, &attr, packerThrFxn, &packerEnv)) 
                        {
                            ERR("Failed to create packer thread\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        initMask |= PACKERTHREADCREATED;
                    }
                    /* Signal that initialization is done */
                    if(hRendezvousInitVR != NULL)
                    {
                    	Rendezvous_meet(hRendezvousInitVR);
                    }

                    is_rec_started  = 1;
                    
                    logEvent(log_REC_APL_REC_STARTED);
                    debug("STARTED RECORDING\r\n");
                }
            }

/*            else if(currentCommand == START_RFTX)
            {
                is_rftx_failed = 0;

                if(is_rftx_started == 0)
                {
                    gblResetQuit(RFTX_QID);
                    frames_per_sec_rsz     = 5; // отправляем пока 5 кадров в секунду, если больше, то проблемы

                    if(sound_only != 1) 
                    {
                        // Set the resizer thread priority 
                        schedParam.sched_priority = RESIZE_THREAD_PRIORITY;
                        if (pthread_attr_setschedparam(&attr, &schedParam)) 
                        {
                            ERR("Failed to set scheduler parameters\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        // Create the resizer thread 
                        resizeEnv.hRendezvousInit           = hRendezvousInitRFTX;
                        resizeEnv.hRendezvousAllRTFinished  = hRendezvousFinishRFTX;

                        if (!(initMask & RESIZETHREADCREATED))
                        {
                            // ресайзер настроен на уменьшение размера кадров
                            if (pthread_create(&resizeThread, &attr, resizeThrFxn, &resizeEnv)) 
                            {
                                ERR("Failed to create resize thread\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }

                            initMask |= RESIZETHREADCREATED;
                        }

                        // Set the resized video thread priority 
                        schedParam.sched_priority = RSZVIDEO_THREAD_PRIORITY;
                        if (pthread_attr_setschedparam(&attr, &schedParam)) 
                        {
                            ERR("Failed to set scheduler parameters\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        videoEncoder = engine->videoEncoders;

                        // Create the resized video thread 
                        rszVideoEnv.hRendezvousInit            = hRendezvousInitRFTX;
                        rszVideoEnv.hRendezvousAllEncFinished  = hRendezvousFinishRFTX;
                        rszVideoEnv.hRendezvousFrameFormat     = NULL;
                        rszVideoEnv.videoEncoder               = videoEncoder->codecName;
                        rszVideoEnv.params                     = videoEncoder->params;
                        rszVideoEnv.dynParams                  = videoEncoder->dynParams;
                        rszVideoEnv.engineName                 = engine->engineName;
                        rszVideoEnv.encEngineLock              = &encEngineLock;
                        rszVideoEnv.src_bgroup_num             = eVRESIZER_SRC;

                        if (!(initMask & RSZVIDEOTHREADCREATED))
                        {
                            // для сжатия уменьшенных кадров
                            if (pthread_create(&rszVideoThread, &attr, videoThrFxn, &rszVideoEnv)) 
                            {
                                ERR("Failed to create resized video thread\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }

                            initMask |= RSZVIDEOTHREADCREATED;
                        }

                        //  Set the radio data transmitter thread priority 
                        schedParam.sched_priority = RADIODATATX_THREAD_PRIORITY;
                        if (pthread_attr_setschedparam(&attr, &schedParam)) 
                        {
                            ERR("Failed to set scheduler parameters\r\n");
                            logEvent(log_REC_APL_INIT_FAILED);
                            continue;
                        }

                        // Create the radio data transmitter thread 
                        
                        radioDataTXEnv.hRendezvousInit           = hRendezvousInitRFTX;
                        radioDataTXEnv.hRendezvousAllRTFinished  = hRendezvousFinishRFTX;

                        if (!(initMask & RADIODATATXTHREADCREATED))
                        {
                            // для передачи данных по радиоканалу на визир
                            if (pthread_create(&radioDataTXThread, &attr, radioDataTXThrFxn, &radioDataTXEnv)) 
                            {
                                ERR("Failed to create radio data transmitter thread\r\n");
                                logEvent(log_REC_APL_INIT_FAILED);
                                continue;
                            }

                            initMask |= RADIODATATXTHREADCREATED;
                        }
                    }
                    if(hRendezvousInitRFTX != NULL)
                    {
                    	Rendezvous_meet(hRendezvousInitRFTX);
                    }

                    is_rftx_started = 1;

                    pthread_mutex_lock(&rftx_mutex);
                    pthread_cond_broadcast(&rftx_cond);
                    pthread_mutex_unlock(&rftx_mutex);

                    // logEvent(log_REC_APL_REC_STARTED);
                    debug("[%llu] STARTED RADIOTRANSLATION\r\n", time_in_us());
                }
            }*/

            else if(currentCommand == START_WIS)
            {
                is_stream_failed = 0;
                
                if(is_stream_started == 0)
                {
                    gblResetQuit(STRM_QID);

                    is_stream_failed = 0;

                    schedParam.sched_priority = STREAMBUFFS_THREAD_PRIORITY;
                    if (pthread_attr_setschedparam(&attr, &schedParam)) 
                    {
                        ERR("Failed to set scheduler parameters\r\n");
                        internal_error = 1;
                    }

                    streamBuffsEnv.hRendezvousFinishSTRM = hRendezvousFinishSTRM;

                    if (pthread_create(&streamBuffsThread, &attr, StreamBuffsThrFxn, &streamBuffsEnv)) 
                    {
                        ERR("Failed to create streaming thread\r\n");
                        internal_error = 1;
                    }
                    
                    initMask |= STREAMBUFFSTHREADCREATED;

                    is_stream_started = 1;
                    debug("[%llu] STARTED NETWORK STREAMING\r\n", time_in_us());
                }

            }

            else if(currentCommand == STOP_CAP)
            {
                if(is_cap_started == 1)
                {
                    debug("Stop audio and video capture...\r\n");
                    
                    gblSetQuit(REC_QID | STRM_QID | RFTX_QID);

                    /* Make sure the other threads aren't waiting for us */
                    if(hRendezvousFinishCap != NULL)
                    {
                    	Rendezvous_meet(hRendezvousFinishCap);
                    }

                    if ((initMask & VCAPTURETHREADCREATED) && (pthread_join(vCaptureThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join video capture thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rec_failed       = 1;
                            //is_rftx_failed      = 1;
                            is_stream_failed    = 1;
                        }
                        initMask = initMask & ~VCAPTURETHREADCREATED;
                    }

                    // if ((initMask & AEWBTHREADCREATED) && (pthread_join(aewbThread, &ret) == 0))
                    // {
                    //     if (ret == THREAD_FAILURE) 
                    //     {
                    //         ERR("Failed to join aewb thread\r\n");
                    //         logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                    //         is_rec_failed       = 1;
                    //         is_rftx_failed      = 1;
                    //         is_stream_failed    = 1;
                    //     }
                    //     initMask = initMask & ~AEWBTHREADCREATED;
                    // }

                    if ((initMask & ACAPTURETHREADCREATED) && (pthread_join(aCaptureThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join audio capture thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rec_failed       = 1;
                            //is_rftx_failed      = 1;
                            is_stream_failed    = 1;
                        }
                        initMask = initMask & ~ACAPTURETHREADCREATED;
                    }

                    if(hRendezvousCapture)
                    {
                        Rendezvous_force(hRendezvousCapture);
                    }
                    if(hRendezvousCapture) 
                    {
                        Rendezvous_delete(hRendezvousCapture);
                        hRendezvousCapture = NULL;
                    }

				    if (hRendezvousInitCap)
				    {
				        Rendezvous_force(hRendezvousInitCap);
				    }

				    if (hRendezvousInitCap) 
				    {
				        Rendezvous_delete(hRendezvousInitCap);
				        hRendezvousInitCap = NULL;
				    }

				    if (hRendezvousFinishCap) 
				    {
				        Rendezvous_delete(hRendezvousFinishCap);
				        hRendezvousFinishCap = NULL;
				    }

                    is_cap_started      = 0;

/*закомментирован механизм перезагрузки приложения после останова захвата каждый раз */
//                     // if (!(initMask & RADIOCOMMTHREADCREATED))
//                     // {
//                     // 	finish_app = 1;
//                     //	if (is_usb_on == 0)
// 		            //    {
// 		            //    	status = SUCCESS;
// 		            //    }
// 		            //    else
// 		            //    {
// 				    //		status = RESTART;
// 		            //    }
//                     // }
//                     // else
// 	                // {
// 			    		 status = RESTART;
// 	                // }

// 	                // if ((is_chrg_on_status == 0) || (chrage_status_logged == 1))
// 	                // {
// 	                // 	status = SUCCESS;
// 	                // }
// 	                if(((is_rec_failed == 1)||(is_rftx_failed == 1)||(is_stream_failed == 1))
//                         &&(is_memory_full == 0))
// 	                {
// 	                	cnt_restart++;
// 	                }

                    // CMEM_exit();

                    // system("rmmod cmemk 2>/dev/null");

                    // usleep(1000000);

                    // system("modprobe cmemk phys_start=0x88000000 phys_end=0x90000000 allowOverlap=1 useHeapIfPoolUnavailable=1");


                    // if(CMEM_init() == -1)
                    // {
                    //     ERR("Failed to initialize CMEM\r\n");
                    // }

//  	                gblSetCmd(FINISH);
//                     currentCommand = FINISH;

//                     ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

// 				    if(status == SUCCESS)
// 				    {
// 				    	logEvent(log_SYSTEM_NORMAL_POWEROFF);
// 				    }
// 				    else
// 				    {
// 				    	logEvent(log_NO_EVENT);
// 				    }

//                     ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

// 	                if (initMask & RADIOCOMMTHREADCREATED) 
// 	                {
// 	                	Rendezvous_meet(hRendezvousFinishRC);
// 	                }
                    
//                     ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

// 	                if (initMask & LOGGINGTHREADCREATED) 
// 	                {
// 	                	Rendezvous_meet(hRendezvousFinishLG);
// 	            	}

// 	            	stop_rec_reset = 1;
/*закомментирован механизм перезагрузки приложения после останова захвата каждый раз */

                    debug("STOPPED AUDIO AND VIDEO CAPTURE\r\n");

                    is_cap_finishing    = 0;
                }
            }

            else if(currentCommand == STOP_ENC)
            {
                if(is_enc_started == 1)
                {
                    debug("Stop audio and video encoder ...\r\n");
                    
                    gblSetQuit(REC_QID | STRM_QID);

                    /* Make sure the other threads aren't waiting for us */
                    if(hRendezvousFinishEnc != NULL)
                    {
                        Rendezvous_meet(hRendezvousFinishEnc);
                    }

                    if ((initMask & VIDEOTHREADCREATED) && (pthread_join(videoThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join video thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rec_failed       = 1;
                            is_stream_failed    = 1;
                        }
                        initMask = initMask & ~VIDEOTHREADCREATED;
                    }

                    if ((initMask & AUDIOTHREADCREATED) && (pthread_join(audioThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join audio thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rec_failed       = 1;
                            is_stream_failed    = 1;
                        }
                        initMask = initMask & ~AUDIOTHREADCREATED;
                    }

                    if (hRendezvousInitEnc)
                    {
                        Rendezvous_force(hRendezvousInitEnc);
                    }

                    if (hRendezvousInitEnc) 
                    {
                        Rendezvous_delete(hRendezvousInitEnc);
                        hRendezvousInitEnc      = NULL;
                    }

                    if (hRendezvousFinishEnc) 
                    {
                        Rendezvous_delete(hRendezvousFinishEnc);
                        hRendezvousFinishEnc    = NULL;
                    }

                    is_enc_started    = 0;

                    debug("STOPPED AUDIO AND VIDEO ENCODER\r\n");
                    is_enc_finishing  = 0;
                }
            }

            else if(currentCommand == STOP_REC)
            {
            	is_rec_on_cmd = 0;

                if(is_rec_started == 1)
                {
                    debug("Stop recording ...\r\n");
                    
                    gblSetQuit(REC_QID);

                    if(hRendezvousFinishVR != NULL)
                    {
                    	Rendezvous_meet(hRendezvousFinishVR);
                    }

                    logEvent(log_REC_APL_REC_STOPPED);
    				
    				if ((initMask & PACKERTHREADCREATED) && (pthread_join(packerThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join packer thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rec_failed = 1;
                        }
                        initMask = initMask & ~PACKERTHREADCREATED;
                    }

				    if (hRendezvousInitVR)
				    {
				        Rendezvous_force(hRendezvousInitVR);
				    }

				    if (hRendezvousInitVR) 
				    {
				        Rendezvous_delete(hRendezvousInitVR);
				        hRendezvousInitVR = NULL;
				    }

				    if (hRendezvousFinishVR) 
				    {
				        Rendezvous_delete(hRendezvousFinishVR);
				        hRendezvousFinishVR = NULL;
				    }

                    is_rec_started      = 0;

	                debug("STOPPED RECORDING\r\n");

                    is_rec_finishing    = 0;
                }
            }

/*            else if(currentCommand == STOP_RFTX)
            {
                if(is_rftx_started == 1)
                {
                    gblSetQuit(RFTX_QID);

                    sem_post(&txDataSem);

                    debug("Stop radiotranslation ...\r\n");
                    if(hRendezvousFinishRFTX != NULL)
                    {
                    	Rendezvous_meet(hRendezvousFinishRFTX);
                    }

                    //logEvent(log_REC_APL_REC_STOPPED);

                    if ((initMask & RESIZETHREADCREATED) && (pthread_join(resizeThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join resize thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rftx_failed = 1;
                        }
                        initMask = initMask & ~RESIZETHREADCREATED;
                    }

                    if ((initMask & RSZVIDEOTHREADCREATED) && (pthread_join(rszVideoThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join resized video thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rftx_failed = 1;
                        }
                        initMask = initMask & ~RSZVIDEOTHREADCREATED;
                    }

                    if ((initMask & RADIODATATXTHREADCREATED) && (pthread_join(radioDataTXThread, &ret) == 0))
                    {
                        if (ret == THREAD_FAILURE) 
                        {
                            ERR("Failed to join radio data transmitter thread\r\n");
                            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
                            is_rftx_failed = 1;
                        }
                        initMask = initMask & ~RADIODATATXTHREADCREATED;
                    }

                    if (hRendezvousInitRFTX)
                    {
                        Rendezvous_force(hRendezvousInitRFTX);
                    }

                    if (hRendezvousInitRFTX) 
                    {
                        Rendezvous_delete(hRendezvousInitRFTX);
                        hRendezvousInitRFTX = NULL;
                    }

                    if (hRendezvousFinishRFTX) 
                    {
                        Rendezvous_delete(hRendezvousFinishRFTX);
                        hRendezvousFinishRFTX = NULL;
                    }

                    debug("STOPPED RADIOTRANSLATION\r\n");

                    is_rftx_started     = 0;
                    is_rftx_finishing   = 0;
                }
            }*/

            else if(currentCommand == STOP_WIS)
            {
                if(is_stream_started == 1)
                {
                    gblSetQuit(STRM_QID);

                    debug("Stop network streaming ...\r\n");

                    if (initMask & STREAMBUFFSTHREADCREATED)
                    {
                        if(hRendezvousFinishSTRM != NULL)
                        {
                            Rendezvous_meet(hRendezvousFinishSTRM);
                        }

                        if (pthread_join(streamBuffsThread, &ret) == 0)
                        {
                            if (ret == THREAD_FAILURE) 
                            {
                                status = FAILURE;
                            }

                            is_stream_started   = 0;

                            debug("STOPPED NETWORK STREAMING\r\n");

                            initMask = initMask & ~STREAMBUFFSTHREADCREATED;

                            is_stream_finishing = 0;
                        }
                    }
                }
            }

            else if(currentCommand == FINISH)
            {
                debug("FINISH received...\r\n");

                logEvent(log_SYSTEM_NORMAL_POWEROFF);

                /*if(initMask & RADIOCOMMTHREADCREATED) 
                {
                	Rendezvous_meet(hRendezvousFinishRC);
                }*/
                if(initMask & LOGGINGTHREADCREATED) 
                {
                	Rendezvous_meet(hRendezvousFinishLG);
            	}
                if(initMask & NETCOMMTHREADCREATED) 
                {
                    Rendezvous_meet(hRendezvousFinishNC);
                }
                cleanup(SUCCESS, REC_QID | STRM_QID | RFTX_QID);
            }
            else
            {
                debug("DEFAULT %i\r\n", currentCommand);
            }
        }

        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL); 

        // механизм свертывания всех нитей, связанных с сетью, при отключении wifi и их создании при включении wifi
        // на данный момент вносит значительную задержку в работу программы - темп обработки кадров не сохраняется

        // if((stop_netconnect == 1) && (initMask & NETCOMMTHREADCREATED))
        // {
        //     if(hRendezvousFinishNC)
        //     {
        //         Rendezvous_meet(hRendezvousFinishNC);
        //     }

        //     if(pthread_join(netCommThread, &ret) == 0) 
        //     {
        //         if(ret == THREAD_FAILURE) 
        //         {
        //             status = FAILURE;
        //             ERR("Failed to join netcommunication thread\r\n");
        //             logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
        //         }
        //         initMask = initMask & ~NETCOMMTHREADCREATED;
        //     }

        //     if(hRendezvousFinishNC) 
        //     {
        //         Rendezvous_delete(hRendezvousFinishNC);
        //         hRendezvousFinishNC = NULL;
        //     }
        // }
        // else if((stop_netconnect == 0) && (!(initMask & NETCOMMTHREADCREATED)))
        // {
        //     hRendezvousFinishNC            = Rendezvous_create(pairSync, &rzvAttrs);
        //     netCommEnv.hRendezvousFinishNC = hRendezvousFinishNC;

        //     if (pthread_create(&netCommThread, &attr, netCommThrFxn, &netCommEnv)) 
        //     {
        //         ERR("Failed to create netcommunication thread\r\n");
        //         logEvent(log_REC_APL_INIT_FAILED);
        //     }
        //     else
        //     {
        //         initMask |= NETCOMMTHREADCREATED;
        //     }
        // }
    }

cleanup:

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    debug("Exiting application...\r\n");

#ifdef THREADLOG_EN
    log_threads("Exit app\r\n");
    fclose(pFifoLogFile);
    pFifoLogFile = NULL;
    system("/bin/cp /mnt/ramdisk/tmp_log /opt/");
#endif

    /* Make sure the other threads aren't waiting for init to complete */
    if(hRendezvousInitVR)
    {
        Rendezvous_force(hRendezvousInitVR);
    }
/*    if(hRendezvousInitRFTX)
    {
        Rendezvous_force(hRendezvousInitRFTX);
    }*/
    if(hRendezvousInitRC)
    {
        Rendezvous_force(hRendezvousInitRC);    
    }
    if(hRendezvousInitLG) 
    {
        Rendezvous_force(hRendezvousInitLG);
    }

    if(initMask & STREAMBUFFSTHREADCREATED)
    {
        if(hRendezvousFinishSTRM)
        {
            Rendezvous_force(hRendezvousFinishSTRM);
        }

    	if(pthread_join(streamBuffsThread, &ret) == 0)
    	{
	        if(ret == THREAD_FAILURE)
	        {
	            status = FAILURE;
	        }
	    }
    }

    if(initMask & RADIOCOMMTHREADCREATED)
    {
        sem_wait(&rfSem);
        if(pthread_join(radioCommThread, &ret) == 0) 
        {
            if(ret == THREAD_FAILURE) 
            {
                status = FAILURE;
            }
        }
    }

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(initMask & LOGGINGTHREADCREATED) 
    {
        if(pthread_join(loggingThread, &ret) == 0) 
        {
            if(ret == THREAD_FAILURE) 
            {
                status = FAILURE;
            }
        }
    }

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(initMask & INDICATIONTHREADCREATED) 
    {
        if(hRendezvousFinishI)
        {
            Rendezvous_meet(hRendezvousFinishI);
        }

        if(pthread_join(indicationThread, &ret) == 0) 
        {
            if(ret == THREAD_FAILURE) 
            {
                status = FAILURE;
            }
        }
    }
    
    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    // останавливаем самба-сервер, который поднимается в netCommThread нити
    system("/usr/bin/killall -s 2 smbd");
    usleep(2000000);
    system("/usr/bin/killall -s 2 nmbd");
    usleep(2000000);
    system("/usr/bin/killall -s 9 smbd");
    system("/usr/bin/killall -s 9 nmbd");
    usleep(1000000);

    if(initMask & NETCOMMTHREADCREATED) 
    {
        if(pthread_join(netCommThread, &ret) == 0) 
        {
            if (ret == THREAD_FAILURE) 
            {
                status = FAILURE;
            }
        }
    }
    
    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(hRendezvousInitVR) 
    {
        Rendezvous_delete(hRendezvousInitVR);
        hRendezvousInitVR = NULL;
    }

/*    if(hRendezvousInitRFTX) 
    {
        Rendezvous_delete(hRendezvousInitRFTX);
        hRendezvousInitRFTX = NULL;
    }*/

    if(hRendezvousInitRC) 
    {
        Rendezvous_delete(hRendezvousInitRC);
        hRendezvousInitRC = NULL;
    }

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(hRendezvousInitLG) 
    {
        Rendezvous_delete(hRendezvousInitLG);
        hRendezvousInitLG = NULL;
    }

    if(hRendezvousInitI) 
    {
        Rendezvous_delete(hRendezvousInitI);
        hRendezvousInitI = NULL;
    }

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(hRendezvousFinishLG) 
    {
        Rendezvous_delete(hRendezvousFinishLG);
        hRendezvousFinishLG = NULL;
    }

    if(hRendezvousFinishI) 
    {
        Rendezvous_delete(hRendezvousFinishI);
        hRendezvousFinishI = NULL;
    }

    if(hRendezvousFinishVR) 
    {
        Rendezvous_delete(hRendezvousFinishVR);
        hRendezvousFinishVR = NULL;
    }

    if(hRendezvousFinishSTRM) 
    {
        Rendezvous_delete(hRendezvousFinishSTRM);
        hRendezvousFinishSTRM = NULL;
    }

    // if(hRendezvousFinishRFTX) 
    // {
    //     Rendezvous_delete(hRendezvousFinishRFTX);
    //     hRendezvousFinishRFTX = NULL;
    // }

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(hRendezvousFinishRC) 
    {
        Rendezvous_delete(hRendezvousFinishRC);
        hRendezvousFinishRC = NULL;
    }

    if(hRendezvousFinishNC) 
    {
        Rendezvous_delete(hRendezvousFinishNC);
        hRendezvousFinishNC = NULL;
    }

    pthread_attr_destroy(&attr);

    pthread_mutex_destroy(&encEngineLock);
    pthread_mutex_destroy(&sd_mount_mutex);
    pthread_cond_destroy(&sd_mount_cond);
    pthread_mutex_destroy(&mp4_vol_mutex);
    pthread_cond_destroy(&mp4_vol_cond);
//    pthread_mutex_destroy(&rftx_mutex);
//    pthread_cond_destroy(&rftx_cond);
    pthread_mutex_destroy(&start_rec_mutex);

    for(i = 0; i < MAX_BUF_GROUP_NUM; i++)
    {
        pthread_mutex_destroy(&buf_mutex[i]);
        pthread_mutex_destroy(&rcond_mutex[i]);
        pthread_mutex_destroy(&wcond_mutex[i]);
        pthread_cond_destroy(&rbuf_cond[i]);
        pthread_cond_destroy(&wbuf_cond[i]);
    }

    CERuntime_exit();
    pthread_mutex_destroy(&gbl.mutex);
    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }


    clearEventRingBuf();
    sem_destroy(&semaphore);
    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }


    if(stop_rec_reset == 0)
    {
	    // save log file on the sd card
	   	if(need_to_create_log == 1)
	   	{
	    	system("/bin/cp /opt/device.log /media/card/");
            // system("/bin/cp /opt/device.log /media/mmcblk0/");
	    }
	}
	else
	{
		if(video_failed == 1)
        {
            if(fd_wdt != -1)
            {
                ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
            }
    		sync();
    		debug("Restart...\r\n");
        	system("/sbin/init 6");
        }
	    else if(status != SUCCESS)
	    {
	    	status = cnt_restart;
	    	//debug("cnt_restart %i \r\n", cnt_restart);
	    }
	}

    if(fd_wdt != -1)
    {
        ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);
    }

    if(status == SUCCESS)
    {
        //system("/bin/echo 240 > /sys/devices/platform/omap/omap_i2c.2/i2c-2/2-0068/int_mask");

        final_indication();
        // записываем всё на диск, иначе файловая система находится в режиме с отложенной записью 
        sync(); 
        if(fd_wdt != -1)
        {
            close(fd_wdt);
        }
        debug("Shutdown...\r\n");
        system("/sbin/init 0");
    }
    else
    {
        if(fd_wdt != -1)
        {
            err = close(fd_wdt);
        }
        sync();
        debug("Rerun %i%i...\r\n", err, errno);
    }

    debug("Exit done! %i\r\n", status);

    exit(status);
}

int     init_threads(pthread_attr_t *th_attr, struct sched_param *th_schedParam)
{
    /* Create the objects which synchronizes the thread init and cleanup */
    hRendezvousFinishNC         = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishSTRM   	= Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousInitRC           = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousInitLG           = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousInitI            = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishRC         = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishLG   		= Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishI          = Rendezvous_create(pairSync, &rzvAttrs);
//    hRendezvousInitRFTX         = Rendezvous_create(numRFTXDataThreads, &rzvAttrs);
//    hRendezvousFinishRFTX       = Rendezvous_create(numRFTXDataThreads, &rzvAttrs);

    if((hRendezvousFinishLG    == NULL) || (hRendezvousInitLG      == NULL) || 
        (hRendezvousInitRC      == NULL) || (hRendezvousFinishRC    == NULL) ||
        (hRendezvousInitI       == NULL) || (hRendezvousFinishI     == NULL) ||
        /*(hRendezvousInitRFTX    == NULL) || (hRendezvousFinishRFTX  == NULL) || */
        (hRendezvousFinishSTRM  == NULL) || (hRendezvousFinishNC    == NULL) )
    {
        ERR("Failed to create Rendezvous objects\r\n");
        return FAILURE;
    }

    /* Initialize the thread attributes */
    if (pthread_attr_init(th_attr)) 
    {
        ERR("Failed to initialize thread attrs\r\n");
        return FAILURE;
    }

    /* Force the thread to use custom scheduling attributes */
    if (pthread_attr_setinheritsched(th_attr, PTHREAD_EXPLICIT_SCHED)) 
    {
        ERR("Failed to set schedule inheritance attribute\r\n");
        return FAILURE;
    }

    /* Set the thread to be fifo real time scheduled */
    if (pthread_attr_setschedpolicy(th_attr, SCHED_FIFO)) 
    {
        ERR("Failed to set FIFO scheduling policy\r\n");
        return FAILURE;
    }

    // первой запускаем нить, отвечающую за поднятие сети и сетевых сервисов

    /* Set the netcommunication thread priority */
    (*th_schedParam).sched_priority = NETCOMM_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(th_attr, th_schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        internal_error = 1;
    }

    netCommEnv.hRendezvousFinishNC = hRendezvousFinishNC;

    // Create the thread for netcommunication
    if (pthread_create(&netCommThread, th_attr, netCommThrFxn, &netCommEnv)) 
    {
        ERR("Failed to create network communication thread\r\n");
        internal_error = 1;
    }

    initMask |= NETCOMMTHREADCREATED;

    // запускаем нить, отвечающую за индикацию с помощью 3-х светодиодов на плате

    /* Set the indication thread priority */
    (*th_schedParam).sched_priority = INDICATION_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(th_attr, th_schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        return FAILURE;
    }

    /* Create the thread for indication */
    indicationEnv.hRendezvousInit    = hRendezvousInitI;
    indicationEnv.hRendezvousFinishI = hRendezvousFinishI;

    if (pthread_create(&indicationThread, th_attr, indicationThrFxn, &indicationEnv)) 
    {
        ERR("Failed to create indication thread\r\n");
        return FAILURE;
    }

    initMask |= INDICATIONTHREADCREATED;

    // Wait indicationThread initialization
    if(hRendezvousInitI != NULL)
    {
    	Rendezvous_meet(hRendezvousInitI);
    }

    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

    // запускаем логгирование в файл /opt/device.log 
    // (для копирования файла на носитель нужно установить переменную need_to_create_log == 1)

    /* Set the logging thread priority */
    (*th_schedParam).sched_priority = LOGGING_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(th_attr, th_schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        internal_error = 1;
    }

    // Create the thread for logging
    loggingEnv.hRendezvousInit     = hRendezvousInitLG;
    loggingEnv.hRendezvousFinishLG = hRendezvousFinishLG;

    if (pthread_create(&loggingThread, th_attr, loggingThrFxn, &loggingEnv))
    {
        ERR("Failed to create logging thread\r\n");
        internal_error = 1;
    }

    initMask |= LOGGINGTHREADCREATED;

    // Wait loggingThread initialization
    if(hRendezvousInitLG != NULL)
    {
    	Rendezvous_meet(hRendezvousInitLG);
    }

    // Set the radio communication thread priority 
    (*th_schedParam).sched_priority = RADIOCOMM_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(th_attr, th_schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        internal_error = 1;
    }

    // Create the thread for radio communication
    radioCommEnv.hRendezvousInit     = hRendezvousInitRC;
    radioCommEnv.hRendezvousFinishRC = hRendezvousFinishRC;
    // radioCommEnv.hRendezvousInitRFTX = hRendezvousInitRFTX ;
    // radioCommEnv.hRendezvousStopRFTX = hRendezvousFinishRFTX;

    if (pthread_create(&radioCommThread, th_attr, radioCommThrFxn, &radioCommEnv)) 
    {
        ERR("Failed to create radio communication thread\r\n");
        internal_error = 1;
    }

    initMask |= RADIOCOMMTHREADCREATED;

    // Wait radioCommThread initialization
    if(hRendezvousInitRC != NULL)
    {
        Rendezvous_meet(hRendezvousInitRC);
    }

    return SUCCESS;
}

int     init_thread_env()
{
        /* Zero out the thread environments */
//    Dmai_clear(aewbEnv);
    Dmai_clear(aCaptureEnv);
    Dmai_clear(vCaptureEnv);
    Dmai_clear(streamBuffsEnv);
    Dmai_clear(videoEnv);
    Dmai_clear(rszVideoEnv);
    Dmai_clear(audioEnv);
    Dmai_clear(packerEnv);
    //Dmai_clear(resizeEnv);
    Dmai_clear(loggingEnv);
    Dmai_clear(indicationEnv);
    Dmai_clear(radioCommEnv);
    Dmai_clear(netCommEnv);

    return SUCCESS;
}

int     init_mutexes()
{
    int i;

    if(pthread_mutex_init(&start_rec_mutex, NULL) != 0)
    {
        ERR("\r\n start_rec_mutex init failed\r\n");
        return FAILURE;
    }    
    if(pthread_mutex_init(&sd_mount_mutex, NULL) != 0)
    {
        ERR("\r\n sd_mount_mutex init failed\r\n");
        return FAILURE;
    }    
    if(pthread_cond_init(&sd_mount_cond, NULL) != 0)
    {
        ERR("\r\n sd_mount_cond init failed\r\n");
        return FAILURE;
    }

    for(i = 0; i < MAX_BUF_GROUP_NUM; i++)
    {
        if(pthread_mutex_init(&buf_mutex[i], NULL) != 0)
        {
            ERR("\r\n buf_mutex[%i] init failed\r\n", i);
            return FAILURE;
        } 
        if(pthread_mutex_init(&rcond_mutex[i], NULL) != 0)
        {
            ERR("\r\n rcond_mutex[%i] init failed\r\n", i);
            return FAILURE;
        }
        if(pthread_mutex_init(&wcond_mutex[i], NULL) != 0)
        {
            ERR("\r\n wcond_mutex[%i] init failed\r\n", i);
            return FAILURE;
        }
        if(pthread_cond_init(&rbuf_cond[i], NULL) != 0)
        {
            ERR("\r\n rbuf_cond[%i] init failed\r\n", i);
            return FAILURE;
        }
        if(pthread_cond_init(&wbuf_cond[i], NULL) != 0)
        {
            ERR("\r\n wbuf_cond[%i] init failed\r\n", i);
            return FAILURE;
        }
    }

    if(pthread_mutex_init(&mp4_vol_mutex, NULL) != 0)
    {
        ERR("\r\n mp4_vol_mutex init failed\r\n");
        return FAILURE;
    }    
    if(pthread_cond_init(&mp4_vol_cond, NULL) != 0)
    {
        ERR("\r\n mp4_vol_cond init failed\r\n");
        return FAILURE;
    }

    // if(pthread_mutex_init(&rftx_mutex, NULL) != 0)
    // {
    //     ERR("\r\n rftx_mutex init failed\r\n");
    //     return FAILURE;
    // }
    // if(pthread_cond_init(&rftx_cond, NULL) != 0)
    // {
    //     ERR("\r\n rftx_cond init failed\r\n");
    //     return FAILURE;
    // }

    /* Initialize the mutex which protects the global data */
    if(pthread_mutex_init(&gbl.mutex, NULL) != 0)
    {
        ERR("\r\n gbl.mutex init failed\r\n");
        return FAILURE;
    }

    if (pthread_mutex_init(&encEngineLock, NULL) != 0)
    {
        ERR("\r\n encEngineLock mutex init failed\r\n");
        return FAILURE;
    }

    return SUCCESS;
}

/*
int load_settings_from_cfg_file()
{
    if(is_sd_mounted == 0)
    {
        ERR("SD is not set!\r\n");
        return FAILURE;
    }
    debug("Reading config...\r\n");

    FILE               *pFile;
    size_t              result;
    char                set_systime_cmd[30];
    long                lSize;
    u32                 atom_size;
    u32                 version;
    u32                 atom_name;
    u32                 t32;
    u8                 *buffer;
    u8                 *data;
    u8                  load_over   = 0;
    u16                 pos         = 0;
    struct timespec     cond_time;
    int                 err;

    while(1)
    {
        if(is_sd_mounted == 1)
        {
            break;
        }
        else
        {
            makewaittime(&cond_time, 0, 500000000); // 500 ms
            pthread_mutex_lock(&sd_mount_mutex);
            err = pthread_cond_timedwait(&sd_mount_cond, &sd_mount_mutex, &cond_time);
            if(err != 0) 
            {
                if(err == ETIMEDOUT)
                {
                    ERR("SD is not mounted!\r\n");
                }
                else if(err != ETIMEDOUT)
                {
                    ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
                }
                pthread_mutex_unlock(&sd_mount_mutex);
                return FAILURE;
            }
            pthread_mutex_unlock(&sd_mount_mutex);
        }
    }
    pFile = fopen ("/media/card/param.cfg" , "rb+" );
    // pFile = fopen ( "/media/mmcblk0/param.cfg" , "rb+" );

    if(pFile == NULL) 
    {
        WARN("Cannot open configuration file\r\n");
        return FAILURE;
    }

    // obtain file size
    fseek(pFile , 0 , SEEK_END);
    lSize = ftell (pFile);
    rewind(pFile);

    // allocate memory to contain the whole file
    buffer = (u8*) malloc(sizeof(u8) * lSize);
    if(buffer == NULL)
    {
        WARN("Cannot allocate memory for configuration file\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    // copy the file into the buffer
    result = fread(buffer, 1, lSize, pFile);
    if (result != lSize) 
    {
        WARN("Cannot read configuration file\r\n");
        fclose (pFile);
        pFile = NULL;
        free (buffer);
        return FAILURE;
    }

    while(!load_over)
    {
        atom_size = get32v(&buffer[pos]);
        if(atom_size == 0)
        {
            load_over = 1;
        }
        pos += 4;
        atom_name = get32v(&buffer[pos]);

        pos     += 4;
        version  = get32v(&buffer[pos]);
        pos     += 4;

        switch(atom_name)
        {
            case to_num('s','s','t','m'): 
                if(version == 0)
                {
                    if(atom_size != 0x14)
                    {
                        break;
                    }

                    u8 time_set = buffer[pos];
                    if(time_set != 1)
                    {
                        break;
                    }
                    logEvent(log_WATCHES_SET);

                    data = &buffer[pos + 1];

                    sprintf(set_systime_cmd, "/bin/date %02x%02x%02x%02x2%01x%02x.%02x", to_bcd(data[5]), to_bcd(data[4]), 
                        to_bcd(data[2]), to_bcd(data[1]), data[6]/100, to_bcd(data[6]), to_bcd(data[0]));
                    debug("Attention: system date is %s\r\n", set_systime_cmd);

                    buffer[pos] = 0;
                    fseek(pFile , pos , SEEK_SET);
                    fwrite (buffer + pos , 1, 1, pFile);
                    system(set_systime_cmd);                    

                    logEvent(log_WATCHES_NEW_TIME);
                }
                break;
            case to_num('s','n','d','o'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        sound_only = 1;
                        debug("Attention: SOUND_ONLY\r\n");
                    }
                    else
                    {
                        sound_only = 0;
                    }
                }
                break;
            case to_num('l','o','g',' '):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        need_to_create_log = 1;
                        debug("Attention: LOGGING ON\r\n");
                    }
                    else
                    {
                        need_to_create_log = 0;
                        debug("Attention: LOGGING OFF\r\n");
                    }
                }
                break;
            case to_num('a','c','p','t'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        is_access_point = 1;
                        debug("Attention: ACCESS POINT MODE\r\n");
                    }
                    else
                    {
                        is_access_point = 0;
                        debug("Attention: WIFI CLIENT MODE\r\n");
                    }
                }
                break;
            case to_num('s','l','p','m'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        sleep_on_radio = 1;
                        debug("Attention: SLEEP ON RADIO\r\n");
                    }
                    else
                    {
                        sleep_on_radio = 0;
                        debug("Attention: SLEEP ON TIME\r\n");
                    }
                }
                break;
            case to_num('d','i','l','c'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        deinterlace_on = 1;
                        debug("Attention: DEINTERLACER IS ON\r\n");
                    }
                    else
                    {
                        deinterlace_on = 0;
                        debug("Attention: DEINTERLACER IS OFF\r\n");
                    }
                }
                break;
            case to_num('b','l','e','d'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        leds_always = 1;
                        debug("Attention: Indication always\r\n");
                    }
                    else if(t32 == 2)
                    {
                        leds_always = 0;
                        debug("Attention: Indication only when USB connected\r\n");
                    }
                }
                break;
            case to_num('c','o','l','r'):
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 1)
                    {
                        color_video = 1;
                        debug("Attention: COLOR VIDEO FORMAT\r\n");
                    }
                    else if(t32 == 0)
                    {
                        color_video = 0;
                        debug("Attention: BW VIDEO FORMAT\r\n");
                    }
                }
                break;
            case to_num('m','c','g','n'):       // analog microphone gain
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    t32 = t32 << 1;
                    analog_mic_gain1 = t32;
                    analog_mic_gain2 = t32;
                    // set level of analog microphone gain
                    debug("Attention: ANALOG MIC GAIN - %i dB\r\n", analog_mic_gain1);
                }
                break;
            case to_num('d','m','g','n'): // digital microphone gain -  number of digits for shift from 0 to 8
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32                 = get32v(&buffer[pos]);
                    digital_mic_gain    = t32;
                    debug("Attention: DIGITAL MIC GAIN by %i times\r\n", digital_mic_gain);
                }
                break;
            case to_num('v','b','t','r'): // video bitrate
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32                 = get32v(&buffer[pos]);
                    video_bitrate       = t32;

                    if(video_bitrate < MIN_VBITRATE)
                    {
                        debug("Cannot set video bitrate - %lu \r\n", video_bitrate);
                        video_bitrate = MIN_VBITRATE;
                    }
                    else if(video_bitrate > MAX_VBITRATE)
                    {
                        debug("Cannot set video bitrate - %lu \r\n", video_bitrate);
                        video_bitrate = MAX_VBITRATE;
                    }
                    debug("Attention: VIDEO BITRATE - %lu \r\n", video_bitrate);
                }
                break;
            case to_num('a','u','c','h'): // audio channels number
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32                 = get32v(&buffer[pos]);
                    audio_channels      = t32;

                    if(audio_channels < 1)
                    {
                        debug("Cannot set number of audio channels - %lu \r\n", audio_channels);
                        audio_channels = 1;
                    }
                    else if(audio_channels > 2)
                    {
                        debug("Cannot set number of audio channels - %lu \r\n", audio_channels);
                        audio_channels = 2;
                    }
                    debug("Attention: AUDIO CHANNELS - %lu \r\n", audio_channels);
                }
                break;

            case to_num('v','s','r','c'): // active video input
                if(version == 0)
                {
                    if(atom_size != 0x10)
                    {
                        break;
                    }
                    t32 = get32v(&buffer[pos]);
                    if(t32 == 2)
                    {
                        hdmi_active         = 0;
                        video_source_num    = 2;
                        debug("Attention: DIGITAL INPUT IS ACTIVE\r\n");
                    }
                    else if(t32 == 1)
                    {
                        hdmi_active         = 1;
                        video_source_num    = 1;
                        debug("Attention: HDMI INPUT IS ACTIVE\r\n");
                    }
                    else if(t32 == 0)
                    {
                        hdmi_active         = 0;
                        video_source_num    = 0;
                        debug("Attention: ANALOG INPUT IS ACTIVE\r\n");
                    }
                }
                break;

            case to_num('f','r','e','e'):  
                fclose(pFile);
                pFile = NULL;
                free (buffer);
                return SUCCESS;
            default:   
                break;
        }
        pos += (atom_size - 12);
    }

    fclose(pFile);
    pFile = NULL;
    free (buffer);
    return SUCCESS;
}*/

