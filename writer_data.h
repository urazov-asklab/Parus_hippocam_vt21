/*
 * writer_data.h
 *
*/

#ifndef _WRITER_DATA_H
#define _WRITER_DATA_H

// интерфейс, который использует библиотека live для стриминга по сети

#define AV_FLAGS_MP4_I_FRAME	1

typedef struct 
{
	unsigned long 		serial;
	int 				flags;
	unsigned long 		size;
	char   			   *ptr;
	unsigned long long 	timestamp;
}AV_DATA;

int GetMP4Vol(AV_DATA*avdata);
int GetMP4_Frame(int num, AV_DATA*avdata);
int GetMP4_I_Frame(AV_DATA*avdata);
int GetMP4_Serial(AV_DATA*avdata);
int FreeMP4_Frame(int num);

int GetAACSerial(AV_DATA*avdata);
int GetAACFrame(int num, AV_DATA*avdata);
int FreeAACFrame(int num);


#endif /* _WRITER_DATA_H */
