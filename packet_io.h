/*
 * packet_io.h
 *
 */

#ifndef _PACKET_IO_H
#define _PACKET_IO_H

#include "eth_iface.h"

#include "common.h"

#define PACKET_MAX_SIZE			1514
#define PACKET_MIN_SIZE			8
#define NET_PROT_VERSION		0
#define NETS_PROT_VERSION		1
#define NP_VERSION_BASIC		((NET_PROT_VERSION) << 6)
#define NP_VERSION_SHORT		((NETS_PROT_VERSION) << 6)
#define NP_VERSION_MASK			(3 << 6)
#define NP_TYPE_MASK			3
#define MAX_FIELDS_PER_PACKET	20
#define APP_NAME_LENGTH			20
#define	APP_VERSION				1							// v1.0

typedef struct AppParameterDescr
{
	u8 size;
	u8 flags;
} AppParameterDescr;

extern const char				dev_type_str[];
extern const AppParameterDescr 	AppInterParameterDescr[];
extern const AppParameterDescr 	AppSyncParameterDescr[];
extern const AppParameterDescr 	AppDescrParameterDescr[];

/*
Функции и структуры для осуществления коммуникации приложений на плате с удаленными приложениями
Вспомогательные функции для сборки/разбора сетевых пакетов и 
обработки команд для всех приложений  (классы 0-2 и 3 если задана таблица).
*/

typedef enum AppSyncParameterTypes
{
	parAppSynTSValue	= 0,
	parAppSynTSFreq,
	parAppSynTUValue,
	parAppSynTUFreq,
	parAppSynCount
} AppSyncParameterTypes;

typedef enum AppInterParameterTypes
{
	parAppIntRunStatus = 0,
	parAppIntLastError,
	parAppIntSetAvail,
	parAppIntCount
} AppInterParameterTypes;

enum 
{
	apdParamCheck = 0,
	apdParamSet,
	apdParamCount
};

typedef enum RunStatus
{
	rsOff = 0,
	rsErr,
	rsReady,
	rsActive,
	rsCount
} RunStatus;

typedef enum AppClassTypes
{
	clsAppDescr	= 0,
	clsAppSync	= 1,
	clsAppInter = 2,
	clsAppCfg	= 3
} AppClassTypes;

typedef enum PacketFieldInfoBits
{
	pfNoField 	= 0x01,
//	pfCalcField = 0x02,
} PacketFieldInfoBits;

typedef enum AppParameterDescrFlags
{
	apdReadableOnly = 0x00,
	apdWritable 	= 0x01,
	apdCountable	= 0x02
} AppParameterDescrFlags;

typedef struct AppCfgParameterTable
{
	u8 	field_num;
	u8 	access_flag;				// 0 - read_only, else apdWritable
	u8 	size;						// in bytes
	u8 *data;
} AppCfgParameterTable;

typedef enum AppDescrParameterTypes
{
	parAppDscType	= 0,
	parAppDscAppNumber,
	parAppDscTextID,
	parAppDscStatus,
	parAppDscDevID,
	parAppDscCount
} AppDescrParameterTypes;

typedef struct packet_info
{
	u8	flags;
	u8	command_type;
	u16	sequence_num;
	u32 ssrc;
	u8	data[];
} packet_info;

enum NetPacketTypes
{
	npData 		= 0x00,
	npDataAck	= 0x01,
	npCommand	= 0x02,
	npCommandAck= 0x03
};

enum NetCommandTypes
{
	ncGet = 0x00,
	ncSet = 0x01,
	ncErr = 0x02
};

typedef enum AppServerApplicationTypes
{
	appServer				= 0,
	appVideoSource,
	appVideoReceiver,
	appAudioSource,
	appAudioReceiver,
	appDigitalInPort,
	appDigitalOutPort,
	appAudioVideoRecord,
	appTelemetry,
	appAutoCall,
	
	appSuperVisor,
	appModemService,
	appAppTypesCount
} AppServerApplicationTypes;

typedef enum AppStatus
{/*
	sDisabled		= 0,
	sSelfTesting	,
	sIntPerError 	,
	sExtPerError	,
	sOutOfMemory	,
	sBusy			,
	sPause			,
	sStream			,
	*/
	sDisabled 		= 0,
	sReady		 	,
	sCaptured	  /*,
	sIntPerError 	,
	sOutOfMemory
	*/
} AppStatus;

// TimeStamp and TimeUp parameters
typedef struct app_sync_info
{
	u32		ts_value;
	u32		tu_value;
	u16		ts_freq;
	u16		tu_freq;
} app_sync_info;

typedef struct app_descriptor
{
	AppServerApplicationTypes	type;		
	AppStatus					status;
	u8							run_status;
	u8							last_error;
	char						name[APP_NAME_LENGTH];
	u32							number;
	app_sync_info				sync;
}app_descriptor;

typedef struct session_info
{
	sock_info		host;
	u32				host_ssrc;
	u32				own_ssrc;
} session_info;

typedef struct app_server_interact_struct
{
	app_descriptor app;
	session_info   session;
} app_server_interact_struct;

typedef struct packet_field
{
	u8 	fInfo;
	u8 	fClass;
	u8 	fType;
	u8 	fSize;
	u8 *fData;
} packet_field;

typedef struct packet_info2
{
	u8			flags;
	u8			command_type;
	u16			sequence_num;
	u32 		ssrc;
	const u8   *data;
	u16 		data_size;
} packet_info2;

typedef struct PacketIO 
{
	AppStatus					owner_status;
	app_server_interact_struct *app_serv;
	sock_info 					cur_socket;
	u8							packet[PACKET_MAX_SIZE];
	u8 							field_storage[PACKET_MAX_SIZE];
	u16 						field_storage_pos;
	packet_info2 				p_inf;
	packet_info2 				d_inf;
	packet_field 				src_fld[MAX_FIELDS_PER_PACKET];
	packet_field 				dst_fld[MAX_FIELDS_PER_PACKET];
	AppCfgParameterTable 	   *cfg_table;
	int 						cfg_table_count;
	u8 							isAppOwnerExists;
	u64 						LastReadTimer;
} PacketIO;


// public
void 	PacketIOInit(PacketIO *comm);
void 	PacketIOSetAppStruct(PacketIO *comm, app_server_interact_struct *as);
void 	PacketIOSetOwner(PacketIO *comm, u8 flag);
int 	PacketIOSetParamTable(PacketIO *comm, AppCfgParameterTable *ptr, int num_items);


/*
Функция считывает интерфейс ethernet и обрабатывает пришедшие пакеты. При необходимости вызывает внешние 
функции. Возвращает FAILURE если произошла ошибка при работе, SUCCESS - если все в порядке.
*/
u8 		PacketIOProcessInterfaces(PacketIO *comm);


// private (используем только внутри .c файла)
/*
Осуществляется реакция на пакет
*/
// u8 	ReactOnPacket(PacketIO *comm, int parCount);
/*
Обрабатываются все параметры
*/
// u8 	PacketProcessParams(PacketIO *comm, int srcCount, int *dstCount);
/*
Проверяет все параметры
*/
// u8 	CheckAllParams(PacketIO *comm, app_descriptor *ad, int srcCount);
/*
Обрабатывает все параметры, выпалняет команды
*/
// u8 	ProcessAllParams(PacketIO *comm, app_descriptor *ad, int srcCount, int *dstCount);
/*
Разбор пакета на параметры
*/
// u8 	PacketParse(packet_info2 *pi, packet_field *params, int *parCount, const u8 *buffer, 
// 					int size);

/*
Сборка пакета из параметров
*/
// u8 	PacketAssemble(const packet_info2 *pi, packet_field *params, int parCount, u8 *buffer, 
// 						int *size);

/*
Инициализация структур packet_field перед использованием (free = 0 - без очистки памяти) 
и после использования (free = 1 - с очисткой памяти)
*/
// u8 	PacketStructClear(packet_field *params, int count);
// u8 	PacketCheckHeader(const u8 *packet, int size, packet_info2 *pi);
// u16 	PacketCalcDataSize(const packet_field *params, int parCount, u8 *cSizes, u8 *cCount, 
// 							u8*types);
// u8 	PacketWriteBasic(const packet_info2 *pi, packet_field *params, int parCount, u8 *buffer, 
//							int *size)

// int	GetPosInCfgTable(PacketIO *comm, u8 fType);
// int	CompareStatuses(PacketIO *comm);
// void	CheckReleaseTime(PacketIO *comm);
// int 	ReadPacket(app_server_interact_struct *ad, u16 length, u8 *buffer, sock_info *src);


#endif /* _PACKET_IO_H */