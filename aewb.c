
#include <sys/ioctl.h>
#include <sys/time.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/types.h> 

#include <linux/omap3isp.h>
#include <linux/v4l2-mediabus.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "aewb.h"
#include "media.h"
#include "tools.h"

const __u32 gtable[1024] = {
	#include "gtable.h" // gamma_table
};

typedef struct aewb_thr_dat
{
	struct media_device *mdev;
	
	struct {
		struct media_entity *entity;
	} ccdc;
	struct {
		struct media_entity *entity;
		struct omap3isp_prev_wbal wbal;
	} preview;
	struct omap3_isp_aewb aewb;
	int 	black_level;

	fd_set efds;
}aewb_thr_dat;

aewb_thr_dat thr_data;

#define MEDIA_DEVICE		"/dev/media0"
#define SELECT_TIMEOUT		3				/* in seconds */

static int 	omap3_isp_aewb_configure(struct aewb_thr_dat *aewb_d, u32 saturation);
static int 	omap3_isp_aewb_setup(struct aewb_thr_dat *aewb_d);
static int 	omap3_isp_stats_start(struct aewb_thr_dat *aewb_d);
static int 	omap3_isp_aewb_event(struct aewb_thr_dat *aewb_d);
// static void omap3_isp_aewb_process(struct aewb_thr_dat *aewb_d, void *buffer, size_t size);
static void omap3_isp_aewb_process_alt(struct aewb_thr_dat *aewb_d, void *buffer, size_t size);
// static void omap3_isp_aewb_process_habr(struct aewb_thr_dat *aewb_d, void *buffer, size_t size);
static int 	omap3_isp_preview_setup(struct aewb_thr_dat *aewb_d);
static int 	omap3_isp_preview_set_brightness(struct aewb_thr_dat*aewb_d, unsigned int value);
static int 	omap3_isp_preview_set_contrast(struct aewb_thr_dat*aewb_d, unsigned int value);
static int 	omap3_isp_preview_set_saturation(struct aewb_thr_dat*aewb_d, float value);
static int 	omap3_isp_preview_set_white_balance(struct aewb_thr_dat *aewb_d, float gains[4]);
static int 	omap3_isp_preview_update_white_balance(struct aewb_thr_dat *aewb_d);
static int 	omap3_isp_ccdc_set_black_level(struct aewb_thr_dat *aewb_d, u32 value);

int 	v4l2_subdev_open(struct media_entity *entity);
void 	v4l2_subdev_close(struct media_entity *entity);
int 	v4l2_subdev_set_control(struct media_entity *entity, unsigned int id, int32_t *value);

void *aewbThrFxn(void *arg)
{
	AEWBEnv 	   *envp 	= (AEWBEnv*)arg;
	void 		   *status 	= THREAD_SUCCESS;
	int 			ret;
	int             fd_clr 	= 0;
	// int 			k 		= 0;
	struct timeval 	timeout;

	/* Signal that initialization is done and wait for other threads */
    Rendezvous_meet(envp->hRendezvousInit);

// start_again:
//    usleep(1000);

    thr_data.mdev = media_open(MEDIA_DEVICE, 0);
    if(thr_data.mdev == NULL) 
    {
		ERR("Unable to open media device %s\r\n", MEDIA_DEVICE);
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	thr_data.ccdc.entity = media_get_entity_by_name(thr_data.mdev, "OMAP3 ISP CCDC");
	if(thr_data.ccdc.entity == NULL)
	{
		ERR("Unable to locate ccdc entity.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}
	ret = v4l2_subdev_open(thr_data.ccdc.entity);
	if(ret < 0) 
	{
		ERR("Unable to initialize ccdc engine.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	thr_data.black_level = 0;
	omap3_isp_ccdc_set_black_level(&thr_data, thr_data.black_level);

	thr_data.preview.entity = media_get_entity_by_name(thr_data.mdev, "OMAP3 ISP preview");
	if(thr_data.preview.entity == NULL)
	{
		ERR("Unable to locate preview entity.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	ret = v4l2_subdev_open(thr_data.preview.entity);
	if(ret < 0) 
	{
		ERR("Unable to initialize preview engine.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	thr_data.aewb.entity = media_get_entity_by_name(thr_data.mdev, "OMAP3 ISP AEWB");
	if(thr_data.aewb.entity == NULL)
	{
		ERR("Unable to locate AEWB entity.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}
	ret = v4l2_subdev_open(thr_data.aewb.entity);
	if(ret < 0) 
	{
		ERR("Unable to initialize statistics engine.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	omap3_isp_aewb_configure(&thr_data, (1<<10)-1 - thr_data.black_level);

	ret = omap3_isp_stats_start(&thr_data);
	if(ret < 0) 
	{
		ERR("Unable to start statistics engine.\r\n");
		is_rec_failed       = 1;
        is_rftx_failed      = 1;
        is_stream_failed    = 1;
        is_cap_finishing    = 1;
        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
	}

	omap3_isp_preview_setup(&thr_data);

	FD_ZERO(&thr_data.efds);
	FD_SET(thr_data.aewb.entity->fd, &thr_data.efds);

	fd_clr = 1;

    while(!gblGetQuit(REC_QID | STRM_QID | RFTX_QID)) 
    {   	
    	// настройки цветного цифрового видео 
    	// (эти функции не участвуют при построении баланса белого, 
    	// но работают с превьюером, который доступен из этой нити)
    	omap3_isp_preview_set_brightness(&thr_data, cam_brightness);
    	omap3_isp_preview_set_contrast(&thr_data, cam_contrast);
    	omap3_isp_preview_set_saturation(&thr_data, cam_saturation);

    	timeout.tv_sec 	= SELECT_TIMEOUT; 
		timeout.tv_usec = 0;

		// ждем в течение SELECT_TIMEOUT данные для баланса белого

    	ret = select(thr_data.aewb.entity->fd + 1, NULL, NULL, &thr_data.efds, &timeout); 
    	if(ret < 0) 
    	{			
			if(errno == EINTR)
			{
				WARN("EINTR error\r\n");
				usleep(1000);
				continue;
			}

			ERR("Select failed with %d\r\n", errno);
			is_rec_failed       = 1;
	        is_rftx_failed      = 1;
	        is_stream_failed    = 1;
	        is_cap_finishing    = 1;
	        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
		}
		if(ret == 0) 
		{
			ERR("Select timeout\r\n");
			is_rec_failed       = 1;
	        is_rftx_failed      = 1;
	        is_stream_failed    = 1;
	        is_cap_finishing    = 1;
	        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
		}
		// else
		// {
		// 	debug("AEWB DONE!!!\r\n");
		// }

		// если данные доступны, то ...

		if(FD_ISSET(thr_data.aewb.entity->fd, &thr_data.efds))
		{
			// ... в этой функции на их основе рассчитываем коэффициенты для баланса белого и отправляем их в ISP
			if(omap3_isp_aewb_event(&thr_data) != 0)
			{
				is_rec_failed       = 1;
		        is_rftx_failed      = 1;
		        is_stream_failed    = 1;
		        is_cap_finishing    = 1;
		        cleanup(THREAD_FAILURE, REC_QID | STRM_QID | RFTX_QID);
			}
		}
		// if((k > 1000) && (k < 2000))
		// {
		// 	cleanup(THREAD_FAILURE, 0);
		// }
		// k++;
    }

cleanup:  // завершаем нить

	if((fd_clr == 1) && (thr_data.aewb.entity))
	{
		FD_CLR(thr_data.aewb.entity->fd, &thr_data.efds);
	}

	if(thr_data.aewb.entity)
	{
    	v4l2_subdev_close(thr_data.aewb.entity);
    }
    if(thr_data.preview.entity)
    {
    	v4l2_subdev_close(thr_data.preview.entity);
    }
    if(thr_data.ccdc.entity)
    {
    	v4l2_subdev_close(thr_data.ccdc.entity);
    }
    if(thr_data.mdev)
    {
    	media_close(thr_data.mdev);
    }
    if(thr_data.aewb.buffer)
    {
    	free(thr_data.aewb.buffer);
    }
    thr_data.aewb.buffer = NULL;

 //    if(gblGetQuit(REC_QID | STRM_QID | RFTX_QID)) 
	// {
		is_cap_finishing 	= 1; // флаг процесса останова группы нитей

		debug("AEWB thread finishing...\r\n");

		// if(status == THREAD_FAILURE)
		// {
		// 	is_rec_failed       = 1;
	 //        is_rftx_failed      = 1;
	 //        is_stream_failed    = 1;
	 //        is_cap_finishing 	= 1;
		// }
	    Rendezvous_force(envp->hRendezvousInit);
	    /* Make sure the other threads aren't waiting for us */
	    Rendezvous_meet(envp->hRendezvousCleanup);
	// }
	// else
	// {
	// 	debug("AEWB thread restarting...\r\n");
	// 	// k = 2000;
	// 	goto start_again;
	// }

    debug("AEWB thread finished\r\n");

    return status;
}

static int omap3_isp_aewb_event(struct aewb_thr_dat *aewb_d)
{
	struct omap3_isp_aewb 	   		   *aewb 	= &aewb_d->aewb;
	struct omap3isp_stat_data 			data;
	struct v4l2_event 					event;
	struct omap3isp_stat_event_status  *status 	= (struct omap3isp_stat_event_status *)event.u.data;
	int 								ret;

	memset(&event, 0, sizeof event);
	ret = ioctl(aewb->entity->fd, VIDIOC_DQEVENT, &event);
	if(ret < 0) 
	{
		ERR("Unable to retrieve AEWB event: %s (%d).\r\n", strerror(errno), errno);
		return FAILURE;
	}

	if(status->buf_err) 
	{
		ERR("AEWB: stats error, skipping buffer.\r\n");
		return FAILURE;
	}

	memset(&data, 0, sizeof data);
	data.buf 		= aewb->buffer;
	data.buf_size 	= aewb->size;

	ret = ioctl(aewb->entity->fd, VIDIOC_OMAP3ISP_STAT_REQ, &data);
	if(ret < 0) 
	{
		ERR("Unable to retrieve AEWB data: %s (%d).\n", strerror(errno), errno);
		return FAILURE;
	}

	// omap3_isp_aewb_process(aewb_d, data.buf, data.buf_size);
	omap3_isp_aewb_process_alt(aewb_d, data.buf, data.buf_size);
	//omap3_isp_aewb_process_habr(aewb_d, data.buf, data.buf_size);
	return SUCCESS;
}

// static void omap3_isp_aewb_process(struct aewb_thr_dat *aewb_d, void *buffer, size_t size __attribute__((__unused__)))
// {
// 	struct omap3_isp_aewb 		   *aewb 		= &aewb_d->aewb;
// 	struct omap3_isp_aewb_stats 	stats;
// 	u16 					   	   *data 		= buffer;
// 	u32 							windows;
// 	u32 							i;
// 	u32 							j;

// 	static int fcount = 0;

// 	float 	gains[4];
// 	double 	mean;

// 	memset(&stats, 0, sizeof stats);

// 	/* Number of windows, exclusing black row. */
// 	windows = aewb->win_n_x * aewb->win_n_y;

// 	for(i = 0; i < windows; ++i) 
// 	{
// 		for(j = 0; j < 8; ++j)
// 		{
// 			stats.accum[j] += *data++;
// 		}

// 		if(i % 8 == 7) 
// 		{
// 			for(j = 0; j < 8; ++j)
// 			{
// 				stats.unsat += *data++;
// 			}
// 		}
// 	}

// 	if(windows % 8) 
// 	{
// 		// Skip black row windows up to the next boundary of 8 windows.
// 		data += min(8 - windows % 8, aewb->win_n_x) * 8;
// 		for(j = 0; j < windows % 8; ++j)
// 		{
// 			stats.unsat += *data++;
// 		}
// 	}

// 	stats.npix = div_round_up(aewb->win_w, aewb->win_inc_x) * aewb->win_n_x
// 		   * div_round_up(aewb->win_h, aewb->win_inc_y) * aewb->win_n_y;

// 	/* Automatic White Balance */
// 	/*mean = stats.accum[0] + stats.accum[3]
// 	     + (stats.accum[1] + stats.accum[2]) / 2;*/
// 	mean = stats.accum[1] + stats.accum[2] + (stats.accum[0] + stats.accum[3]) / 2;
// 	mean /= 3;

// 	for(i = 0; i < 4; ++i)
// 	{
// 		gains[i] = mean / stats.accum[i];
// 	}

// 	fcount++;
// 	if(!(fcount % 100))
// 	{
// 		debug("m: %f, g0: %f, g1: %f, g2: %f, g3: %f\r\n",mean, gains[0], gains[1], gains[2], gains[3]);
// 	}

// 	omap3_isp_preview_set_white_balance(aewb_d, gains);
// }

// Методика «серый мир», согласно которой сумма всех цветов на изображении сцены дает серый цвет
static void omap3_isp_aewb_process_alt(struct aewb_thr_dat *aewb_d, void *buffer, 
											size_t size __attribute__((__unused__)))
{
	struct omap3_isp_aewb 		   *aewb 	= &aewb_d->aewb;
	struct omap3_isp_aewb_stats 	stats;
	u16 					   	   *data 	= buffer;
	u32 							windows;
	u32 							i;
	u32 							j;
	u32 							RR 		= 0;
	u32 							GG 		= 0;
	u32 							BB 		= 0;
	// int 							GN[3] 	= { -16, 0, 16};
	// int 							GB[3] 	= {0};
	// int 							GR[3] 	= {0};
	int 							r;
	int 							g;
	int 							b;
	// int 							min;
	// int 							minr;
	// int 							minb;

	static int 		fcount 		= 0;
	static float 	gains[4] 	= {1.0, 1.0, 1.0, 1.0};
	// double mean;

	memset(&stats, 0, sizeof stats);

	#define G0 0
	#define R0 1
	#define B0 2
	#define G1 3

	/* Number of windows, exclusing black row. */
	windows = aewb->win_n_x * aewb->win_n_y;

	for(i = 0; i < windows; ++i) 
	{
		g = (data[G0] + data[G1])/2;
		r = data[R0];
		b = data[B0];

		for(j = 0; j < 8; ++j)
		{
			stats.accum[j] += *data++;
		}
		if(i % 8 == 7) 
		{
			for(j = 0; j < 8; ++j)
			{
				stats.unsat += *data++;
			}
		}

		RR += r; GG += g; BB += b;
		// for(j = 0; j < 3; j++) 
		// {
		// 	GB[j] += abs(g - (b*(512 + GN[j])>>9));
		// 	GR[j] += abs(g - (r*(512 + GN[j])>>9));
		// }
	}

	if(windows % 8) 
	{
		/* Skip black row windows up to the next boundary of 8 windows */
		data += min(8 - windows % 8, aewb->win_n_x) * 8;
		for(j = 0; j < windows % 8; ++j)
		{
			stats.unsat += *data++;
		}
	}

	/* Automatic White Balance */
	RR = RR/windows;
	GG = GG/windows;
	BB = BB/windows;

	// min = GR[0]; minr = 0;
	// for(j = 1; j < 3; j++)
	// {
	// 	if(GR[j] < min) 
	// 	{
	// 		min = GR[j]; minr = j;
	// 	}
	// }
	// min = GB[0]; minb = 0;
	// for(j = 1; j < 3; j++)
	// {
	// 	if(GB[j] < min) 
	// 	{
	// 		min = GB[j]; minb = j;
	// 	}
	// }
	// if(minr != 1) 
	// {
	// 	gains[1] = gains[1] + (GN[minr]*gains[1]/512);
	// }
	// if(minb != 1) 
	// {
	// 	gains[2] = gains[2] + (GN[minb]*gains[2]/512);
	// }

	if(RR) 
	{
		gains[R0] = GG/(float)RR;
	}
    if(BB) 
    {
    	gains[B0] = GG/(float)BB;
    }

	fcount++;
	if(!(fcount % 1000))
	{
		debug("RR: %lu, GG: %lu, BB: %lu, g0: %f, r0: %f, b0: %f, g1: %f\r\n", RR, GG, BB, gains[G0], gains[R0], 
																			gains[B0], gains[G1]);
	}

	omap3_isp_preview_set_white_balance(aewb_d, gains);
}

// static void omap3_isp_aewb_process_habr(struct aewb_thr_dat *aewb_d, void *buffer, 
// 										size_t size __attribute__((__unused__)))
// {

// 	struct omap3_isp_aewb 		   *aewb 		= &aewb_d->aewb;
// 	struct omap3_isp_aewb_stats 	stats;
// 	u16 						   *data 		= buffer;
// 	u32 							windows;
// 	u32 							i;
// 	u32 							j;
// 	u32 							RR 			= 0;
// 	u32 							GG 			= 0;
// 	u32 							BB 			= 0;
// 	// u32 							GB[70] 		= {0};
// 	// u32 							GR[70] 		= {0};
// 	// u32 							GN[70] 		= {0};
// 	u32 							AB[70] 		= {0};
// 	u32 							AR[70] 		= {0};
// 	u32 							AG[70] 		= {0};
// 	u32 							BRDIFF[70] 	= {0};
// 	float 							r;
// 	float 							g;
// 	float 							b;
// 	float 							min_f;
// 	int 							minr;
// 	// int 							minb;

// 	static int 		fcount		= 0;
// 	static float 	gains[4] 	= {1.0, 1.0, 1.0, 1.0};

// 	memset(&stats, 0, sizeof stats);

// 	#define G0 0
// 	#define R0 1
// 	#define B0 2
// 	#define G1 3

// 	/* Number of windows, exclusing black row. */
// 	windows = aewb->win_n_x * aewb->win_n_y;

// 	for(i = 0; i < windows; ++i) 
// 	{
// 		g = (data[G0] + data[G1]) / 2;
// 		r = data[R0];
// 		b = data[B0];

// 		for(j = 0; j < 8; ++j)
// 		{
// 			stats.accum[j] += *data++;
// 		}
// 		if(i % 8 == 7) 
// 		{
// 			for(j = 0; j < 8; ++j)
// 			{
// 				stats.unsat += *data++;
// 			}
// 		}

// 		RR += r; GG += g; BB += b;
		
// 		//GB[i] = abs(g - b);
//         //GR[i] = abs(g - r);

//         BRDIFF[i] 	= abs(g - b) + abs(g - r);        
//         AB[i] 		= b;
//         AR[i] 		= r;
//         AG[i] 		= g;
// 	}

// 	if(windows % 8) 
// 	{
// 		/* Skip black row windows up to the next boundary of 8 windows  */
// 		data += min(8 - windows % 8, aewb->win_n_x) * 8;
// 		for(j = 0; j < windows % 8; ++j)
// 		{
// 			stats.unsat += *data++;
// 		}
// 	}

// 	/* Automatic White Balance */
// 	RR = RR / windows;
// 	GG = GG / windows;
// 	BB = BB / windows;

// 	// min_f = GR[0]; minr = 0;
// 	// for(j = 1; j < windows; j++)
// 	// {
// 	// 	if(GR[j] < min_f) 
// 	// 	{
// 	// 		min_f = GR[j]; minr = j;
// 	// 	}
// 	// }
// 	// min_f = GB[0]; minb = 0;
// 	// for(j = 1; j < windows; j++)
// 	// {
// 	// 	if(GB[j] < min_f)
// 	// 	{
// 	// 		min)f = GB[j]; minb = j;
// 	// 	}
// 	// }

// 	min_f = BRDIFF[0]; minr = 0;

// 	for(j = 1; j < windows; j++)
// 	{
// 		if(BRDIFF[j] < min_f) 
// 		{
// 			min_f = BRDIFF[j]; minr = j;
// 		}
// 	}
// 	gains[R0] = (float)(AG[minr]) / AR[minr];
// 	gains[B0] = (float)(AG[minr]) / AB[minr];

// 	// if(RR)
// 	// { 
// 	// 	gains[R0] = GG/(float)RR;
// 	// }
// 	// if(BB)
// 	// {
// 	// 	gains[B0] = GG/(float)BB;
// 	// }

// 	fcount++;
// 	if(!(fcount % 1000))
// 	{
// 		// debug("minr: %i, minb: %i\r\n", minr, minb);
// 		debug("RR: %lu, GG: %lu, BB: %lu, g0: %f, r0: %f, b0: %f\r\n", RR, GG, BB, gains[G0], gains[R0], gains[B0]);
// 	}

// 	omap3_isp_preview_set_white_balance(aewb_d, gains);
// }

int omap3_isp_ccdc_set_black_level(struct aewb_thr_dat*aewb_d, u32 value)
{
	struct omap3isp_ccdc_update_config 	config;
	struct omap3isp_ccdc_bclamp 		bclamp;
	int 								ret;

	memset(&config, 0, sizeof config);
	config.update = OMAP3ISP_CCDC_BLCLAMP;
	config.flag = 0;
	config.bclamp = &bclamp;

	memset(&bclamp, 0, sizeof bclamp);
	bclamp.dcsubval = value;

	ret = ioctl(aewb_d->ccdc.entity->fd, VIDIOC_OMAP3ISP_CCDC_CFG, &config);
	if(ret < 0) 
	{
		ERR("In function %s - %s (%d)\r\n", __func__, strerror(errno), errno);
		return -errno;
	}

	return ret;
}

static void matrix_zero(struct matrix *m)
{
	unsigned int i, j;

	for (i = 0; i < 3; ++i) {
		for (j = 0; j < 3; ++j)
		{
			m->coeff[i][j] = 0.0;
		}
	}
}

static void matrix_invert(struct matrix *m)
{
	/* Invert the matrix using the transpose of the matrix of cofactors. The
	 * Gauss-Jordan elimination would be faster in the general case, but we
	 * know that the matrix is 3x3.
	 */
	const float eps = 1e-6;
	struct matrix out;
	unsigned int i, j;
	float det;

	out.coeff[0][0] = m->coeff[1][1] * m->coeff[2][2]
			- m->coeff[1][2] * m->coeff[2][1];
	out.coeff[0][1] = m->coeff[0][2] * m->coeff[2][1]
			- m->coeff[0][1] * m->coeff[2][2];
	out.coeff[0][2] = m->coeff[0][1] * m->coeff[1][2]
			- m->coeff[0][2] * m->coeff[1][1];
	out.coeff[1][0] = m->coeff[1][2] * m->coeff[2][0]
			- m->coeff[1][0] * m->coeff[2][2];
	out.coeff[1][1] = m->coeff[0][0] * m->coeff[2][2]
			- m->coeff[0][2] * m->coeff[2][0];
	out.coeff[1][2] = m->coeff[0][2] * m->coeff[1][0]
			- m->coeff[0][0] * m->coeff[1][2];
	out.coeff[2][0] = m->coeff[1][0] * m->coeff[2][1]
			- m->coeff[1][1] * m->coeff[2][0];
	out.coeff[2][1] = m->coeff[0][1] * m->coeff[2][0]
			- m->coeff[0][0] * m->coeff[2][1];
	out.coeff[2][2] = m->coeff[0][0] * m->coeff[1][1]
			- m->coeff[1][0] * m->coeff[0][1];

	det = m->coeff[0][0] * out.coeff[0][0] +
	      m->coeff[0][1] * out.coeff[1][0] +
	      m->coeff[0][2] * out.coeff[2][0];

	if (det < eps)
		return;

	det = 1/det;

	for (i = 0; i < 3; ++i)
		for (j = 0; j < 3; ++j)
			m->coeff[i][j] = out.coeff[i][j] * det;
}

static void matrix_multiply(struct matrix *a, const struct matrix *b)
{
	struct matrix out;

	/* Compute a * b and return the result in a. */
	out.coeff[0][0] = a->coeff[0][0] * b->coeff[0][0]
			+ a->coeff[0][1] * b->coeff[1][0]
			+ a->coeff[0][2] * b->coeff[2][0];
	out.coeff[0][1] = a->coeff[0][0] * b->coeff[0][1]
			+ a->coeff[0][1] * b->coeff[1][1]
			+ a->coeff[0][2] * b->coeff[2][1];
	out.coeff[0][2] = a->coeff[0][0] * b->coeff[0][2]
			+ a->coeff[0][1] * b->coeff[1][2]
			+ a->coeff[0][2] * b->coeff[2][2];
	out.coeff[1][0] = a->coeff[1][0] * b->coeff[0][0]
			+ a->coeff[1][1] * b->coeff[1][0]
			+ a->coeff[1][2] * b->coeff[2][0];
	out.coeff[1][1] = a->coeff[1][0] * b->coeff[0][1]
			+ a->coeff[1][1] * b->coeff[1][1]
			+ a->coeff[1][2] * b->coeff[2][1];
	out.coeff[1][2] = a->coeff[1][0] * b->coeff[0][2]
			+ a->coeff[1][1] * b->coeff[1][2]
			+ a->coeff[1][2] * b->coeff[2][2];
	out.coeff[2][0] = a->coeff[2][0] * b->coeff[0][0]
			+ a->coeff[2][1] * b->coeff[1][0]
			+ a->coeff[2][2] * b->coeff[2][0];
	out.coeff[2][1] = a->coeff[2][0] * b->coeff[0][1]
			+ a->coeff[2][1] * b->coeff[1][1]
			+ a->coeff[2][2] * b->coeff[2][1];
	out.coeff[2][2] = a->coeff[2][0] * b->coeff[0][2]
			+ a->coeff[2][1] * b->coeff[1][2]
			+ a->coeff[2][2] * b->coeff[2][2];

	*a = out;
}

static void matrix_float_to_s10q8(__u16 out[3][3], const struct matrix *in)
{
	u32 i, j;

	for(i = 0; i < 3; ++i) 
	{
		for(j = 0; j < 3; ++j)
		{
			out[i][j] = (__u16)((__s16)(in->coeff[i][j] * 256) & 0x3ff);
		}
	}
}

static void matrix_float_to_s12q8(__u16 out[3][3], const struct matrix *in)
{
	u32 i, j;

	for(i = 0; i < 3; ++i) 
	{
		for(j = 0; j < 3; ++j)
		{
			out[i][j] = (__u16)((__s16)(in->coeff[i][j] * 256) & 0xfff);
		}
	}
}



static const struct matrix omap3isp_preview_csc = {
	.coeff = {
		/* Default values. */
		{  0.2968750,  0.5937500,  0.1093750 },
		{ -0.1718750, -0.3281250,  0.5000000 },
		{  0.5000000, -0.3828125, -0.0781250 },
#if 0
		/* Default values for fluorescent light. */
		{  0.25781250,  0.50390625,  0.09765625 },
		{ -0.14843750, -0.29296875,  0.43750000 },
		{  0.43750000, -0.36718750, -0.07031250 },
#endif
	},
};

static const struct matrix omap3isp_preview_rgb2rgb = {
	.coeff = {
#if 1
		/* Default values. */		
		{ 1.0, 0.0, 0.0 },
		{ 0.0, 1.0, 0.0 },
		{ 0.0, 0.0, 1.0 },
#endif
#if 0	//Matrix from Aptina DevWare
		{  1.50, -0.20, -0.31 },
		{ -0.185, 1.48, -0.66 },
		{ -0.16, -0.81,  4.00 },
#endif
#if 0
		/* Default values for fluorescent light. */
		{  1.88281250, -0.812500, -0.07031250 },
		{ -0.39453125,  1.671875, -0.27734375 },
		{ -0.12500000, -1.250000,  2.37500000 },
#endif
	},
};

int omap3_isp_preview_setup(struct aewb_thr_dat*aewb_d)
{
	struct omap3isp_prev_update_config 	config;
	struct omap3isp_prev_rgbtorgb 		rgb2rgb;
	struct omap3isp_prev_csc 			csc;
	struct omap3isp_prev_gtables 		gamma_tables;

	int ret;

	memset(&config, 0, sizeof config);
	config.update 	= OMAP3ISP_PREV_WB | OMAP3ISP_PREV_RGB2RGB | OMAP3ISP_PREV_COLOR_CONV | OMAP3ISP_PREV_GAMMA;	
	// OMAP3ISP_PREV_GAMMABYPASS;
	config.flag 	= OMAP3ISP_PREV_WB | OMAP3ISP_PREV_RGB2RGB | OMAP3ISP_PREV_COLOR_CONV | OMAP3ISP_PREV_GAMMA;	
	// OMAP3ISP_PREV_GAMMABYPASS;
	config.wbal 	= &aewb_d->preview.wbal;
	config.rgb2rgb 	= &rgb2rgb;
	config.csc 		= &csc;
	config.gamma 	= &gamma_tables;

	memcpy(gamma_tables.red, gtable, OMAP3ISP_PREV_GAMMA_TBL_SIZE*sizeof(__u32));
	memcpy(gamma_tables.green, gtable, OMAP3ISP_PREV_GAMMA_TBL_SIZE*sizeof(__u32));
	memcpy(gamma_tables.blue, gtable, OMAP3ISP_PREV_GAMMA_TBL_SIZE*sizeof(__u32));

	aewb_d->preview.wbal.dgain = 1 << 8;
	aewb_d->preview.wbal.coef0 = 1 << 5;
	aewb_d->preview.wbal.coef1 = 1 << 5;
	aewb_d->preview.wbal.coef2 = 1 << 5;
	aewb_d->preview.wbal.coef3 = 1 << 5;

	memset(&rgb2rgb, 0, sizeof rgb2rgb);
	matrix_float_to_s12q8(rgb2rgb.matrix, &omap3isp_preview_rgb2rgb);

	memset(&csc, 0, sizeof csc);
	matrix_float_to_s10q8(csc.matrix, &omap3isp_preview_csc);

	ret = ioctl(aewb_d->preview.entity->fd, VIDIOC_OMAP3ISP_PRV_CFG, &config);
	if(ret < 0) 
	{
		ERR("In function %s - %s (%d)\r\n", __func__, strerror(errno), errno);
		return -errno;
	}

	return ret;
}

int omap3_isp_preview_set_brightness(struct aewb_thr_dat *aewb_d, unsigned int value)
{
	int brightness = value;
	int ret;

	ret = v4l2_subdev_set_control(aewb_d->preview.entity, V4L2_CID_BRIGHTNESS, &brightness);
	if (ret < 0)
		return -errno;

	return ret;
}
 
int omap3_isp_preview_set_contrast(struct aewb_thr_dat*aewb_d, unsigned int value)
{
	int contrast = value;
	int ret;

	ret = v4l2_subdev_set_control(aewb_d->preview.entity, V4L2_CID_CONTRAST, &contrast);
	if (ret < 0)
		return -errno;

	return ret;
}

int omap3_isp_preview_set_saturation(struct aewb_thr_dat *aewb_d, float value)
{
	struct omap3isp_prev_update_config config;
	struct omap3isp_prev_rgbtorgb rgb2rgb;
	struct matrix saturation;
	struct matrix gain;
	int ret;

	matrix_zero(&gain);
	gain.coeff[0][0] = 1.0;
	gain.coeff[1][1] = value;
	gain.coeff[2][2] = value;

	saturation = omap3isp_preview_csc;
	matrix_invert(&saturation);
	matrix_multiply(&saturation, &gain);
	matrix_multiply(&saturation, &omap3isp_preview_csc);

	memset(&config, 0, sizeof config);
	config.update = OMAP3ISP_PREV_RGB2RGB;
	config.flag = OMAP3ISP_PREV_RGB2RGB;
	config.rgb2rgb = &rgb2rgb;

	memset(&rgb2rgb, 0, sizeof rgb2rgb);
	matrix_float_to_s12q8(rgb2rgb.matrix, &saturation);

	ret = ioctl(aewb_d->preview.entity->fd, VIDIOC_OMAP3ISP_PRV_CFG, &config);
	if (ret < 0)
		return -errno;

	return ret;
}

int omap3_isp_preview_set_white_balance(struct aewb_thr_dat *aewb_d, float gains[4])
{
	aewb_d->preview.wbal.dgain = 1 << 8;
	aewb_d->preview.wbal.coef0 = (__u8)clamp(gains[0] * (1 << 5), 0.0, 255.0);
	aewb_d->preview.wbal.coef1 = (__u8)clamp(gains[1] * (1 << 5), 0.0, 255.0);
	aewb_d->preview.wbal.coef2 = (__u8)clamp(gains[2] * (1 << 5), 0.0, 255.0);
	aewb_d->preview.wbal.coef3 = (__u8)clamp(gains[3] * (1 << 5), 0.0, 255.0);

	return omap3_isp_preview_update_white_balance(aewb_d);
}

int omap3_isp_preview_update_white_balance(struct aewb_thr_dat *aewb_d)
{
	struct omap3isp_prev_update_config 	config;
	int 								ret;

	memset(&config, 0, sizeof config);
	config.update 	= OMAP3ISP_PREV_WB;
	config.flag 	= OMAP3ISP_PREV_WB;
	config.wbal 	= &aewb_d->preview.wbal;

	ret = ioctl(aewb_d->preview.entity->fd, VIDIOC_OMAP3ISP_PRV_CFG, &config);
	if(ret < 0) 
	{
		ERR("In function %s - %s (%d)\r\n", __func__, strerror(errno), errno);
		return -errno;
	}

	return ret;
}

static const struct format_info {
	__u32 code;
	unsigned int bpp;
} formats[] = {
	{ V4L2_MBUS_FMT_SBGGR8_1X8, 8, },
//	{ V4L2_MBUS_FMT_SGBRG8_1X8, 8, },
	{ V4L2_MBUS_FMT_SGRBG8_1X8, 8, },
//	{ V4L2_MBUS_FMT_SRGGB8_1X8, 8, },
	{ V4L2_MBUS_FMT_SBGGR10_1X10, 10, },
	{ V4L2_MBUS_FMT_SGBRG10_1X10, 10, },
	{ V4L2_MBUS_FMT_SGRBG10_1X10, 10, },
	{ V4L2_MBUS_FMT_SRGGB10_1X10, 10, },
};

int omap3_isp_aewb_configure(struct aewb_thr_dat*aewb_d, u32 saturation)
{
	struct omap3_isp_aewb 	 *aewb 		= &aewb_d->aewb;
	struct v4l2_mbus_framefmt format;
//	struct media_entity_pad  *source;

	u32 bpp 		= 10;
	u32 win_n_x 	= 10;
	u32 win_n_y 	= 7;
	u32 win_inc_x;
	u32 win_inc_y;
	u32 win_h;
	u32 win_w;
	u32 win_x;
	u32 win_y;
//	u32 i;
//	int ret;

	format.width 	= 1264;
	format.height 	= 720;

	/* Window width and height are computed by dividing the frame width and
	 * height by the number of windows horizontally and vertically. Make
	 * sure they fall in the admissible ranges by modifying the number of
	 * windows is necessary. Finally, clamp the number of windows to the
	 * admissible range.
	 */
	win_w 	= (format.width / win_n_x) & ~1;
	win_w 	= clamp_t(unsigned int, win_w, OMAP3ISP_AEWB_MIN_WIN_W,
			  OMAP3ISP_AEWB_MAX_WIN_W);
	win_n_x = format.width / win_w;
	win_n_x = clamp_t(unsigned int, win_n_x, OMAP3ISP_AEWB_MIN_WINHC,
			  OMAP3ISP_AEWB_MAX_WINHC);

	win_h 	= (format.height / win_n_y) & ~1;
	win_h 	= clamp_t(unsigned int, win_h, OMAP3ISP_AEWB_MIN_WIN_H,
			  OMAP3ISP_AEWB_MAX_WIN_H);
	win_n_y = format.height / win_h;
	win_n_y = clamp_t(unsigned int, win_n_y, OMAP3ISP_AEWB_MIN_WINVC,
			  OMAP3ISP_AEWB_MAX_WINVC);

	/* Accumulators are 16-bit registers. To avoid overflows limit the
	 * number of pixels to 64 (for 10-bit formats) or 256 (for 8-bit
	 * formats) by increasing the horizontal and vertical increments.
	 */
	win_inc_x = 2;
	win_inc_y = 2;

	while((win_w / win_inc_x) * (win_h / win_inc_y) > 1U << (16 - bpp)) 
	{
		if(win_inc_x <= win_inc_y)
		{
			win_inc_x += 2;
		}
		else
		{
			win_inc_y += 2;
		}
	}

	/* Center the windows in the image area. The black row will be
	 * positionned at the end of the frame. Make sure the position is a
	 * multiple of 2 pixels to keep the Bayer pattern.
	 */
	win_x = ((format.width - win_w * win_n_x) / 2) & ~1;
	win_y = ((format.height - win_h * win_n_y) / 2) & ~1;

	debug("AEWB: #win %lux%lu start %lux%lu size %lux%lu inc %lux%lu\r\n", win_n_x, win_n_y, win_x, win_y, win_w, 
																			win_h, win_inc_x, win_inc_y);

	aewb->win_x 		= win_x;
	aewb->win_y 		= win_y;
	aewb->win_n_x 		= win_n_x;
	aewb->win_n_y 		= win_n_y;
	aewb->win_w 		= win_w;
	aewb->win_h 		= win_h;
	aewb->win_inc_x 	= win_inc_x;
	aewb->win_inc_y 	= win_inc_y;
	aewb->saturation 	= saturation;

	//rect->width 	= win_n_x * win_w;
	//rect->height 	= win_n_y * win_h;

	return 0;
}

int omap3_isp_stats_start(struct aewb_thr_dat*aewb_d)
{
	u32 							enable 	= 1;
	struct omap3_isp_aewb 		   *aewb 	= &aewb_d->aewb;
	struct v4l2_event_subscription 	esub;
	int 							ret;

	ret = omap3_isp_aewb_setup(aewb_d);
	if(ret < 0) 
	{
		ERR("Unable to configure AEWB engine: %s (%d).\r\n", strerror(errno), errno);
		return ret;
	}

	memset(&esub, 0, sizeof esub);
	esub.type = V4L2_EVENT_OMAP3ISP_AEWB;

	ret = ioctl(aewb->entity->fd, VIDIOC_SUBSCRIBE_EVENT, &esub);
	if(ret < 0) 
	{
		ERR("Unable to subscribe to AEWB event: %s (%d).\r\n", strerror(errno), errno);
		return ret;
	}

	ret = ioctl(aewb->entity->fd, VIDIOC_OMAP3ISP_STAT_EN, &enable);
	if(ret < 0) 
	{
		ERR("Unable to start AEWB engine: %s (%d).\r\n", strerror(errno), errno);
		return ret;
	}

	return 0;
}

static int omap3_isp_aewb_setup(struct aewb_thr_dat*aewb_d)
{	
	struct omap3_isp_aewb 		   *aewb = &aewb_d->aewb;
	struct omap3isp_h3a_aewb_config config;
	u32 							buf_size;
	int 							ret;

	memset(&config, 0, sizeof config);
	config.saturation_limit 	= aewb->saturation;
	config.win_width 			= aewb->win_w;
	config.win_height 			= aewb->win_h;
	config.hor_win_count 		= aewb->win_n_x;
	config.ver_win_count 		= aewb->win_n_y;
	config.hor_win_start 		= aewb->win_x;
	config.ver_win_start 		= aewb->win_y;
	config.blk_win_height 		= 2;
	config.blk_ver_win_start 	= aewb->win_y + aewb->win_h * (aewb->win_n_y - 1) + 2;
	config.subsample_hor_inc 	= aewb->win_inc_x;
	config.subsample_ver_inc 	= aewb->win_inc_y;
	config.alaw_enable 			= 0;

	buf_size = (aewb->win_n_x * aewb->win_n_y +
			    (aewb->win_n_x * aewb->win_n_y + 7) / 8 +
		    	 aewb->win_n_x + (aewb->win_n_x + 7) / 8 ) * 16;

	config.buf_size = buf_size;

	ret = ioctl(aewb->entity->fd, VIDIOC_OMAP3ISP_AEWB_CFG, &config);
	if(ret < 0)
	{
		return -errno;
	}

	if(config.buf_size != buf_size)
	{
		debug("AEWB: buf size was %lu, is %u\r\n", buf_size, config.buf_size);
	}

	aewb->size 		= config.buf_size;
	aewb->buffer 	= malloc(config.buf_size);
	if(aewb->buffer == NULL)
	{
		return -ENOMEM;
	}

	return 0;
}

/*
 * v4l2_subdev_open - Open a sub-device
 * @entity: Sub-device media entity
 *
 * Open the V4L2 subdev device node associated with @entity. The file descriptor
 * is stored in the media_entity structure.
 *
 * Return 0 on success, or a negative error code on failure.
 */
int v4l2_subdev_open(struct media_entity *entity)
{
	if(entity->fd != -1)
	{
		return 0;
	}

	entity->fd = open(entity->devname, O_RDWR);
	if(entity->fd == -1) 
	{
		ERR("In function %s - Failed to open subdev device node %s\r\n", __func__, entity->devname);
		return -errno;
	}

	return 0;
}

/*
 * v4l2_subdev_close - Close a sub-device
 * @entity: Sub-device media entity
 *
 * Close the V4L2 subdev device node associated with the @entity and opened by
 * a previous call to v4l2_subdev_open() (either explicit or implicit).
 */
void v4l2_subdev_close(struct media_entity *entity)
{
	if(entity->fd == -1)
	{
		return;
	}

	close(entity->fd);
	entity->fd = -1;
}

/*
 * v4l2_subdev_set_control - Write the value of a control
 * @entity: Subdev-device media entity
 * @id: Control ID
 * @value: Control value
 *
 * Set control @id to @value. The device is allowed to modify the requested
 * value, in which case @value is updated to the modified value.
 *
 * Return 0 on success or a negative error code on failure.
 */
int v4l2_subdev_set_control(struct media_entity *entity, unsigned int id, int32_t *value)
{
	struct v4l2_control ctrl;
	int ret;

	if(entity->fd == -1)
	{
		return -1;
	}

	ctrl.id 	= id;
	ctrl.value 	= *value;

	ret = ioctl(entity->fd, VIDIOC_S_CTRL, &ctrl);
	if (ret < 0) {
		debug("unable to set control: %s (%d).\n",
			strerror(errno), errno);
		return -errno;
	}

	*value = ctrl.value;
	return 0;
}

