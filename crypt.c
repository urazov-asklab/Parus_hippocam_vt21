
// Реализация алгоритмов ГОСТ 28147-89

#include "crypt.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
 
u32 Gamma0[2];
u32 Gamma[2];
u32 CGammaPos;

u8  sbox8[1024];
u8	Session_Crypt_key[32];
u8  Crypt_key[32];

void FillSBox()
{
    int     j, i;

    u8      s[64] = {
        // 0    1     2     3     4     5     6     7    8     9      10    11   12    13    14    15
        0xC7, 0xE0, 0x0A, 0x25, 0xF2, 0xD4, 0x4B, 0x73, 0x68, 0x9E, 0x89, 0x3C, 0x11, 0x56, 0xAF, 0xBD,
        0xA9, 0xEA, 0x14, 0x07, 0x41, 0xC5, 0x56, 0x7D, 0x20, 0x82, 0x6F, 0x9E, 0xB3, 0xFC, 0xD8, 0x3B,
        0x2A, 0x43, 0x0F, 0xFE, 0x90, 0xE6, 0x85, 0x68, 0xA1, 0x1C, 0x3D, 0xDB, 0xC7, 0x79, 0xB2, 0x54,
        0x10, 0x78, 0x5D, 0x45, 0xF3, 0x3E, 0x07, 0x29, 0xEF, 0x8B, 0xDC, 0x64, 0xCA, 0xB1, 0xA6, 0x92};

    for (j = 0; j < 4; j++)
    {
        for (i = 0; i < 256; i++)
        {
            sbox8[(j << 8) + i] 
                        = (s[(j << 4) + (i & 0x0f)] & 0x0f) | (s[(j << 4) + ((i >> 4) & 0x0f)] & 0xf0);
        }
    }
}

int GOST8Init()
{
    //int n;
    int fd;

    fd = open( "/opt/config", O_RDONLY ); 
    if(fd < 0)
    {
        ERR("Failed to open /opt/config file\r\n");
        return FAILURE;
    }

    if(lseek(fd, 4, SEEK_SET) != 4)
    {
        close(fd);
        ERR("Failed to initialize GOST8 from /opt/config file\r\n");
        return FAILURE;
    }

    CGammaPos = 0;

    memset(&Gamma0[0], 0, 2*sizeof(u32));
    memset(&Gamma[0], 0, 2*sizeof(u32));

    FillSBox();

    // основной ключ шифрования хранится в файле /opt/config за 4-мя байтами адреса устройства
    read(fd, Crypt_key, 32);

    // printf("Crypt_key:\r\n");
    // for(n = 0; n < 32; n++)
    // {
    //     if((n % 16) == 0)
    //     {
    //         printf("\r\n");
    //     }
    //     printf(" %02X", Crypt_key[n]);         
    // }
    // printf("\r\n");

    memcpy(GOST8_actual_key, Crypt_key, 8*sizeof(u32));
    close(fd);
    return SUCCESS;    
}

void GOST8SetSessionKey()
{
    memcpy(GOST8_actual_key, Session_Crypt_key, 8*sizeof(u32));
}

u32  GOST8ReturnSessionKey() // debug function
{
    return ((u32)&Session_Crypt_key[0]);
}

void GOST8SetSecretKey()
{
    memcpy(GOST8_actual_key, Crypt_key, 8*sizeof(u32));
}

void BasicStepForward(u32 *r1, u32 *r2)
{
    u32    tr1 = *r1;
    u32    tr2 = *r2;
    u8    *pr1 = (u8*)(&tr1);
    u32    resr1;
    int    k;

    for (k = 0; k < 8; k++)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    *r1 = tr1;
    *r2 = tr2;
}

void Circle16_3(u32 *r1, u32 *r2)
{
    int i;

    for (i = 0; i < 2; i++)
    {
        BasicStepForward(r1, r2);
    }
}

void GOST8CreateSessionKey(u32 *new_key32)
{
	u32    *key32  = (u32*)&Session_Crypt_key[0];
	u8     *key8   = &Session_Crypt_key[0];
    u8      shift;
    u32     cycles;
    u32     rand_val;
    u32     xor_fact;
    u32     t32;
    struct  timespec monotime;
    int     fd;
    int     i;
 
    fd = open( "/dev/urandom", O_RDONLY ); 
    if(fd < 0)
    {
        ERR("Cannot open /dev/urandom file\r\n");
        return;
    }
    read( fd, key8, 32);
    close(fd);

    clock_gettime(CLOCK_MONOTONIC, &monotime);
	cycles     = monotime.tv_nsec;
	xor_fact   = cycles & 0xFFFF;

    clock_gettime(CLOCK_MONOTONIC, &monotime);
    rand_val     = monotime.tv_nsec;

	if(cycles & 0x8000)
    {
		xor_fact = xor_fact | ((rand_val & 0xFFFF)<<16);
    }
	else
    {
		xor_fact = (rand_val & 0xFFFF) | (xor_fact<<16);
    }
		
	shift = cycles & 0x07;                     //0..7
	shift = (cycles >> (16 + shift)) & 0x1F;
	
	//now work with 32bit key	
	for(i = 0; i < 8; i++)
	{
		t32 = key32[i];

        clock_gettime(CLOCK_MONOTONIC, &monotime);
        cycles     = monotime.tv_nsec;

		if(cycles & 1)
		{
			t32 = (t32<<shift) | (t32>>(31-shift));
		}
		else
		{
			t32 = (t32>>shift) | (t32<<(31-shift));
		}		
		key32[i] = t32^xor_fact;
	}
	for(i = 0; i < 8; i += 2)
	{
		Circle16_3(&key32[i], &key32[i+1]);
	}
	
	memcpy(new_key32, Session_Crypt_key, 32);
}

void CircleFFFB(u32 *r1, u32 *r2)
{
    u32    tr1 = *r1;
    u32    tr2 = *r2;
    u8    *pr1 = (u8*)(&tr1);
    u32    resr1;
    int    k;

    for (k = 0; k < 8; k++)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];

        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];

        /*
        pr1[0] = (pr1[0] & 0x0F) | (0xF0 & (sbox8_2[(pr1[0] >> 4) & 0x0F]));
        pr1[0] = (pr1[0] & 0xF0) | (0x0F & sbox8_2[pr1[0] & 0x0F]);

        pr1[1] = (pr1[1] & 0x0F) | (0xF0 & (sbox8_2[0x10 + ((pr1[1] >> 4) & 0x0F)]));
        pr1[1] = (pr1[1] & 0xF0) | (0x0F & sbox8_2[0x10 + (pr1[1] & 0x0F)]);

        pr1[2] = (pr1[2] & 0x0F) | (0xF0 & (sbox8_2[0x20 + ((pr1[2] >> 4) & 0x0F)]));
        pr1[2] = (pr1[2] & 0xF0) | (0x0F & sbox8_2[0x20 + (pr1[2] & 0x0F)]);

        pr1[3] = (pr1[3] & 0x0F) | (0xF0 & (sbox8_2[0x30 + ((pr1[3] >> 4) & 0x0F)]));
        pr1[3] = (pr1[3] & 0xF0) | (0x0F & sbox8_2[0x30 + (pr1[3] & 0x0F)]);
        */

        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    for (k = 0; k < 8; k++)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];

        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];

        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    for (k = 0; k < 8; k++)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];

        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    for (k = 7; k >= 0; k--)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];

        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];

        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    *r1 = tr2;
    *r2 = tr1;
}

void CircleFBBB(u32 *r1, u32 *r2)
{
    u32    tr1 = *r1;
    u32    tr2 = *r2;
    u8    *pr1 = (u8*)(&tr1);
    u32    resr1;
    int    k;

    for (k = 0; k < 8; k++)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
              
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];

        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }

    for (k = 7; k >= 0; k--)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }

    for (k = 7; k >= 0; k--)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }

    for (k = 7; k >= 0; k--)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }

    *r1 = tr2;
    *r2 = tr1;
}

void BasicStepBackward(u32 *r1, u32 *r2)
{
    u32    tr1 = *r1;
    u32    tr2 = *r2;
    u8    *pr1 = (u8*)(&tr1);
    u32    resr1;
    int    k;

    for (k = 7; k >= 0; k--)
    {
        resr1   = tr1;
        tr1    += GOST8_actual_key[k];
        pr1[0]  = sbox8[pr1[0]];
        pr1[1]  = sbox8[(int)pr1[1]+0x100];
        pr1[2]  = sbox8[(int)pr1[2]+0x200];
        pr1[3]  = sbox8[(int)pr1[3]+0x300];
        tr1     = (tr1 << 11) | (tr1 >> (32-11));
        tr1    ^= tr2;
        tr2     = resr1;
    }
    *r1 = tr1;
    *r2 = tr2;
}

void Circle32_3(u32 *r1, u32 *r2)
{
    int    i;
    u32    resr1;

    for (i = 0; i < 3; i++)
    {
        BasicStepForward(r1, r2);
    }
    BasicStepBackward(r1, r2);

    resr1    = *r1;
    *r1      = *r2;
    *r2      = resr1;
}

/*
void Circle32_P(u32 *r1, u32 *r2)
{
    int    i;
    u32    resr1;

    BasicStepForward(r1, r2);
    
    for (i = 0; i < 3; i++)
    {
        BasicStepBackward(r1, r2);
    }

    resr1    = *r1;
    *r1      = *r2;
    *r2      = resr1;
}*/

void GOST8SimpleExchEn(u32 *r, int size)
{
    int i;
    for (i = 0; i < size/4; i += 2)
    {
        CircleFFFB(&r[i], &r[i+1]);
    }
}

void GOST8SimpleExchDe(u32 *r, int size)
{
    int i;

    for (i = 0; i < size/4; i += 2)
    {
        CircleFBBB(&r[i], &r[i+1]);
    }
}

void GOST8SetGamma0(u32 g1, u32 g2)
{
    Gamma0[0] = g1;
    Gamma0[1] = g2;
    CircleFFFB(&Gamma0[0], &Gamma0[1]);
    CGammaPos = 0xffffffff;
}

void GetGamma(u32 *r1, u32 *r2, u32 gpos)
{
    u32 g1;
    u32 c0 = 0x1010101;
    u32 c1 = 0x1010104;

    if (CGammaPos > gpos)
    {
        CGammaPos = 0xffffffff;
    }
    if (CGammaPos == 0xffffffff)
    {
        Gamma[0] = Gamma0[0] + c0;
        Gamma[1] = Gamma0[1] + c1;
        if (Gamma[1] < Gamma0[1])
        {
            Gamma[1]++;
        }
        CGammaPos = 0;
    }
    while (CGammaPos < gpos)
    {
        Gamma[0] = Gamma[0] + c0;
        g1 = Gamma[1] + c1;
        if (g1 < Gamma[1])
        {
            g1++;
        }
        Gamma[1] = g1;
        CGammaPos += 8;
    }
    *r1 = Gamma[0];
    *r2 = Gamma[1];
}

void GOST8GammingEnDe(  u32    *r,          // data address for encryption
                        int     size,       // data size
                        u32     gpos)       // 0
{
    u32 g[2];        
    u32 gp = gpos & 0xfffffffc;
    u32 rp = 0;
    u32 sz = (size >> 2);
    
    if ((gp & 4) != 0)
    {
        GetGamma(&g[0], &g[1], gp - 4);
        CircleFFFB(&g[0], &g[1]);
        gp      += 4;
        r[rp++] ^= g[1];
        sz--;
    }

    for (; sz >= 2; sz -= 2, rp += 2, gp += 8)
    {
        GetGamma(&g[0], &g[1], gp);
        CircleFFFB(&g[0], &g[1]);
        r[rp]   ^= g[0];
        r[rp+1] ^= g[1];
    }
            
    if (sz != 0)
    {
        GetGamma(&g[0], &g[1], gp);
        CircleFFFB(&g[0], &g[1]);
        r[rp] ^= g[0];
    }
}

void GOST8GammingLinkEn(u32 *r, int size)
{
    u32 g[2];
    int i;

    GetGamma(&g[0], &g[1], 0);
    for (i = 0; i < size/4; i += 2)
    {
      Circle32_3(&g[0], &g[1]);
      r[i]     ^= g[0];
      r[i+1]   ^= g[1];
      g[0]      = r[i];
      g[1]      = r[i+1];
    }
}

void GOST8GammingLinkDe(u32 *r, int size)
{
    u32    g[2]; 
    u32    tempg[2];
    int    i;

    GetGamma(&g[0], &g[1], 0);
    for (i = 0; i < size/4; i += 2)
    {
      Circle32_3(&g[0], &g[1]);
      tempg[0]   = r[i];
      tempg[1]   = r[i+1];
      r[i]      ^= g[0];
      r[i+1]    ^= g[1];
      g[0]       = tempg[0];
      g[1]       = tempg[1];
    }
}

void GOST8Imito(u32 *r, int size, u32 *imito)
{
	u32 rp = 0;
    u32 sz = (size >> 2);
	
	for (; sz >= 2; sz -= 2, rp += 2)
    {
    	// unencrypted packet data xor with authentication code
    	imito[0] ^= r[rp];
    	imito[1] ^= r[rp+1];

        BasicStepForward(&imito[0], &imito[1]);   // 8 encryption cycles
        BasicStepForward(&imito[0], &imito[1]);   // 8 encryption cycles
    }
}