/*
 * avrec_service.c
 */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/watchdog.h>

#include "logging.h"
#include "setting_server.h"
#include "packet_io.h"
#include "common.h"
#include "gpio_func.h"

#include "avrec_service.h"

#define MAX_AUDIO_GAIN          47 
#define MAX_SKIP_FACTOR         24  // сколько кадров можем пропустить в течение 1 сек.
#define AVREC_CUSTOM_FIELDS     20  
#define AVREC_CFG_FIELD_COUNT   (AVREC_CUSTOM_FIELDS + MAX_TIMER_NUM)
#define P_CLIP(a,b,c)           (a < b ? b : (a > c ? c : a))

// время, через которе можно уходить в сон, если нет никаких событий (сейчас не используем)
struct timeval PowerOffCheckTime;
struct timeval CurTime;
struct timeval ChangesCheckPeriod;  // обновляем конфигурацию параметров через этот период времени

app_server_interact_struct *ad;     // хранит дескриптор приложения и данные по установленной сессии
PacketIO                    comm;   // хранит данные о куммуникации с приложением настроек аудио/видео записи
u8                          real_time[6]; // 0 - sec, 1 - min, 2 - hour, 3 - day, 4 - mon, 5 - year

// config info
u8                          work_mode;
u8                          new_work_mode;
u8                          prev_cap_state;
AVAppSettings               curr_settings;

// misc params
sAVErrCodes                 last_error;

u8 InitComm(AppCfgParameterTable *table, int count)
{
    PacketIOInit(&comm);
    PacketIOSetAppStruct(&comm, ad);    
    PacketIOSetOwner(&comm, 1);
    if(PacketIOSetParamTable(&comm, table, count))
    {
        return FAILURE;
    }
    return SUCCESS;
}

u8 SaveSettings()  // сохраняем текущие параметры в /opt/appdata
{
    FILE           *pFile       = NULL;

    debug("NEW SETTINGS\r\n");
    // debug("Work mode %i\r\n", new_work_mode);
    // debug("Charger on %i\r\n", curr_settings.charger_on);
    // debug("Charge level %i\r\n", curr_settings.charge_level);
    // debug("Image params: brightness %i\r\n", curr_settings.img_params.brightness);
    // debug("Image params: contrast %i\r\n", curr_settings.img_params.contrast);
    // debug("Image params: saturation %i\r\n", curr_settings.img_params.saturation);
    // debug("Skip factor %i\r\n", curr_settings.skip_factor);
    // debug("Sound channels %i\r\n", curr_settings.sound_channels);
    // debug("Audio gain ch1 %i\r\n", curr_settings.aud_gain_ch1);
    // debug("Audio gain ch2 %i\r\n", curr_settings.aud_gain_ch2);
    // debug("Voltage %i\r\n", curr_settings.video_cam_voltage);
    // debug("Bitrate %lu\r\n", curr_settings.bitrate);
    // debug("Framesize %i\r\n", curr_settings.framesize);
    // debug("Video source %i\r\n", curr_settings.video_source);
    // debug("Number of digital channel %i\r\n", curr_settings.cam_channel);
    // debug("Real time %.4i_%.2i_%.2i_%.2i-%.2i-%.2i\r\n", from_bcd(real_time[5]) + 2000, from_bcd(real_time[4]), 
    //     from_bcd(real_time[3]), from_bcd(real_time[2]), from_bcd(real_time[1]), from_bcd(real_time[0]));
    // debug("Timer count %i\r\n", curr_settings.timer_count);

    cam_brightness      = curr_settings.img_params.brightness - 78;
    cam_contrast        = curr_settings.img_params.contrast - 78;
    cam_saturation      = (float)(curr_settings.img_params.saturation - 78) / (float) 10;

    half_vrate          = curr_settings.skip_factor;
    audio_channels      = curr_settings.sound_channels;
    analog_mic_gain1    = curr_settings.aud_gain_ch1;
    analog_mic_gain2    = curr_settings.aud_gain_ch2;   
    video_bitrate       = curr_settings.bitrate;
    video_source_num    = curr_settings.video_source;
    cam_channel_num     = curr_settings.cam_channel;
    // digital_mic_gain = curr_settings.dig_gain;

    pFile = fopen("/opt/appdata", "rb+");
    if(pFile == NULL)
    {
        WARN("Cannot open appdata file\r\n");

        pFile = fopen("/opt/appdata", "wb+");

        if (pFile == NULL)
        {
            ERR("Cannot create appdata file\r\n");
            return FAILURE;
        }
    }

    if(fseek (pFile, 0 , SEEK_SET) != 0)
    {
        ERR("Invalid appdata file\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fwrite((void*)&cam_brightness, sizeof(u32), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (cam_brightness)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&cam_contrast, sizeof(u32), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (cam_contrast)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&cam_saturation, sizeof(float), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (cam_saturation)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fseek(pFile, 4, SEEK_CUR) != 0)  // reserved for cam_hue
    {
        ERR("Invalid appdata file\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fwrite((void*)&video_bitrate, sizeof(u32), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (video_bitrate)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&half_vrate, sizeof(u16), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (half_vrate)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&audio_channels, sizeof(u32), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (audio_channels)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&analog_mic_gain1, sizeof(int), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (analog_mic_gain1)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&analog_mic_gain2, sizeof(int), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (analog_mic_gain2)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&video_source_num, sizeof(u8), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (video_source_num)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&cam_voltage, sizeof(u8), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (cam_voltage)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    if(fwrite((void*)&cam_channel_num, sizeof(u8), 1, pFile) != 1)
    {
        ERR("Cannot write appdata file (cam_channel_num)\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    fclose(pFile);
    pFile = NULL;

    return SUCCESS;
}

u8 AVRecLoadSettings()      // загружаем последние сохраненные параметрв из /opt/appdata, если они есть
{
    time_t          seconds;
    struct tm       timeinfo;
    FILE           *pFile           = NULL;
    u32             t32;
    float           tfloat;
    u16             t16;
    u8              t8;
    int             tint;
    int             fSize;

    work_mode       = APP_REC_MODE_IDLE;
    new_work_mode   = work_mode;

    pFile = fopen("/opt/appdata", "rb");
    if(pFile == NULL)
    {
        WARN("Cannot open appdata file\r\n");
        
    }
    else
    {
        // structure of /opt/appdata and size in Bytes
        // cam_brightness       4B
        // cam_contrast         4B
        // cam_saturation       4B
        // cam_hue              4B
        // video_bitrate        4B
        // half_vrate           2B
        // audio_channels       4B
        // analog_mic_gain1     4B
        // analog_mic_gain2     4B
        // video_source_num     1B
        // cam_voltage          1B
        // cam_channel_num      1B
        // is_access_mode       1B
        // wifissid             13B
        // wifipass             13B

        do
        {
            // find size of the file

            fseek(pFile, 0, SEEK_END);
            fSize = ftell(pFile);

            if(fSize < 4)
            {
                WARN("Size of file /opt/appdata is wrong\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fseek(pFile, 0, SEEK_SET) != 0)
            {
                ERR("Invalid appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t32, sizeof(u32), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            cam_brightness = (cam_brightness > 100) ? 100 : cam_brightness;
            cam_brightness = (t32 > 100) ? cam_brightness : t32;

            if(fSize < 8)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t32, sizeof(u32), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            cam_contrast = (cam_contrast > 100) ? 100 : cam_contrast;
            cam_contrast = (t32 > 100) ? cam_contrast : t32;

            if(fSize < 12)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&tfloat, sizeof(float), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            cam_saturation = ((cam_saturation > 10) || (cam_saturation < 0)) ? 2 : cam_saturation;
            cam_saturation = ((tfloat > 10) || (tfloat < 0)) ? cam_contrast : tfloat;

            if(fSize < 16)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fseek(pFile, 4, SEEK_CUR) != 0)  // reserved for cam_hue
            {
                ERR("Invalid appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fSize < 20)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t32, sizeof(u32), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            video_bitrate = ((video_bitrate > 2000000) || (video_bitrate < 500000)) ? 2000000 : video_bitrate;
            video_bitrate = ((t32 > 2000000) || (t32 < 500000)) ? video_bitrate : t32;

            if(fSize < 22)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t16, sizeof(u16), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            half_vrate = ((half_vrate != 1) && (half_vrate != 0)) ? 0 : half_vrate;
            half_vrate = ((t16 == 1) || (t16 == 0)) ? t16 : half_vrate;

            if(fSize < 26)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t32, sizeof(u32), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            audio_channels = ((audio_channels != 1) && (audio_channels != 2)) ? 1 : audio_channels;
            audio_channels = ((t32 == 1) || (t32 == 2)) ? t32 : audio_channels;

            if(fSize < 30)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&tint, sizeof(int), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            analog_mic_gain1 = ((analog_mic_gain1 > 47) || (analog_mic_gain1 < 0)) ? 47 : analog_mic_gain1;
            analog_mic_gain1 = ((tint > 47) || (tint < 0)) ? analog_mic_gain1 : tint;

            if(fSize < 34)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&tint, sizeof(int), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            analog_mic_gain2 = ((analog_mic_gain2 > 47) || (analog_mic_gain2 < 0)) ? 47 : analog_mic_gain2;
            analog_mic_gain2 = ((tint > 47) || (tint < 0)) ? analog_mic_gain2 : tint;

            if(fSize < 35)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t8, sizeof(u8), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            video_source_num = 0;
            // video_source_num = ((video_source_num != 0) && (video_source_num != 1) && (video_source_num != 2)) ? 2;
            // video_source_num = ((t8 == 0) || (t8 == 1) || (t8 == 2)) ? t8 : video_source_num;

            if(fSize < 36)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t8, sizeof(u8), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            cam_voltage = ((cam_voltage != 12) && (cam_voltage != 10) && (cam_voltage != 5)) ? 12 : cam_voltage;
            cam_voltage = ((t8 == 12) || (t8 == 10) || (t8 == 5)) ? t8 : cam_voltage;

            if(fSize < 37)
            {
                WARN("Size of file /opt/appdata is not full\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }

            if(fread((void*)&t8, sizeof(u8), 1, pFile) != 1)
            {
                ERR("Cannot read appdata file\r\n");
                fclose(pFile);
                pFile = NULL;
                break;
            }
            cam_channel_num = ((cam_channel_num != 0) && (cam_channel_num != 1)) ? 0 : cam_channel_num;
            cam_channel_num = ((t8 == 0) || (t8 == 1)) ? t8 : cam_channel_num;

            fclose(pFile);
            pFile = NULL;
        }
        while(0);
    }

    curr_settings.img_params.brightness = cam_brightness + 78;
    curr_settings.img_params.contrast   = cam_contrast + 78;
    curr_settings.img_params.saturation = cam_saturation * 10 + 78;

    curr_settings.skip_factor           = half_vrate;
    curr_settings.framesize             = (video_bitrate / ((framerate >> half_vrate)/1000)) >> 3;
    curr_settings.sound_channels        = audio_channels;
    curr_settings.aud_gain_ch1          = analog_mic_gain1;
    curr_settings.aud_gain_ch2          = analog_mic_gain2;
    curr_settings.video_cam_voltage     = cam_voltage;
    curr_settings.charger_on            = charger_present;
    curr_settings.charge_level          = charger_level;
    curr_settings.bitrate               = video_bitrate;
    curr_settings.video_source          = video_source_num;
    curr_settings.cam_channel           = cam_channel_num;
    curr_settings.timer_count           = MAX_TIMER_NUM;
    curr_settings.sd_info.card_size     = sd_totalspace << 10;
    curr_settings.sd_info.free_space    = sd_freespace << 10;
    curr_settings.sd_info.rec_files_num = 0xFFFFFFFF;
    curr_settings.sd_info.status        = sd_status;
    curr_settings.timer_count           = MAX_TIMER_NUM;

    if(video_source_num == 2)
    {
        // gpio_set_value(change_dcam, curr_settings.cam_channel);
    }

    seconds         = time(NULL);
    timeinfo        = *localtime(&seconds);

    real_time[0]    = to_bcd(timeinfo.tm_sec);
    real_time[1]    = to_bcd(timeinfo.tm_min);
    real_time[2]    = to_bcd(timeinfo.tm_hour);
    real_time[3]    = to_bcd(timeinfo.tm_mday);
    real_time[4]    = to_bcd(timeinfo.tm_mon + 1);
    real_time[5]    = to_bcd(timeinfo.tm_year % 100);

    // debug("OLD SETTINGS\r\n");
    // debug("Work mode %i\r\n", new_work_mode);
    // debug("Charger on %i\r\n", curr_settings.charger_on);
    // debug("Charge level %i\r\n", curr_settings.charge_level);
    // debug("Image params: brightness %i\r\n", curr_settings.img_params.brightness);
    // debug("Image params: contrast %i\r\n", curr_settings.img_params.contrast);
    // debug("Image params: saturation %i\r\n", curr_settings.img_params.saturation);
    // debug("Skip factor %i\r\n", curr_settings.skip_factor);
    // debug("Sound channels %i\r\n", curr_settings.sound_channels);
    // debug("Audio gain ch1 %i\r\n", curr_settings.aud_gain_ch1);
    // debug("Audio gain ch2 %i\r\n", curr_settings.aud_gain_ch2);
    // debug("Voltage %i\r\n", curr_settings.video_cam_voltage);
    // debug("Bitrate %lu\r\n", curr_settings.bitrate);
    // debug("Framesize %i\r\n", curr_settings.framesize);
    // debug("Video source %i\r\n", curr_settings.video_source);
    // debug("Number of digital channel %i\r\n", curr_settings.cam_channel);
    // debug("Real time %.4i_%.2i_%.2i_%.2i-%.2i-%.2i\r\n", from_bcd(real_time[5]) + 2000, from_bcd(real_time[4]), 
    //     from_bcd(real_time[3]), from_bcd(real_time[2]), from_bcd(real_time[1]), from_bcd(real_time[0]));
    // debug("Timer count %i\r\n", curr_settings.timer_count);
    // debug("SD size %lld\r\n", curr_settings.sd_info.card_size);
    // debug("SD free space %lld\r\n", curr_settings.sd_info.free_space);
    // debug("SD status %lu\r\n", curr_settings.sd_info.status);

    SaveSettings();

    return SUCCESS;
}

u8 DestroyService()
{
    if(EthIfaceFree(comm.app_serv->app.number))
    {
        ERR("Failed to free ethernet interface\r\n");
        return FAILURE;
    }

    if(comm.cfg_table != NULL)
    {
        free(comm.cfg_table);
        comm.cfg_table = NULL;
    }
    return SUCCESS;
}


u8 InitService() // Инициализируем таблицу параметров для приложения настроек аудио/видео записи
{
    u8  pos;
    u8  f_num;
    int i;

    is_config_changed_from_wlan = 0;
    prev_cap_state              = 0;

    AppCfgParameterTable table[AVREC_CFG_FIELD_COUNT] = 
    {
        { 0,    apdWritable,        1, (u8*)&new_work_mode},                        // 0
        { 2,    apdWritable,        4, (u8*)&curr_settings.img_params},             // 1
        { 4,    apdWritable,        2, (u8*)&curr_settings.framesize},              // 2
        { 5,    apdWritable,        2, (u8*)&curr_settings.skip_factor},            // 3
        { 6,    apdWritable,        1, (u8*)&curr_settings.sound_channels},         // 4
        { 7,    apdWritable,        1, (u8*)&curr_settings.aud_gain_ch1},           // 5
        { 8,    apdWritable,        1, (u8*)&curr_settings.aud_gain_ch2},           // 6
        { 0x17, apdWritable,        1, (u8*)&curr_settings.video_source},           // 7
        { 0x10, apdWritable,        1, (u8*)&curr_settings.video_cam_voltage},      // 8
        { 3,    apdWritable,        1, (u8*)&curr_settings.cam_channel},            // 9
        { 0x16, apdWritable,        4, (u8*)&curr_settings.bitrate},                // 10
        { 0x5F, apdWritable,        6, (u8*)real_time},                             // 11
        { 0x30, apdReadableOnly,    1, (u8*)&curr_settings.charger_on},             // 12
        { 0x31, apdReadableOnly,    1, (u8*)&curr_settings.charge_level},           // 13
        { 0x41, apdReadableOnly,   24, (u8*)&curr_settings.sd_info},                // 14
        { 0xA0, apdReadableOnly,    1, (u8*)&curr_settings.timer_count},            // 15
    };
    // curr_settings.sound_channels    = 1;
    // curr_settings.cam_channel       = 0;
    // curr_settings.video_source      = 2;
    // curr_settings.bitrate           = 2000000;
    // curr_settings.timer_count        = MAX_TIMER_NUM;
    pos                             = AVREC_CUSTOM_FIELDS;
    f_num                           = 0xA1;

    for(i = 0; i < MAX_TIMER_NUM; i++, f_num++, pos++)
    {
        AppCfgParameterTable cfg_item = {f_num, apdWritable, 11, (u8*)&curr_settings.record_timers[i]};
        memcpy(&table[pos], &cfg_item, sizeof(AppCfgParameterTable));
    }
    
    if(InitComm(table, AVREC_CFG_FIELD_COUNT))
    {
        return FAILURE;
    }

    last_error          = eAVNoError;
    ad->app.run_status  = rsOff;
    ad->app.last_error  = ieNoError;
    
    // LoadSettings();
    return SUCCESS;
}

void DoStatus()
{
    switch(work_mode)
    {
        case APP_REC_MODE_RECTIMER:
        case APP_REC_MODE_REC:
            if(is_rec_started)
            {
                    ad->app.run_status = rsReady;
                    //ad->app.last_error = ieNoError;
            }
            else
            {
                if(is_memory_full)
                {
                    ad->app.run_status = rsErr;
                    ad->app.last_error = ieNoCardsReady;
                }
                else
                {
                    if(temp_sens)
                    {
                        ad->app.run_status = rsErr;
                        ad->app.last_error = ieSystemOverheat;
                    }
                }
            }
            break;
        case APP_REC_MODE_IDLE:
            if(temp_sens)
            {
                ad->app.run_status = rsErr;
                ad->app.last_error = ieSystemOverheat;
            }
            else
            {
                ad->app.run_status = rsReady;
                ad->app.last_error = ieNoError;
            }
            break;          
    }
}

void DoPowerOffCondition()
{
    u64 diff_time;
    gettimeofday(&CurTime, NULL); 
    diff_time = (((u64)CurTime.tv_sec * (u64)US_IN_SEC + (u64)CurTime.tv_usec)
                - ((u64)PowerOffCheckTime.tv_sec * (u64)US_IN_SEC + (u64)PowerOffCheckTime.tv_usec));

    if(diff_time < 100000)
    {
        return;
    }
    gettimeofday(&PowerOffCheckTime, NULL); 

    AVRecPowerOffCondition = ((ad->app.status != sCaptured) && (is_config_changed_from_wlan == 0) 
                                && ((work_mode == APP_REC_MODE_IDLE) 
                                    || (((work_mode == APP_REC_MODE_REC) 
                                        || (work_mode == APP_REC_MODE_RECTIMER)) && (!is_rec_failed)
                                  )
                                )
                             );

    //setting new work modes
    if(ad->app.status != sCaptured)
    {
        //if nobody captured us
        if((new_work_mode == work_mode) && (work_mode != APP_REC_MODE_RECTIMER)
            && (work_mode != APP_REC_MODE_REC))
        {
            //if work mode is not record
            new_work_mode = APP_REC_MODE_IDLE;
        }
    }
}

void DoConfigChanges()
{
    u64 diff_time;
    gettimeofday(&CurTime, NULL);
    diff_time = (((u64)CurTime.tv_sec * (u64)US_IN_SEC + (u64)CurTime.tv_usec) 
                - ((u64)ChangesCheckPeriod.tv_sec * (u64)US_IN_SEC + (u64)ChangesCheckPeriod.tv_usec));
    if(diff_time < 100000)
    {
        return;
    }
    gettimeofday(&ChangesCheckPeriod, NULL);

    if(!is_config_changed_from_wlan)
    {
        return;
    }
    // if(is_cap_started != 1)
    // {
        SaveSettings();
        is_config_changed_from_wlan = 0;
    // }
    // else
    // {
    //  WARN("Cannot apply new settings due to recording");
    // }
}

int ProcessChanges()
{
    int i                       = 0;
    int capture_off_params[]    = {2, 3, 4, 7, 8, 9, 10, 11};
    int capture_off_p_count     = 8;

    work_mode = new_work_mode;
    DoStatus();                 // обновить статус приложения
    DoPowerOffCondition();      // запрет на сон
    DoConfigChanges();          // сохранить текущие настройки

    curr_settings.charger_on            = charger_present;
    curr_settings.charge_level          = charger_level;
    curr_settings.sd_info.card_size     = sd_totalspace << 10;
    curr_settings.sd_info.free_space    = sd_freespace << 10;
    curr_settings.sd_info.status        = sd_status;

    // Update parameter statuses in table
    if((is_cap_started) && (prev_cap_state == 0))
    {
        prev_cap_state = 1;
        for(i = 0; i < capture_off_p_count; i++)
        {
            if(comm.cfg_table[capture_off_params[i]].access_flag != apdReadableOnly)
            {
                if((is_stream_request == 0) && (i == (capture_off_p_count - 1)))
                {
                    break;
                }
                comm.cfg_table[capture_off_params[i]].access_flag = apdReadableOnly;
            }
        }
    }
    
    if ((is_cap_started == 0) && (prev_cap_state == 1))
    {
        prev_cap_state = 0;
        for(i = 0; i < capture_off_p_count; i++)
        {
            if(comm.cfg_table[capture_off_params[i]].access_flag == apdReadableOnly)
            {
                comm.cfg_table[capture_off_params[i]].access_flag = apdWritable;
            }
        }
    }

    if(is_rec_started)
    {
        if(is_rec_request)
        {
            if(new_work_mode != APP_REC_MODE_REC)
            {
                new_work_mode = APP_REC_MODE_REC;
            }
        }
        // else
        // {
        //  if(new_work_mode != APP_REC_MODE_RECTIMER)
        //  {
        //      new_work_mode = APP_REC_MODE_RECTIMER;
        //  }
        // }
    }
    else
    {
        if((is_rec_request != 1) && (new_work_mode != APP_REC_MODE_IDLE))
        {
            new_work_mode = APP_REC_MODE_IDLE; 
        }
    }
    return 0;
}


void *avRecServiceThrFxn(void *arg)
{
    Command          currentCommand;
    void            *status         = THREAD_SUCCESS;
    AVRecServiceEnv *envp           = (AVRecServiceEnv *) arg;
    ad                              = envp->app_descr;

    gettimeofday(&PowerOffCheckTime, NULL);
    gettimeofday(&ChangesCheckPeriod, NULL); 

    if(InitService())
    {
        ERR("Failed to init avrec service\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }
    while (1)
    {
        currentCommand = gblGetCmd();

        if((currentCommand == FINISH) || (currentCommand == SLEEP) /*|| (stop_netconnect == 1)*/)
        {
            debug("AVRec service thread finishing ...\r\n");
            goto cleanup;
        }
        // если пришли пакеты для приложения настроек аудио/видео записи, то реагируем на них
        PacketIOProcessInterfaces(&comm);
        // если пакеты с новыми настройками, то обработать и сохранить все, что надо
        ProcessChanges();     
        usleep(10000);      // пауза, чтобы не занимать часто процессорное время
    }

    cleanup:
    
    if(DestroyService())
    {
        ERR("Failed to destroy avrec service\r\n");
    }

    if(envp->hRendezvousFinish != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinish); // рандеву с нитью-родителем, сообщение о завершении
    }

    debug("AVRec service thread finished\r\n");
    return status;  
}

// проверяем на валидность значение установленного таймера (таймеры не реализованы - это заготовки)
u8 CheckTimer(rec_timer *tmr) 
{
    timer_time *st;
    timer_time *end;
    u8          dd = 0;

    st  = &tmr->time_start;
    end = &tmr->time_end;

    if(st->year > end->year)
    {
        ERR("Timer has invalid year value\r\n");
        return FAILURE;
    }
    if(st->year == end->year)
    {
        if(st->month > end->month)
        {
            ERR("Timer has invalid month value\r\n");
            return FAILURE;
        }
        if(st->month == end->month)
        {
            if(st->date > end->date)
            {
                ERR("Timer has invalid date value\r\n");
                return FAILURE;
            }
            if(st->date == end->date)
            {
                if(st->hour > end->hour)
                {
                    ERR("Timer has invalid hour value\r\n");
                    return FAILURE;
                }
                if(st->hour == end->hour)
                {
                    if(st->min >= end->min)
                    {
                        ERR("Timer has invalid minute value\r\n");
                        return FAILURE;
                    }
                    if(st->min < end->min)
                    {
                        if((st->year < 99) && (end->year < 99) && (st->month >= 1)
                            && (st->month <= 12) && (end->month >= 1) 
                            && (end->month <= 12))
                        {
                            switch(st->month)
                            {
                                case 1:case 3:case 5:case 7:case 8:case 10:case 12:
                                    dd = 31;
                                    break;
                                case 4:case 6:case 9:case 11:
                                    dd = 30;
                                    break;
                                case 2:
                                    dd = 28;
                                    break;
                            }
                            if(st->month == 2)
                            {
                                if(st->year%4 == 0)
                                {
                                    dd++;
                                }
                            }
                            if((st->date == 0) || (st->date > dd))
                            {
                                ERR("Timer has invalid date value\r\n");
                                return FAILURE;
                            }
                            switch(end->month)
                            {
                                case 1:case 3:case 5:case 7:case 8:case 10:case 12:
                                    dd = 31;
                                    break;
                                case 4:case 6:case 9:case 11:
                                    dd = 30;
                                    break;
                                case 2:
                                    dd = 28;
                                    break;
                            }
                            if(end->month == 2)
                            {
                                if(end->year%4 == 0)
                                {
                                    dd++;
                                }
                            }
                            if((end->date == 0) || (end->date > dd))
                            {
                                ERR("Timer has invalid date value\r\n");
                                return FAILURE;
                            }
                            if((st->hour < 24) && (end->hour < 24) && (st->min < 60)
                                && (end->min < 60))
                            {
                                return SUCCESS;
                            }
                        }
                        else
                        {
                            ERR("Timer has invalid year and month values\r\n");
                            return FAILURE;
                        }
                    }
                }
            }
        }
    }                           
    return SUCCESS;
}

void AVRecDataFunc(packet_info2 *pi){}; // заглушка на месте функции передачи кадров по ethernet

// верификация и сохранение полученных параметров, выполнение действий, связанных с ними
u8 AVRecParamFunc(u8 act, u8 fType, u8 *data)
{
    u8      t8;
    u16     t16;
    u32     t32;
    u32     temp_bitrate;
    u16     temp_framesize;
    char    set_systime_cmd[64];

    switch(act)
    {
        case apdParamCheck:
            switch(fType)
            {
                case 0:
                    t8 = data[0];
                    if(work_mode == APP_REC_MODE_RECTIMER)
                    {
                        ERR("Wrong parameter type\r\n");
                        return FAILURE;
                    }
                    return (t8 <= APP_REC_MODE_RECTIMER) ? SUCCESS : FAILURE;
                case 5:
                    if((work_mode == APP_REC_MODE_REC)
                        ||(work_mode == APP_REC_MODE_RECTIMER))
                    {
                        ERR("Wrong parameter type\r\n");
                        return FAILURE;
                    }
                    memcpy(&t16, data, 2);
                    return (t16 <= MAX_SKIP_FACTOR) ? SUCCESS : FAILURE;
                case 6:
                    t8 = data[0];
                    return ((t8 == 1) || (t8 == 2)) ? SUCCESS : FAILURE;
                case 0x10:
                    t8 = data[0];
                    return ((t8 == 12) || (t8 == 10) || (t8 == 5)) ? SUCCESS : FAILURE;
                case 0x11:
                    t8 = data[0];
                    return ((t8 == 1) || (t8 == 2)) ? SUCCESS : FAILURE;
                case 3:
                    t8 = data[0];
                    return ((t8 == 0) || (t8 == 1)) ? SUCCESS : FAILURE;
                    //return ((t8 == 1) || (t8 == 2)) ? SUCCESS : FAILURE;
                case 0x17:
                    t8 = data[0];
                    return ((t8 == 0) || (t8 == 1) || (t8 == 2)) ? SUCCESS : FAILURE;
                case 0x18:
                    t8 = data[0];
                    return ((t8 == 0) || (t8 == 1) || (t8 == 2)) ? SUCCESS : FAILURE;
                default:
                    if((fType >= 0xA1) && (fType < 0xA1 + MAX_TIMER_NUM))
                    {
                        return CheckTimer((rec_timer*)&data[0]);
                    }
                    return SUCCESS;
            }
        case apdParamSet:
            switch(fType)
            {
                case 0:
                    t8 = data[0];
                    if(t8 == APP_REC_MODE_REC)
                    {   // don't change work_mode itself, flag a command
                        fix_record_state(1);
                        start_rec = 1;
                        debug("initiated from net\r\n");
                    }
                    else
                    {
                        if((t8 == APP_REC_MODE_IDLE) && (new_work_mode == APP_REC_MODE_REC))
                        {
                            fix_record_state(0);
                            start_rec = 0;
                            debug("stopped from net\r\n");
                        }
                        else
                        {
                            new_work_mode = data[0];
                            if(new_work_mode != work_mode)
                            {
                                ad->app.run_status = rsReady;
                                ad->app.last_error = ieNoError;
                            }                   
                        }
                    }
                    return SUCCESS;
                case 2:
                    if(memcmp(&curr_settings.img_params, data, 4))
                    {
                        curr_settings.img_params.brightness = P_CLIP(data[0], 78, 178);
                        curr_settings.img_params.contrast   = P_CLIP(data[1], 78, 178);
                        curr_settings.img_params.saturation = P_CLIP(data[2], 78, 178);
                        is_config_changed_from_wlan         = 1;
                    }
                    return SUCCESS;
                case 4:
                    memcpy((void*)&t16, data, 2);
                    if(t16 < 2500)
                    {
                        t16 = 2500;
                    }
                    else if(t16 > 10000)
                    {
                        t16 = 10000;
                    }
                    temp_bitrate = ((t16 << 3) * (framerate >> curr_settings.skip_factor))/1000;
                    if(curr_settings.framesize != t16)
                    {
                        curr_settings.framesize = t16;
                        curr_settings.bitrate   = temp_bitrate;
                        is_config_changed_from_wlan = 1;
                    }
                    return SUCCESS;
                case 5:
                    memcpy((void*)&t16, data, 2);
                    if(t16 > 1)
                    {
                        t16 = 1;
                    }
                    temp_framesize = (curr_settings.bitrate / ((framerate >> t16)/1000)) >> 3;
                    if(curr_settings.skip_factor != t16)
                    {
                        curr_settings.skip_factor   = t16;
                        curr_settings.framesize     = temp_framesize;
                        is_config_changed_from_wlan = 1;
                    }
                    return SUCCESS;
                case 6:
                    t8 = data[0];
                    if(curr_settings.sound_channels != t8)
                    {
                        curr_settings.sound_channels = t8;
                        is_config_changed_from_wlan  = 1;
                    }
                    return SUCCESS;
                case 7:
                    t16 = data[0] & 0x7F;
                    if(t16 > MAX_AUDIO_GAIN)
                    {
                        t16 = MAX_AUDIO_GAIN;
                    }
                    if(curr_settings.aud_gain_ch1 != t16)
                    {
                        curr_settings.aud_gain_ch1  = t16;
                        is_config_changed_from_wlan = 1; 
                    }
                    return SUCCESS;
                case 8:
                    t16 = data[0] & 0x7F;
                    if(t16 > MAX_AUDIO_GAIN)
                    {
                        t16 = MAX_AUDIO_GAIN;
                    }
                    if(curr_settings.aud_gain_ch2 != t16)
                    {
                        curr_settings.aud_gain_ch2  = t16;
                        is_config_changed_from_wlan = 1; 
                    }
                    return SUCCESS;
                case 0x10:
                    t8 = data[0];
                    if(curr_settings.video_cam_voltage != t8)
                    {
                        curr_settings.video_cam_voltage = t8;
                        is_config_changed_from_wlan     = 1;
                    }
                    return SUCCESS;
                case 3:
                    t8 = data[0];
                    if(curr_settings.cam_channel != t8)
                    {
                        curr_settings.cam_channel       = t8;
                        is_config_changed_from_wlan     = 1;
                        if(video_source_num == 2)
                        {
                            // gpio_set_value(change_dcam, t8);
                        }
                    }
                    return SUCCESS;
                case 0x16:
                    memcpy((void*)&t32, data, 4);
                    if(t32 < 500000)
                    {
                        t32 = 500000;
                    }
                    else if (t32 > 2000000)
                    {
                        t32 = 2000000;
                    }
                    temp_framesize = (t32 / ((framerate >> curr_settings.skip_factor)/1000)) >> 3;
                    if(curr_settings.bitrate != t32)
                    {
                        curr_settings.bitrate       = t32;
                        curr_settings.framesize     = temp_framesize; 
                        is_config_changed_from_wlan = 1;
                    }                   
                    return SUCCESS;
                case 0x18:
                    t8 = data[0];
                    // if(curr_settings.video_source != t8)
                    // {
                    //  curr_settings.video_source  = t8;
                    //  is_config_changed_from_wlan = 1;
                    // }
                    return SUCCESS;
                case 0x5F:              
                    logEvent(log_WATCHES_SET);
                    real_time[0] = data[0] & 0x7F;
                    real_time[1] = data[1] & 0x7F;
                    real_time[2] = data[2] & 0x3F;
                    real_time[3] = data[3] & 0x3F;
                    real_time[4] = data[4] & 0x1F;
                    real_time[5] = data[5];     

                    system("/bin/date");

                    sprintf(set_systime_cmd, "/bin/date %02x%02x%02x%02x20%02x.%02x", real_time[4], real_time[3], 
                        real_time[2], real_time[1], real_time[5], real_time[0]);
                    debug("Attention: system date is %s\r\n", set_systime_cmd);
                    system(set_systime_cmd);
                    
                    logEvent(log_WATCHES_NEW_TIME);
                    return SUCCESS;
                default:
                    if((fType >= 0xA1)&&(fType < 0xA1 + MAX_TIMER_NUM))
                    {
                        t8 = fType - 0xA1;
                        if(t8 > MAX_TIMER_NUM)
                        {
                            ERR("Wrong timer number\r\n");
                            return FAILURE;
                        }
                        if(CheckTimer((rec_timer*)data))
                        {
                            return FAILURE;
                        }
                        memcpy(&curr_settings.record_timers[t8], data, sizeof(rec_timer));
                        is_config_changed_from_wlan = 1;
                        return SUCCESS;
                    }
                    return SUCCESS;
            }
        case apdParamCount:
            // for other countable fields
            switch(fType)
            {
                default:
                    break;
            }
            break;
        default:
            ERR("Incorrect work with parameters\r\n");
            return FAILURE;
    }
    return SUCCESS;
}