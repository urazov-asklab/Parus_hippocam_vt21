//ask_rf3.h

#ifndef __ASK_RF3_H
#define __ASK_RF3_H


int init_rf();
void release_rf();

typedef enum ctrl_chan				// control channels
{
	CTRL_RECORD,					// 0x00 – record control
	CTRL_MODE,						// 0x01 – operation mode control (auto shutdown/always on)
	CTRL_WIFI,						// 0x02 – embedded wifi control
	CTRL_LTE,						// 0x03 – USB modem control
	CTRL_LAN,						// 0x04 – LAN control
	CTRL_GSM,						// 0x05 – embedded GSM control
	CTRL_ACAM_NUM,                  // 0x06 - analog camera channel
    //CTRL_VIBRO,						// 0x06 - external vibro signal control
	CTRL_TIMERS 			= 0x0F,	// 0x0F - timers control
	CTRL_DISC,						// 0x10 - information storage control
	CTRL_SENS_OPEN,					// 0x11 - spoilage sensor control
	CTRL_DRY_CONT,					// 0x12 - dry contact sensor control
	CTRL_ACCEL,						// 0x13 - accelerometer control
	CTRL_SAFETY_VIDEO,				// 0x14 - video safety sensor control
	CTRL_SAFETY_MIC,				// 0x15 - microphone safety sensor control
	CTRL_MOTION_DETECT,				// 0x16 - motion sensor control
	CTRL_SOUND_DETECT,				// 0x17 - sound sensor control
	CTRL_SYSTEM_READ_TEXT 	= 0x20,	// 0x20 - system read text channel
	CTRL_EMPTY 				= 0xFF,	// 0xFF – empty channel
} ctrl_chan;



#define ASK_RF_1            0x00
#define ASK_RF_2            0x01
#define ASK_RF_3            0x02
#define ASK_RF_SHIFT        2

#define MODE_OFF            0x00
#define MODE_MANUAL         0x01
#define MODE_ASK_RF         0x02
#define MODE_SHIFT          0



//CC1310 internal registers(SW emulated)
//R0x00-0x07 - for manual mode
#define REG_POWER           0x00
#define REG_PREAMB_L        0x01
#define REG_PREAMB_H        0x02
#define REG_SYNC_WORD_L     0x03
#define REG_SYNC_WORD_H     0x04
#define REG_BAUDRATE        0x05
#define REG_FREQ            0x06
#define REG_HWADDR          0x07

#define ACK_CMD_STAT        0x10
#define ACK_CTRL_STAT       0x11
#define ACK_DEV_STAT        0x12
#define ACK_PWR_STAT        0x13
#define ACK_PWR_TIME_L      0x14
#define ACK_PWR_TIME_H      0x15
#define ACK_REC_STAT        0x16
#define ACK_REC_TIME_L      0x17
#define ACK_REC_TIME_H      0x18

#define RF_MODE             0x20
#define RF_CONNECT_TIMEOUT  0x21
#define RF_CONFIRM_TIMEOUT  0x22
#define RF_ACK_TIMEOUT      0x23
#define RF_WOR_CONFIG       0x24

#define RF_CMD_STAT         0x27
#define RF_CMD_CHANNEL      0x28
#define RF_CMD_PARAM_0      0x29
#define RF_CMD_PARAM_1      0x2A
#define RF_CMD_CONFIRM      0x2B
#define RF_CMD_NOP          0x2C


//RF_STAT FIELDS
#define RF_STAT_READY       0x80

#define RS_MODE_OFF         0x00
#define RS_MODE_MANUAL      0x01
#define RS_MODE_ASKRF       0x02
#define RS_MODE_SHIFT       0
#define RS_MODE_MASK        0x03

#define RS_STAT_WOR         0x00
#define RS_STAT_RX          0x01
#define RS_STAT_WACK        0x02
#define RS_STAT_WCMD        0x03
#define RS_STAT_BUSY        0x07
#define RS_STAT_SHIFT       2
#define RS_STAT_MASK        0x1C

//RF_CMD_STAT fields
#define RCS_CMD_RECEIVED   0x01

#endif//__ASK_RF3_H
