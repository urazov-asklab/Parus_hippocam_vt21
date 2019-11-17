        
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "indication.h"

int            cnt_snd = 0;
int            cnt_tri = 0;
int            cnt_sd = 0;
int            cnt_nov = 0;


void *indicationThrFxn(void *arg)
{
    // debug("Indication thread started\r\n");

    IndicationEnv  *envp                = (IndicationEnv *) arg;
    void           *status              = THREAD_SUCCESS;
    u64             diff_usecs          = 0;
    int             prev_led            = 0;
    int             ret_value           = 0;
    Command         currentCommand;
    int             fd_led;
    u8              led_red             = 0;
    int             prev_led_red        = 0;
    int             prev_led_orange     = 0;
    int             fd_led_red          = open("/sys/devices/platform/leds-gpio/leds/led_bat/brightness", O_WRONLY);
    int             fd_led_orange       = open("/sys/devices/platform/leds-gpio/leds/led_charge/brightness", O_WRONLY);

    if(fd_led_red < 0) 
    {
        ERR("Cannot open BATTERY LED file\r\n");
        status = THREAD_FAILURE;
        goto cleanup;
    }
    if(fd_led_orange < 0) 
    {
        ERR("Cannot open CHARGER LED file\r\n");
        status = THREAD_FAILURE;
        goto cleanup;
    }
    /* Signal to main thread that initialization is done */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    while(1)
    {
        usleep(50000);

        currentCommand = gblGetCmd();
       	if(currentCommand == FINISH)
		{
			//debug("Indication finishing...\r\n");
            status = THREAD_SUCCESS;
			goto cleanup;
		}
    
        gettimeofday((struct timeval *)&temp_time, NULL);

        diff_usecs = ((u64)temp_time.tv_sec * (u64)US_IN_SEC + (u64)temp_time.tv_usec) 
                    - ((u64)indicate_time.tv_sec * (u64)US_IN_SEC + (u64)indicate_time.tv_usec);

        if((leds_always == 0) && (is_usb_on == 0))
        {
            led_on = 0;
        }
        else if(internal_error == 1)
        {
            if(led_on == 1)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                }
            }
            else
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                }
            }
        }

        else if((is_sd_mounted == 0)||is_sdcard_off_status || ((is_cam_failed) && (is_rec_request))) 
        {
            if(cnt_sd == 0)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_sd++;
                }
            }
            else if(cnt_sd == 1)
            {
                if(diff_usecs >= 700000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_sd++;
                }
            }
            else if(cnt_sd == 2)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_sd++;
                }
            }
            else if(cnt_sd == 3)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_sd = 0;
                }
            }
        }

        else if(is_rec_started == 1)
        {
            if(sound_only != 1)  // audio/video record - long glowing with an interval in one second
            {
                if(led_on == 1)
                {
                    if(diff_usecs >= 900000)
                    {
                        led_on = 0;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                    }
                }
                else
                {
                    if(diff_usecs >= 100000)
                    {
                        led_on = 1;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                    }
                }
            }
            else  // only audio record - alternate long and short glowing
            {
                if(cnt_snd == 0)
                {
                    if(diff_usecs >= 100000)
                    {
                        led_on = 0;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                        cnt_snd++;
                    }
                }
                else if(cnt_snd == 1)
                {
                    if(diff_usecs >= 900000)
                    {
                        led_on = 1;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                        cnt_snd++;
                    }
                }
                else if(cnt_snd == 2)
                {
                    if(diff_usecs >= 900000)
                    {
                        led_on = 0;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                        cnt_snd++;
                    }
                }
                else if(cnt_snd == 3)
                {
                    if(diff_usecs >= 100000)
                    {
                        led_on = 1;
                        gettimeofday((struct timeval *)&indicate_time, NULL);  
                        cnt_snd = 0;
                    }
                }

            }
        }

        else if(is_stream_started  == 1)
        {
            if(cnt_tri == 0)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri++;
                }
            }
            else if(cnt_tri == 1)
            {
                if(diff_usecs >= 500000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri++;
                }
            }
            else if(cnt_tri == 2)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri++;
                }
            }
            else if(cnt_tri == 3)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri++;
                }
            }
            else if(cnt_tri == 4)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri++;
                }
            }
            else if(cnt_tri == 5)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                    cnt_tri = 0;
                }
            }
        }
        
        else
        {
            if(led_on == 1)
            {
                if(diff_usecs >= 100000)
                {
                    led_on = 0;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                }
            }
            else
            {
                if(diff_usecs >= 900000)
                {
                    led_on = 1;
                    gettimeofday((struct timeval *)&indicate_time, NULL);  
                }
            }
        } 

        fd_led = open("/sys/devices/platform/leds-gpio/leds/led_stat/brightness", O_WRONLY);
        if(fd_led < 0) 
        {
            ERR("Cannot open LED file\r\n");
            status = THREAD_FAILURE;
            goto cleanup;
        }

        if((led_on == 1) && (prev_led != 1))
        {
            prev_led = 1;
            ret_value = write(fd_led, "1", 1);
        }
        else if ((led_on == 0) && (prev_led != 0))
        {
            prev_led = 0;
            ret_value = write(fd_led, "0", 1);
        }
        if(ret_value < 0) 
        {
            ERR("Failed to write to LED file: %d\n\r", errno);
            status = THREAD_FAILURE;
            goto cleanup;
        }
        close(fd_led);   

        gettimeofday((struct timeval *)&temp_time2, NULL);

        diff_usecs = ((u64)temp_time2.tv_sec * (u64)US_IN_SEC + (u64)temp_time2.tv_usec) 
                    - ((u64)indicate_time2.tv_sec * (u64)US_IN_SEC + (u64)indicate_time2.tv_usec);

        if((charger_present == 1) && (charger_level != 2))
        {
            if(prev_led_orange != 1)
            {
                ret_value       = write(fd_led_orange, "1", 1);
                prev_led_orange = 1;
            
                if(ret_value < 0) 
                {
                    ERR("Failed to write to CHARGER LED file: %d\n\r", errno);
                    status = THREAD_FAILURE;
                    goto cleanup;
                }

                ret_value       = write(fd_led_red, "0", 1);
                prev_led_red = 0;

                if(ret_value < 0) 
                {
                    ERR("Failed to write to BATTERY LED file: %d\n\r", errno);
                    status = THREAD_FAILURE;
                    goto cleanup;
                }
            }
        }
        else
        {
            if(prev_led_orange != 0)
            {
                ret_value       = write(fd_led_orange, "0", 1);
                prev_led_orange = 0;

                if(ret_value < 0) 
                {
                    ERR("Failed to write to CHARGER LED file: %d\n\r", errno);
                    status = THREAD_FAILURE;
                    goto cleanup;
                }
            }
            if(cnt_ch == 1)
            {
                led_red = 1;
            }
            else
            {
                led_red = 0;
            }
            // else if(charger_level == 0)
            // {
            //     if(led_red == 1)
            //     {
            //         if(diff_usecs >= 100000)
            //         {
            //             led_red = 0;
            //             gettimeofday((struct timeval *)&indicate_time2, NULL);  
            //         }
            //     }
            //     else
            //     {
            //         if(diff_usecs >= 900000)
            //         {
            //             led_red = 1;
            //             gettimeofday((struct timeval *)&indicate_time2, NULL);  
            //         }
            //     }
            // }

            if((led_red == 1) && (prev_led_red  != 1))
            {
                prev_led_red  = 1;
                ret_value = write(fd_led_red, "1", 1);
            }
            else if((led_red == 0) && (prev_led_red != 0))
            {
                prev_led_red = 0;
                ret_value = write(fd_led_red, "0", 1);
            }
            if(ret_value < 0) 
            {
                ERR("Failed to write to BATTERY LED file: %d\n\r", errno);
                status = THREAD_FAILURE;
                goto cleanup;
            }
        }
    }

cleanup:
    close(fd_led_red);
    close(fd_led_orange);
    //debug("Finishing indication ... \r\n");

    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }

    /* Signal to main thread that thread is finishing */
    if(envp->hRendezvousFinishI != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinishI);
    }

    debug("Indication thread finished\r\n");

    return status;
}