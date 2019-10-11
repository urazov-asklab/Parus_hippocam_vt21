
#ifndef __AEWB_H
#define __AEWB_H

#include <ti/sdo/dmai/Rendezvous.h>

typedef struct AEWBEnv
{
	Rendezvous_Handle       hRendezvousInit;		// рандеву всех нитей захвата после инициализации
    Rendezvous_Handle       hRendezvousCleanup;		// рандеву всех нитей захвата перед окончанием работы
} AEWBEnv;

struct matrix 
{
	float coeff[3][3];
};




/*
 * struct omap3_isp_aewb_stats - OMAP3 ISP AEWB statistics
 * @npix: Total number of accumulated pixels
 * @unsat: Number of accumulated unsaturated pixels
 * @accum: Accumulators
 *
 * The first 4 accumulators store the accumulated pixel values for the 4 color
 * components, organized as follows.
 *
 * +---+---+
 * | 0 | 1 |
 * +---+---+
 * | 2 | 3 |
 * +---+---+
 *
 * The next 4 accumulators are similar, but store the accumulated saturated
 * (clipped) pixel values.
 */
struct omap3_isp_aewb_stats 
{
	u32 npix;
	u32 unsat;
	u32 accum[8];
};




struct omap3_isp_aewb 
{
	struct media_entity    *entity;
	u32 					size;
	void 				   *buffer;

	u32 win_x;
	u32 win_y;
	u32 win_n_x;
	u32 win_n_y;
	u32 win_w;
	u32 win_h;
	u32 win_inc_x;
	u32 win_inc_y;

	u32 saturation;
};

extern void *aewbThrFxn(void *arg);


#endif//__AEWB_H
