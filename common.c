#include <stdio.h>
#include <fcntl.h>
#include <unistd.h> 
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "common.h"

void fix_record_state(int rec_on)
{
    FILE *pFile;
    pthread_mutex_lock(&start_rec_mutex);
    if(rec_on)
    {
        pFile = fopen("/opt/start_rec", "w");
        if(pFile != NULL)
        {
            fclose(pFile);
            pFile = NULL;
        }   
        debug("RECORD ON\r\n");
    }
    else
    {
        if(remove( "/opt/start_rec" ) != 0 )
        {
            WARN("Error deleting file /opt/start_rec: %s\r\n", strerror(errno));
        }
        sync();
        debug("RECORD OFF\r\n");
    }
    pthread_mutex_unlock(&start_rec_mutex);
}

int get_record_state()
{
    FILE *pFile;
    int state;

    pthread_mutex_lock(&start_rec_mutex);
    // если этот файл существует, то запись пользователь намеренно не останавливал, поэтому запускаем её снова
    pFile = fopen ("/opt/start_rec" , "r" );
    if(pFile != NULL)
    {
        state = 1;
        fclose(pFile);
        pFile = NULL;
    }
    else
    {
        state = 0;
    }
    pthread_mutex_unlock(&start_rec_mutex);
    return state;
}

INLINE void log_threads(char *msg)
{
#ifdef THREADLOG_EN
    struct timeval  tv_log;
    u64             diff_t_log;
    char            logString[256];

    gettimeofday(&tv_log, NULL);
    diff_t_log = ((u64)tv_log.tv_sec * (u64)US_IN_SEC + (u64)tv_log.tv_usec) 
                - ((u64)prev_time.tv_sec * (u64)US_IN_SEC + (u64)prev_time.tv_usec);

    if(diff_t_log < 100000)
    {
        sprintf(logString, "[%.6llu] %s", diff_t_log, msg);
    }
    else
    {
        sprintf(logString, "!!!!!!! [%.6llu] %s", diff_t_log, msg);
    }

    prev_time = tv_log;
    fputs(logString, pFifoLogFile);
#endif
}

void makewaittime(struct timespec *tsp, long seconds, long nanoseconds)
{
    struct timeval now;

    if(nanoseconds >= 1000000000)
    {
        WARN("In function 'makewaittime' 3rd argument failed. Will use default value\r\n");
        nanoseconds = 999999999;
    }
    
    /* get current time */
    gettimeofday(&now, NULL);
    tsp->tv_sec = now.tv_sec;
    tsp->tv_nsec = now.tv_usec * 1000; /* ms to ns */

    /* add time out value */
    tsp->tv_sec += seconds + ((tsp->tv_nsec + nanoseconds) / 1000000000);
    tsp->tv_nsec = (tsp->tv_nsec + nanoseconds) % 1000000000;
}

// read PID of a process from the file
int readPID (const char *pidfile)
{
    FILE *f;
    int   pid;

    f = fopen(pidfile,"r");

    if(f == NULL)
    {
    	ERR("Cannot open PID file %s\r\n", pidfile);
        return 0;
    }

    fscanf(f,"%d", &pid);
    fclose(f);
    return pid;
}

u32 getDeviceAddr()
{
    int fd;

    if(device_addr != 0XFFFFFFFF)
    {
        return device_addr;
    }
    else
    {
        fd = open( "/opt/config", O_RDONLY ); 
        if(fd < 0)
        {
            ERR("Cannot open /opt/config file\r\n");
            return 0XFFFFFFFF;
        }
        // config content: 4 bytes
        // 1) [86210006 => 0x 05 23 75 26 - V(T)21 0000 - device number]
        read(fd, (void*) &device_addr, 4);
        close(fd);
        return device_addr;
    }
}

// int copyFile()
// {
// 	char 	buf[1024];
// 	FILE   *in 	= fopen(infile, "rb");
// 	FILE   *out = fopen(outfile, "wb");
// 	int 	read = 0;

// 	//  Read data in 1kb chunks and write to output file
// 	while ((read = fread(buf, 1, 1024, in)) == 1024)
// 	{
// 	  fwrite(buf, 1, 1024, out);
// 	}

// 	//  If there is any data left over write it out
// 	fwrite(buf, 1, read, out);

// 	fclose(out);
// 	fclose(in);
// }

u64 uptime_ms()
{
    struct timespec ts; 

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec*1000 + (u64)ts.tv_nsec/1000000;
}


u32 uptime()
{
    struct timespec ts; 

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec;
}
