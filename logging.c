        
#include <string.h>
#include <unistd.h>
#include <semaphore.h>
#include <stdlib.h>
#include <errno.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "common.h"
#include "logging.h"

#define LOG_ENTRY_SIZE  5   // in bytes

extern int errno;
long curLogFilePos;


int writeEvent()
{
    FILE   *pFile;
    FILE   *pFilePos;
    u8      buf[LOG_ENTRY_SIZE];

    Event*  ev = &(evtBuf.eventItem[evtBuf.head]);

    if(ev->event_code != log_NO_EVENT)
    {
        // на основе события заполняем структуру для записи в файл
	    buf[0] = ((ev->year%10) << 4) | ev->mon;
	    buf[1] = (ev->day << 3)  | (ev->hour >> 2);
	    buf[2] = (ev->hour << 6) | (ev->min);
	    buf[3] = ev->sec << 2;
	    buf[4] = ev->event_code;


        // очищаем событие в круговом буфере
        evtBuf.eventItem[evtBuf.head].event_code = log_NO_EVENT;

        evtBuf.head++;
        if(evtBuf.head == RING_BUF_SIZE)
        {
            evtBuf.head = 0;
        }

        pFile = fopen("/opt/device.log", "rb+");
        if (pFile == NULL)
        {
            ERR("Cannot open log file\r\n");
            return FAILURE;
        }

        if(curLogFilePos >= MEGABYTE)
        {
            curLogFilePos = 0;
        }

        // debug("Current log file position: %x\r\n", curLogFilePos);
        // debug("File pos: %i\r\n", curLogFilePos);

        if( fseek (pFile , curLogFilePos , SEEK_SET) != 0)
        {
            ERR("Invalid log file\r\n");
            fclose(pFile);
            pFile = NULL;
            return FAILURE;
        }

	    if(fwrite(buf, sizeof(u8), LOG_ENTRY_SIZE, pFile) != LOG_ENTRY_SIZE)
	    {
	        ERR("Logging failed. Writing to the file finished with error\r\n");
	        fclose(pFile);
            pFile = NULL;
	        return FAILURE;
	    }

        // записали событие в лог-файл

        curLogFilePos += LOG_ENTRY_SIZE;

        pFilePos = fopen("/opt/logpos", "rb+");
        if (pFilePos == NULL)
        {
            ERR("Cannot open logpos file\r\n");
            fclose(pFile);
            pFile = NULL;
            return FAILURE;
        }

        if(fseek(pFilePos , 0 , SEEK_SET) != 0)
        {
            ERR("Invalid logpos file\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }

        if(fwrite(&curLogFilePos, sizeof(char), 4, pFilePos) != 4)
        {
            ERR("Logging failed. Writing to the file finished with error\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }

        // записали позицию последнего события в лог-файле -
        // для того, чтобы после прерывания питания начать писать лог не с начала

	    fclose(pFile);
        pFile       = NULL;
        fclose(pFilePos);
        pFilePos    = NULL;
        
    	return SUCCESS;
    }
    return FAILURE;
}

// если лог-файл не создан или поврежден - создаем новый, иначе открываем старый
// если же лог-файл превысил 1Мб, то обнуляем позицию события
int initLogFile()
{
    FILE   *pFile;
    FILE   *pFilePos;
    char   *nullContent;
    int     readFPos        = 0;
    int     bytesToWrite    = 0;

    pFile = fopen("/opt/device.log", "ab+");
    if (pFile == NULL)
    {
        ERR("Cannot open log file\r\n");
        return FAILURE;
    }

    if( fseek (pFile , 0 , SEEK_END) != 0)
    {
        ERR("Invalid log file\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    curLogFilePos = ftell (pFile);
    if(curLogFilePos < 0)
    {
        ERR("Invalid log file size\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    pFilePos = fopen("/opt/logpos", "rb+");
    if (pFilePos == NULL)
    {
        int FPos = 0;
        WARN("Cannot open logpos file\r\n");

        pFilePos = fopen("/opt/logpos", "wb+");

        if (pFilePos == NULL)
        {
            ERR("Cannot create logpos file\r\n");
            fclose(pFile);
            pFile = NULL;
            return FAILURE;
        }
        if((fwrite(&FPos, sizeof(char), 4, pFilePos)) != 4)
        {
            ERR("Cannot write to logpos file\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }
    }

    debug("Log file size: %ld\r\n", curLogFilePos);

    bytesToWrite = MEGABYTE-curLogFilePos;

    if(curLogFilePos > MEGABYTE)
    {
        curLogFilePos = 0;
    }
    else if(curLogFilePos < MEGABYTE)
    {
        nullContent = (char*) malloc (sizeof(char)*bytesToWrite);
        if(nullContent == NULL)
        {
            ERR("Cannot allocate memory for log file\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }

        memset(nullContent, 0, sizeof(char)*bytesToWrite);
        
        if(fwrite (nullContent, sizeof(char), bytesToWrite, pFile) != bytesToWrite)
        {
            ERR("Writing to the log file finished with error\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            free(nullContent);
            return FAILURE;
        }
        free(nullContent);
    }
    else if (curLogFilePos == MEGABYTE)
    {
        if( fseek (pFilePos , 0 , SEEK_SET) != 0)
        {
            ERR("Invalid log file\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }
        if( fread (&readFPos, sizeof(char), 4, pFilePos) != 4)
        {
            ERR("Cannot read log file\r\n");
            fclose(pFile);
            pFile       = NULL;
            fclose(pFilePos);
            pFilePos    = NULL;
            return FAILURE;
        }
        curLogFilePos = readFPos;
        debug("Current log file position: %x\r\n", curLogFilePos);
    }

    fclose(pFilePos);
    pFilePos    = NULL;
    fclose(pFile);
    pFile       = NULL;
    return SUCCESS;
}

void *loggingThrFxn(void *arg)
{
    // debug("Logging thread started\r\n");

    LoggingEnv     *envp                = (LoggingEnv *) arg;
    void           *status              = THREAD_SUCCESS;
    int             err;
    Command         currentCommand;
    Event           curEvent;
    struct timeval  start_thread_time;
    struct timeval  end_thread_time;

    /* Signal to main thread that initialization is done */
    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_meet(envp->hRendezvousInit);
    }

    // если чтение лог-файла завершилось с ошибкой, 
    // то удаляем лог-файл и файл, хранящий позицию, чтобы потом создать новые
    if(initLogFile() != 0) 
    {
        err = remove("/opt/device.log");
        if(err !=0)
        {
            ERR("File /opt/device.log cannot be deleted: %s\r\n", strerror(errno));
        }
        err = remove("/opt/logpos");
        if(err !=0)
        {
            ERR("File /opt/logpos cannot be deleted: %s\r\n", strerror(errno));
        }
        ERR("Failed to init log file\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, NOBODY_QID);
    }

    while(1)
    {
        gettimeofday(&start_thread_time, NULL); 
        currentCommand = gblGetCmd();

        if((currentCommand == FINISH) || (currentCommand == SLEEP))
        {
            //debug("Logging thread finishing ...\r\n");
            goto cleanup;
        }

        log_threads("waiting logmsg\r\n");
        sem_wait(&semaphore);
        log_threads("got logmsg\r\n");
        writeEvent(&curEvent);

        gettimeofday(&end_thread_time, NULL); 
        sprintf((char *)fifoLogString, "Iter logging time(usec): %llu\r\n",
            ((u64)end_thread_time.tv_sec * (u64)US_IN_SEC + (u64)end_thread_time.tv_usec) 
                - ((u64)start_thread_time.tv_sec * (u64)US_IN_SEC + (u64)start_thread_time.tv_usec));
        log_threads((char *)fifoLogString);
    }
    //debug("Finishing logging ... \r\n");
cleanup:
    sync();

    if(envp->hRendezvousInit != NULL)
    {
        Rendezvous_force(envp->hRendezvousInit);
    }
        /* Signal to main thread that logging is finishing */
    if(envp->hRendezvousFinishLG != NULL)
    {
        Rendezvous_meet(envp->hRendezvousFinishLG);
    }
    debug("Logging thread finished\r\n");
    return status;
}