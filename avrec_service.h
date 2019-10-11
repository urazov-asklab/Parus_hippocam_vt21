/*
 * avrec_service.h
 */

#include <ti/sdo/dmai/Rendezvous.h>

#include "packet_io.h"

#ifndef _AVREC_SERVICE_H
#define _AVREC_SERVICE_H

#define	MAX_CARDS_NUM		1
#define MAX_TIMER_NUM		7

#define SD_STATUS_EMPTY		0 	// слот пуст
#define SD_STATUS_ERROR		1	// аппаратная ошибка карты -
// требуется замена карты
#define SD_STATUS_READY		2	// карта готова к работе
#define SD_STATUS_FULL		3	// карта заполнена

typedef enum AppInterErrorTypes
{
	ieNoError = 0,
	ieExtPerihErr,
	ieNoVideo,
	ieErrVidStandard,
	ieNoCardsReady,
	iePeriphBusy,
	ieSystemOverheat
}AppInterErrorTypes;

typedef struct timer_time
{
 	u8 min;
 	u8 hour;
 	u8 date;
 	u8 month;
 	u8 year;
} timer_time;

typedef struct rec_timer // (таймеры пока не реализованы)
{
	timer_time	time_start;
 	timer_time	time_end;
 	u8			enabled;
} rec_timer;

enum app_rec_mode
{
	APP_REC_MODE_IDLE,
	APP_REC_MODE_REC,
	APP_REC_MODE_RECTIMER,
};

typedef struct video_img_settings
{	
	u8		brightness;
	u8		contrast;
	u8		saturation;
	u8		hue;
} video_img_settings;

typedef struct sd_card_info 
{
	u64 	card_size;  	// объем карты в байтах
	u64 	free_space; 	// свободное место на карте в байтах
	u32 	rec_files_num;	// количество файлов записи на карте
	u32 	status;
} sd_card_info;

typedef struct	AVAppSettings
{
	sd_card_info		sd_info;
	video_img_settings 	img_params;
	u16					skip_factor;
	u16					framesize;
	u8 					sound_channels;
	u8 					aud_gain_ch1;
	u8 					aud_gain_ch2;
	u8 					video_cam_voltage;
	u8 					charger_on;
	u8 					charge_level;
	u32					bitrate;
	u8 					video_source;
	u8 					cam_channel;
	u8 					timer_count;
	rec_timer			record_timers[MAX_TIMER_NUM]; 	
 	AppStatus			app_status;
	//u8					rec_mode_flags;
} AVAppSettings;

typedef enum sAVErrCodes
{
	eAVNoError 			= 0,
	eAVVideoBufferErr,
	eAVAudioBufferErr,
	eAVNoVideo,
	eAVAudioBufferInitErr,
	eAVVideoBufferInitErr,
	eAVSDDataWriteErr,
	eAVSDHeaderWriteErr,
	eAVSDDataReadErr,
	eAVSDHeaderReadErr,
	eAVAIMInitErr,
	eAVInternalErr,
	eAVBufferWrongType
} sAVErrCodes;

volatile u8 AVRecPowerOffCondition;

/* Environment passed when creating the thread */
typedef struct AVRecServiceEnv 
{
	app_server_interact_struct *app_descr;
	Rendezvous_Handle 			hRendezvousFinish;
} AVRecServiceEnv;

/* Thread function prototype */
extern void *avRecServiceThrFxn(void *arg);

// public	функции используются в других файлах .c
u8 		AVRecLoadSettings();
u8		AVRecParamFunc(u8 act, u8 fType, u8 *data);
void 	AVRecDataFunc(packet_info2 *pi);			// заглушка на месте функции передачи кадров по ethernet

// private

// u8 		InitComm(AppCfgParameterTable *table, int count);
// u8 		SaveSettings();
// u8		InitService();
// void 	DoStatus();
// void 	DoPowerOffCondition();
// void 	DoConfigChanges();
// int 		ProcessChanges();
// u8 		CheckTimer(rec_timer *tmr);

#endif /* _AVREC_SERVICE_H */