
#ifndef _CRYPT_H
#define _CRYPT_H

#include "common.h"

u32 GOST8_actual_key[8];

int  GOST8Init();
void GOST8SetGamma0(u32 g1, u32 g2);
void GOST8CreateSessionKey(u32 *key32);
void GOST8SetSessionKey();
void GOST8SimpleExchEn(u32 *r, int size);
void GOST8SimpleExchDe(u32 *r, int size);
void GOST8GammingEnDe(u32 *r, int size, u32 gpos);
void GOST8GammingLinkEn(u32 *r, int size);
void GOST8GammingLinkDe(u32 *r, int size);
void GOST8Imito(u32 *r, int size, u32 *imito);
void GOST8SetSecretKey();
// u32 GOST8ReturnSessionKey(); //debug function

#endif /* _CRYPT_H */
