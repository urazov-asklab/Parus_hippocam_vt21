//moov.h

#ifndef	_MOOV_H
#define _MOOV_H

#include "common.h"

// массивы содержащие шаблоны для заполнения некоторых частей mp4 файла, в основном заголовка
extern u8 qta_ftyp[32];
extern u8 qta_mdat[8];

extern u8 qta_moov[116];
extern u8 qta_vt[312];
extern u8 qta_vt_stsd[110];
extern u8 qta_vt_stss[16];
extern u8 qta_vt_stco[16];
extern u8 qta_vt_stsz[20];

#ifdef SOUND_EN
extern u8 qta_st[367];
extern u8 qta_st_stsc[16];
extern u8 qta_st_stsz[20];
extern u8 qta_st_stco[16];
#endif //SOUND_EN

extern u8 qti_stim[8];
extern u8 qti_avcc[8];
extern u8 qti_ctim[8];
extern u8 qti_fps[8];
extern u8 qti_fhgt[8];
extern u8 qti_fwdt[8];
extern u8 qti_vsrn[8];

#endif /* _MOOV_H */