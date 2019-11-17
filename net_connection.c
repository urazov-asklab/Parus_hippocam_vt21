
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <xdc/std.h>

#include <ti/sdo/dmai/Rendezvous.h>

#include "logging.h"
#include "wis_stream.h"
#include "avrec_service.h"
#include "setting_server.h"
#include "ucp_service.h"
#include "common.h"
#include "net_connection.h"

#define SETTINGSERVERTHREADCREATED  0x1
#define AVRECSERVICETHREADCREATED   0x2
#define UCPSERVICETHREADCREATED   	0x4
#define WISSTREAMTHREADCREATED    	0x8

#define MODE_SET                    1
#define SSID_SET                    2
#define PASS_SET                    4
#define GOT_ALL_INFO                7


#define RANDEZVOUS_DEL(r)  if(r != NULL) { Rendezvous_delete(r); r = NULL; }

#define THREAD_JOIN(m, r, th) if(initMask & m) { \
                                    if(r != NULL)  Rendezvous_meet(r); \
                                    if(pthread_join(th, &ret) == 0) \
                                    { \
                                        if(ret == THREAD_FAILURE) \
                                        { \
                                            status = THREAD_FAILURE; \
                                            WARN("Failed to stop %s\r\n", #th); \
                                        } \
                                    } \
                                }
#define THREAD_JOIN_EX(m, r, th, fn) if(initMask & m) { \
                                    fn(); \
                                    if(r != NULL)  Rendezvous_meet(r); \
                                    if(pthread_join(th, &ret) == 0) \
                                    { \
                                        if(ret == THREAD_FAILURE) \
                                        { \
                                            status = THREAD_FAILURE; \
                                            WARN("Failed to stop %s\r\n", #th); \
                                        } \
                                    } \
                                }

u32 last_time_connected = 0;

// найти в resource вхождение search и заменить его на replace
char *str_replace(char *search, char *replace, char *resource)
{
    size_t  i;
    size_t  pointer_len;
    size_t  resource_len;
    size_t  search_len      = strlen(search);
    size_t  replace_len     = strlen(replace);     
    char   *pointer         = (resource == NULL)? NULL : strstr(resource, search);         
    char   *new_text;
    int     linelen;

    if((pointer == NULL) || (resource == NULL))
    {
        ERR("Function 'str_replace' got some null pointers\r\n");
        return NULL;
    }

    for(i = 0; ; i++)
    {
        if(pointer[search_len + i] == '\n')
        {
            search_len += i;
            break;
        }
    }

    pointer_len     = strlen(pointer);
    resource_len    = strlen(resource);
     
    linelen         = resource_len - search_len + replace_len;
    new_text        = (char*)calloc((linelen + 1), sizeof(char));
    if(new_text == NULL)
    {
        ERR("Cannot allocate memory for new_text\r\n");
        return NULL;
    }
        
    strncpy(new_text, resource, resource_len - pointer_len);
    strcat(new_text, replace);
    strcat(new_text, pointer + search_len);
    new_text[linelen] = '\0';

    return new_text;
}

// подредактировать настройки самба-сервера
int set_netbios_name()
{
    char    new_text[512];
    char    addr_str[10];
    char    str[]           = "PA0000";
    FILE   *pFile;
    char   *line;
    char   *tmpline;
    u32     dev_addr;
    int     iters           = 0;
    long    linesize        = 0;

    dev_addr = getDeviceAddr();
    sprintf(addr_str, "%08lu", dev_addr);
    memcpy(&str[2], &addr_str[4], 4);
    debug("NETBIOS NAME: %s\r\n", str);

    // на случай, если текущий файл испорчен - восстанавливаем его из образца
    system("cp /etc/smb0.conf /etc/smb.conf");
    //usleep(100000);

start0:
    iters++;
    if(iters > 5)
    {
        return FAILURE;
    }
    pFile = fopen("/etc/smb.conf","r");
    if(pFile == NULL)
    {
        WARN("Failed to open /etc/smb.conf to read. Creating new one...\r\n");
        system("cp /etc/smb0.conf /etc/smb.conf");
        goto start0;
    }

    if(fseek(pFile, 0, SEEK_END) != 0)
    {
        WARN("Invalid file /etc/smb.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/smb0.conf /etc/smb.conf");
        goto start0;
    }
    linesize = ftell(pFile);
    if(linesize < 1)
    {
        WARN("Invalid size of file /etc/smb.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/smb0.conf /etc/smb.conf");
        goto start0;
    }

    line = (char *) malloc(linesize + 1);
    if(line == NULL)
    {
        WARN("Cannot allocate memory for line\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_SET) != 0)
    {
        WARN("Invalid file /etc/smb.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/smb0.conf /etc/smb.conf");
        goto start0;
    }

    fread(line, 1, linesize, pFile);
    line[linesize] = '\0';
    fclose(pFile);
    pFile = NULL;

    sprintf(new_text, "netbios name = %s", str);

    tmpline = str_replace("netbios name = ", new_text, line);
    if(tmpline == NULL)
    {
        free(line);
        return FAILURE;
    }

    free(line);
    line = tmpline;
    
    pFile = fopen("/etc/smb.conf", "w");
    if(pFile == NULL)
    {
        free(line);
        WARN("Failed to open /etc/smb.conf to write\r\n");
        return FAILURE;
    }
    fprintf(pFile, "%s", line);
    fclose(pFile);
    pFile = NULL;
    free(line);
    return SUCCESS;
}

// установить имя и пароль точки доступа, к которой подключимся по wifi
int set_wifi_net()
{
    char    new_text[512];
    char   *line;
    char   *tmpline;
    FILE   *pFile;
    int     iters           = 0;
    long    linesize        = 0;

    if(strlen((const char *)wifissid) < 1)
    {
        WARN("Wrong SSID for abonent mode\r\n");
        return FAILURE;
    }

    if(strlen((const char *)wifipass) < 8)
    {
        WARN("Wrong wifi password for abonent mode (8-12 characters needed for WPA)\r\n");
        return FAILURE;
    }

    debug("WIFI SSID: %s\r\n", wifissid);
    debug("WIFI PASS: %s\r\n", wifipass);

    // на случай, если текущий файл испорчен - восстанавливаем его из образца
    system("cp /etc/wpa_supplicant0.conf /etc/wpa_supplicant.conf");
    usleep(1000000);

start1:
    iters++;
    if(iters > 5)
    {
        return FAILURE;
    }
    pFile = fopen("/etc/wpa_supplicant.conf","r");
    if(pFile == NULL)
    {
        WARN("Failed to open /etc/wpa_supplicant.conf to read. Creating new one...\r\n");
        system("cp /etc/wpa_supplicant0.conf /etc/wpa_supplicant.conf");
        goto start1;
    }

    if(fseek(pFile, 0, SEEK_END) != 0)
    {
        WARN("Invalid file /etc/wpa_supplicant.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/wpa_supplicant0.conf /etc/wpa_supplicant.conf");
        goto start1;
    }
    linesize = ftell(pFile);
    if(linesize < 1)
    {
        WARN("Invalid size of file /etc/wpa_supplicant.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/wpa_supplicant0.conf /etc/wpa_supplicant.conf");
        goto start1;
    }

    line = (char *) malloc(linesize + 1);
    if(line == NULL)
    {
        WARN("Cannot allocate memory for line\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_SET) != 0)
    {
        WARN("Invalid file /etc/wpa_supplicant.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/wpa_supplicant0.conf /etc/wpa_supplicant.conf");
        goto start1;
    }

    fread(line, 1, linesize, pFile);
    line[linesize] = '\0';
    fclose(pFile);
    pFile = NULL;

    sprintf(new_text, "ssid=\"%s\"", wifissid);

    tmpline = str_replace("ssid=", new_text, line);
    if(tmpline == NULL)
    {
        free(line);
        return FAILURE;
    }

    free(line);
    line = tmpline;

    sprintf(new_text, "psk=\"%s\"", wifipass);

    tmpline = str_replace("psk=", new_text, line);
    if(tmpline == NULL)
    {
        free(line);
        return FAILURE;
    }

    free(line);
    line = tmpline;
    
    pFile = fopen("/etc/wpa_supplicant.conf", "w");
    if(pFile == NULL)
    {
        free(line);
        WARN("Failed to open /etc/wpa_supplicant.conf to write\r\n");
        return FAILURE;
    }
    
    fprintf(pFile, "%s", line);
    fclose(pFile);
    pFile = NULL;
    free(line);
    return SUCCESS;
}

// установить имя и пароль для точки доступа на устройстве
int set_access_point()
{
    char    new_text[512];
    char   *line;
    char   *tmpline;
    FILE   *pFile;
    int     iters           = 0;
    long    linesize        = 0;

    if(strlen((const char *)wifissid) < 1)
    {
        WARN("Wrong SSID for access point mode\r\n");
        return FAILURE;
    }

    if(strlen((const char *)wifipass) < 8)
    {
        WARN("Wrong password for access point mode (8-12 characters needed for WPA)\r\n");
        return FAILURE;
    }

    debug("AP SSID: %s\r\n", wifissid);
    debug("AP PASS: %s\r\n", wifipass);

    // на случай, если текущий файл испорчен - восстанавливаем его из образца
    system("cp /etc/hostapd0.conf /etc/hostapd.conf");
    usleep(1000000);

start2:
    iters++;
    if(iters > 5)
    {
        return FAILURE;
    }
    pFile = fopen("/etc/hostapd.conf","r");
    if(pFile == NULL)
    {
        WARN("Failed to open /etc/hostapd.conf to read. Creating new one...\r\n");
        system("cp /etc/hostapd0.conf /etc/hostapd.conf");
        goto start2;
    }

    if(fseek(pFile, 0, SEEK_END) != 0)
    {
        WARN("Invalid file /etc/hostapd.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/hostapd0.conf /etc/hostapd.conf");
        goto start2;
    }
    linesize = ftell(pFile);
    if(linesize < 1)
    {
        WARN("Invalid size of file /etc/hostapd.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/hostapd0.conf /etc/hostapd.conf");
        goto start2;
    }

    line = (char *) malloc(linesize + 1);
    if(line == NULL)
    {
        WARN("Cannot allocate memory for line\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_SET) != 0)
    {
        WARN("Invalid file /etc/hostapd.conf. Creating new one...\r\n");
        fclose(pFile);
        pFile = NULL;
        system("cp /etc/hostapd0.conf /etc/hostapd.conf");
        goto start2;
    }

    fread(line, 1, linesize, pFile);
    line[linesize] = '\0';
    fclose(pFile);
    pFile = NULL;

    sprintf(new_text, "ssid=%s", wifissid);

    tmpline = str_replace("ssid=", new_text, line);
    if(tmpline == NULL)
    {
        free(line);
        return FAILURE;
    }

    free(line);
    line = tmpline;

    sprintf(new_text, "wpa_passphrase=%s", wifipass);

    tmpline = str_replace("wpa_passphrase=", new_text, line);
    if(tmpline == NULL)
    {
        free(line);
        return FAILURE;
    }

    free(line);
    line = tmpline;
    
    pFile = fopen("/etc/hostapd.conf", "w");
    if(pFile == NULL)
    {
        free(line);
        WARN("Failed to open /etc/hostapd.conf to write\r\n");
        return FAILURE;
    }
    fprintf(pFile, "%s", line);
    fclose(pFile);
    pFile = NULL;
    free(line);
    return SUCCESS;
}

// найти в файле строку полностью соответствующую паттерну
int find_str(const char * pattern)
{
    FILE   *pFile       = NULL;
    char   *line        = NULL;
    long    linesize    = 0;

    pFile = fopen("/tmp/wifi.log","r");
    if(pFile == NULL)
    {
        WARN("Failed to open /tmp/wifi.log to read. \r\n");
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_END) != 0)
    {
        WARN("Invalid file /tmp/wifi.log\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }
    linesize = ftell(pFile);
    if(linesize < 12)
    {
        WARN("Invalid size of file /tmp/wifi.log\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    line = (char *) malloc(linesize + 1);
    if(line == NULL)
    {
        WARN("Cannot allocate memory for line\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_SET) != 0)
    {
        WARN("Invalid file /tmp/wifi.log\r\n");
        fclose(pFile);
        pFile = NULL;
        free(line);
        line = NULL;
        return FAILURE;
    }

    fread(line, 1, linesize, pFile);
    line[linesize] = '\0';
    fclose(pFile);
    pFile = NULL;

    if(strstr(line, pattern) == NULL)
    {
        debug("=========>CONNECTION IS NOT SET\r\n");
        free(line);
        line = NULL;
        return FAILURE;
    }
    debug("=========>CONNECTION IS SET\r\n");
    free(line);
    line = NULL;
    return SUCCESS;
}

// на основании полученного статуса сети проверяем - есть соединение или нет
int check_wifi_connection()
{
    if(is_access_point == 1)
    {
        system("/usr/local/bin/hostapd_cli status > /tmp/wifi.log");
        return find_str("num_sta[0]=1");
    }
    else
    {
        system("/usr/local/sbin/wpa_cli status > /tmp/wifi.log");
        return find_str("wpa_state=COMPLETED");
    }
}

// прочитать из файла текущие настройки сети: режим точка доступа или абонент, ssid и пароль
u8 ReadNetSettings()
{
    FILE           *pFile           = NULL;
    char           *token;
    char           *net_line;
    struct stat     netfileinfo;
    long            net_file_size   = 0;
    int             parserStep      = 0;
    int             parserStatus    = 0;    // [pwd|ssid|mode]
    int             err;
    int             i;
    int             y;
    u8              t8              = 0;
    char            tempssid[13];
    char            temppass[13];


    // update wifissid, wifipass & wifimode from file netsettings.txt
    pFile = fopen("/media/card/netsettings.txt", "r");
    if(pFile == NULL)
    {
        WARN("Cannot open file netsettings.txt\r\n");
        return FAILURE;
    }
    
    if(fseek(pFile, 0, SEEK_END) != 0)
    {
        WARN("Invalid file netsettings.txt\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    net_file_size = ftell(pFile);

    if(net_file_size < 27)
    {
        WARN("Invalid size of file netsettings.txt\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    net_line = (char *) malloc(net_file_size + 1);
    if(net_line == NULL)
    {
        WARN("Cannot allocate memory for net_line\r\n");
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    if(fseek(pFile, 0, SEEK_SET) != 0)
    {
        WARN("Invalid file netsettings.txt\r\n");
        free(net_line);
        net_line = 0;
        fclose(pFile);
        pFile = NULL;
        return FAILURE;
    }

    fread(net_line, 1, net_file_size, pFile);
    net_line[net_file_size] = '\0';

    for(y = 0; net_line[y] != '\0'; y++)
    {
        if(net_line[y] > 0x7F)
        {
            net_line[y] = ' ';
        }
    }

    token = strtok(net_line," \n\r"); // don't free net_line until strtok used

    while(token != NULL)
    {
        switch(parserStep)
        {
            case 0:
                if(strcasecmp(token, "mode:") == 0)
                {
                    parserStep = 1;
                }
                else if(strcasecmp(token, "ssid:") == 0)
                {
                    parserStep = 2;
                }
                else if(strcasecmp(token, "pwd:") == 0)
                {
                    parserStep = 3;
                }
                else
                {
                    WARN("Wrong syntax in netsettings.txt\r\n");
                    token = NULL;
                }
                break;
            case 1:
                if(strcasecmp(token, "abonent") == 0)
                {
                    parserStep      = 0;
                    t8              = 0;
                    parserStatus    = parserStatus | MODE_SET;
                }
                else if(strcasecmp(token, "ap") == 0)
                {
                    parserStep      = 0;
                    t8              = 1;
                    parserStatus    = parserStatus | MODE_SET;
                }
                else
                {
                    WARN("Wrong syntax in netsettings.txt\r\n");
                    token = NULL;
                }
                break;
            case 2:
                if(token != NULL)
                {
                    parserStep      = 0;
                    memset(tempssid, 0, 13);

                    i = strlen(token);
                    if((i > 12) || (i < 1))
                    { 
                        WARN("Wrong syntax in netsettings.txt\r\n");
                        token = NULL;
                    }
                    else
                    {
                        memcpy(tempssid, token, i);
                        parserStatus = parserStatus | SSID_SET;
                    }
                }
                else
                {
                    WARN("Wrong syntax in netsettings.txt\r\n");
                    token = NULL;
                }
                break;
            case 3:
                if(token != NULL)
                {
                    parserStep      = 0;
                    memset(temppass, 0, 13);

                    i = strlen(token);
                    if((i > 12) || (i < 8))
                    { 
                        WARN("Wrong syntax in netsettings.txt\r\n");
                        token = NULL;
                    }
                    else
                    {
                        memcpy(temppass, token, i);
                        parserStatus = parserStatus | PASS_SET;
                    }
                }
                else
                {
                    WARN("Wrong syntax in netsettings.txt\r\n");
                    token = NULL;
                }
                break;
        }

        if(parserStatus == GOT_ALL_INFO) // вся необходимая информация нашлась в файле настроек
        {
            is_access_point = t8;
            memcpy((void *)wifissid, (void *)tempssid, 13);
            memcpy((void *)wifipass, (void *)temppass, 13);
            break;
        }

        if(token != NULL)
        {
            token = strtok(NULL," \n\r");
        }
    }

    free(net_line);
    net_line = 0;
    fclose(pFile);
    pFile = NULL;

    // save modification time
    err = stat("/media/card/netsettings.txt", &netfileinfo);
    if(err != 0)
    {
        WARN("Cannot get status info from netsettings.txt \r\n");
    }
    net_file_prev_time.tv_sec = netfileinfo.st_mtim.tv_sec;

    return SUCCESS;
}

void check_nc_files_debug()
{
    struct stat st;

    #define NC1_FILE    "/tmp/nc1"
    #define NC0_FILE    "/tmp/nc0"

    if(!stat(NC0_FILE, &st))
    {
        remove(NC0_FILE);
        stop_netconnect = 1;
    }
    
    if(!stat(NC1_FILE, &st))
    {
        remove(NC1_FILE);
        stop_netconnect = 0;
    }
}

void check_wifi_sleep_condition(void)
{
    if(stop_netconnect)
    {
        if(!is_netconnect_on)
            wifi_sleep_condition = 1;
        else
            return;
    }
    else
    {
        if(is_netconnect_on)
            wifi_sleep_condition = 0;
        else
        {
            if(uptime() - last_time_connected > 5*60)
            {
                wifi_sleep_condition = 1;
            }
            else
            {
                wifi_sleep_condition = 0;
            }
        }
    }
}

void *netCommThrFxn(void *arg)
{
    //debug("NetComm thread started\r\n");

    NetCommEnv                 *envp                        = (NetCommEnv *) arg;
    Rendezvous_Attrs            rzvAttrs                    = Rendezvous_Attrs_DEFAULT;
    Rendezvous_Handle           hRendezvousInitSetServ      = NULL;
    Rendezvous_Handle           hRendezvousFinishSS         = NULL;
    Rendezvous_Handle           hRendezvousFinishAS         = NULL;
    Rendezvous_Handle           hRendezvousFinishUS         = NULL;
    Rendezvous_Handle           hRendezvousFinishWSS        = NULL;
    void                       *status                      = THREAD_SUCCESS;
    unsigned int                initMask                    = 0;
    pthread_attr_t              attr;
    struct sched_param          schedParam;
    struct timespec             cond_time;
    int                         pairSync;
    int                         connect_res                         = SUCCESS;
    int                         reboot_now                  = 0;
    int                         network_off                 = 0;
    int                         prev_state;
    int                         err;
    Command                     currentCommand;
    void                       *ret;
    time_t                      prev_check_time;
    u32                         dev_addr;

	pthread_t                   settingServerThread;
    pthread_t                   avRecServiceThread;
    pthread_t                   ucpServiceThread;
    pthread_t                   wisStreamThread;

    WISStreamEnv              	wisStreamEnv;
    SettingServerEnv            settingServerEnv;
    AVRecServiceEnv             avRecServiceEnv;
    UCPServiceEnv             	ucpServiceEnv;

    last_time_connected = uptime();
    wifi_sleep_condition = 0;

    dev_addr = getDeviceAddr() - 86210000;

    // ssid и пароль по умолчанию на основе адреса устройства
    sprintf((char *)wifissid, "PA%04lu", dev_addr);
    sprintf((char *)wifipass, "passphrase");
    is_access_point = 1;

    // если в файле другие настройки - меняем
    ReadNetSettings();
    set_access_point();    
    set_wifi_net();// modify wpa_supplicant.conf

    if(pthread_mutex_init(&socket_mutex, NULL) != 0)
    {
        ERR("\r\n socket_mutex init failed\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }
    if(pthread_cond_init(&socket_cond, NULL) != 0)
    {
        ERR("\r\n socket_cond init failed\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }
    /* Initialize the thread attributes */
    if (pthread_attr_init(&attr)) 
    {
        ERR("Failed to initialize thread attrs\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    // остановить wifi, что то одно может быть запущено
    system("/usr/bin/killall hostapd > /dev/null");
    system("/usr/bin/killall wpa_supplicant > /dev/null");
    //usleep(1000000);

    //присваиваем адрес чтобы приложения нормально открыли себе сокеты
    system("/sbin/ifconfig wlan0 192.168.0.1 up");
    usleep(1000000);
    system("/sbin/ifconfig wlan0 down");

    remove("/var/run/hostapd/wlan0");

    network_off = stop_netconnect;

    if(!network_off)
    {
        if(is_access_point == 1)
        {
            // поднимаем точку доступа
            //system("/sbin/ifconfig wlan0 192.168.0.1 netmask 255.255.255.0 up");
            //usleep(2000000);
            system("/usr/local/bin/hostapd -B /etc/hostapd.conf");
            usleep(2000000);
            system("/sbin/ifconfig wlan0 192.168.0.1");
            usleep(500000);
            system("/usr/sbin/udhcpd -S /etc/udhcpd.conf");
            usleep(1000000);
        }
        else
        {
            // поднимаем режим абонента
            system("/usr/local/sbin/wpa_supplicant -Dnl80211 -c/etc/wpa_supplicant.conf -iwlan0 -qq -B > /dev/null");
            usleep(2000000);

            // ждем присоединения к внешней точке доступа
            while(1)
            {
                check_wifi_sleep_condition();

                connect_res = check_wifi_connection();
                if(connect_res == SUCCESS)
                {
                    break;
                }
                usleep(2000000);
                currentCommand = gblGetCmd();
                if((currentCommand == FINISH) || (currentCommand == SLEEP))
                {
                    debug("Net connection get sleep/finish command(%i) ...\r\n", __LINE__);
                    goto cleanup;
                }
            }

            // только после присоединения получаем ip 
            // system("/sbin/udhcpc -i wlan0 -A 5 -t 5");
            system("/sbin/udhcpc -q -b -i wlan0");
            usleep(3000000);
        }

        // check if ip address set
        while(EthIfaceCheckIP() < 0)
        {
            currentCommand = gblGetCmd();
            if((currentCommand == FINISH) || (currentCommand == SLEEP))
            {
                debug("Net connection get sleep/finish command(%i) ...\r\n", __LINE__);
                goto cleanup;
            }
            usleep(1000000);
        }

        // для правильной работы библиотеки live для видеотрансляции
        system("/sbin/route add -host 228.67.43.91 dev wlan0");
        last_time_connected = uptime();
    }

    // сеть есть - можем запустить файловый сервер
    if(set_netbios_name() != 0)
    {
        WARN("Netbios name is not set!\r\n");
    }

    debug("Starting smbd...\r\n");
    system("/sbin/smbd -D");
    usleep(100000);
    debug("Starting nmbd...\r\n");
    system("/sbin/nmbd -D");    

    Dmai_clear(wisStreamEnv);
    Dmai_clear(settingServerEnv);
    Dmai_clear(ucpServiceEnv);
    Dmai_clear(avRecServiceEnv);

    pairSync            	= 2;

    hRendezvousInitSetServ  = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishSS     = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishAS     = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishUS     = Rendezvous_create(pairSync, &rzvAttrs);
    hRendezvousFinishWSS    = Rendezvous_create(pairSync, &rzvAttrs);
    if((hRendezvousInitSetServ == NULL) || (hRendezvousFinishSS == NULL) ||
        (hRendezvousFinishAS   == NULL) || (hRendezvousFinishUS == NULL) ||
        (hRendezvousFinishWSS  == NULL))
    {
    	ERR("Failed to create hRendezvousInitSetServ object\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    /* Force the thread to use custom scheduling attributes */
    if (pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED)) 
    {
        ERR("Failed to set schedule inheritance attribute\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    /* Set the thread to be fifo real time scheduled */
    if (pthread_attr_setschedpolicy(&attr, SCHED_FIFO)) 
    {
        ERR("Failed to set FIFO scheduling policy\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        cleanup(THREAD_FAILURE, STRM_QID);
    }

    // установка настроек видеозаписи через Андроид-устройство
    settingServerEnv.hRendezvousInit    = hRendezvousInitSetServ;
    settingServerEnv.hRendezvousFinish  = hRendezvousFinishSS;

    /* Set the setting server thread priority */
    schedParam.sched_priority = SETTINGSERVER_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(&attr, &schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    // Create the thread for setting server (координатор разных групп настроек)
    if (pthread_create(&settingServerThread, &attr, settingServerThrFxn, &settingServerEnv)) 
    {
        ERR("Failed to create setting server thread\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    initMask |= SETTINGSERVERTHREADCREATED;

    // Wait settingServerThread initialization
    if(hRendezvousInitSetServ != NULL)
    {
    	Rendezvous_meet(hRendezvousInitSetServ);
    }

    avRecServiceEnv.app_descr           = settingServerEnv.app_descr;
    avRecServiceEnv.hRendezvousFinish   = hRendezvousFinishAS;

    /* Set the AV record service thread priority */
    schedParam.sched_priority = AVRECSERVICE_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(&attr, &schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    // Create the thread for AV record service (настройки аудио/видео записи)
    if (pthread_create(&avRecServiceThread, &attr, avRecServiceThrFxn, &avRecServiceEnv)) 
    {
        ERR("Failed to create AV record service thread\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    initMask |= AVRECSERVICETHREADCREATED;

    ioctl(fd_wdt, WDIOC_KEEPALIVE, NULL);

    // запустить стриминг с камеры через wifi(устройство начинает ожидать запроса на стриминг)
#ifdef SOUND_EN
    wis_audio_enable = 1;
#endif
    wis_video_enable = (sound_only == 0) ? 1 : 0;

    /* Set the videostreaming thread priority */
    schedParam.sched_priority = WISSTREAM_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(&attr, &schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    wisStreamEnv.hRendezvousFinish   = hRendezvousFinishWSS;

    // Create the thread for video streaming through wifi
    if (pthread_create(&wisStreamThread, &attr, WISStreamThrFxn, &wisStreamEnv)) 
    {
        ERR("Failed to create WIS streamer thread\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    initMask |= WISSTREAMTHREADCREATED;


    // ответ на поиск устройства по UDP протоколу в сети ethernet
    ucpServiceEnv.app_descr         = settingServerEnv.own_descr;
    ucpServiceEnv.hRendezvousFinish = hRendezvousFinishUS;

    /* Set the UCP service thread priority */
    schedParam.sched_priority = UCPSERVICE_THREAD_PRIORITY;
    if (pthread_attr_setschedparam(&attr, &schedParam)) 
    {
        ERR("Failed to set scheduler parameters\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    // Create the thread for UCP service
    if (pthread_create(&ucpServiceThread, &attr, ucpServiceThrFxn, &ucpServiceEnv)) 
    {
        ERR("Failed to create UCP service thread\r\n");
        logEvent(log_REC_APL_INIT_FAILED);
        internal_error = 1;
    }

    initMask |= UCPSERVICETHREADCREATED;

    is_netconnect_on    = !network_off;
    prev_check_time     = time(NULL);

    while(1)
    {
        currentCommand = gblGetCmd();

        check_nc_files_debug();//

        check_wifi_sleep_condition();

        if((currentCommand == FINISH) || (currentCommand == SLEEP))
        {
            //debug("Net connection thread finishing ...\r\n");
            goto cleanup;
        }

        if(!network_off) // если сеть не выключена проверяем её статус
        {
            if(time(NULL) - prev_check_time >= 30) // проверяем каждые 30 сек.
            {
                prev_check_time   = time(NULL);
                prev_state  = connect_res;
                connect_res = check_wifi_connection();
                if(connect_res == FAILURE)
                {
                    is_netconnect_on = 0;
                }
                else
                {
                    is_netconnect_on = 1;
                    last_time_connected = uptime();
                    if((prev_state == FAILURE) && (is_access_point == 0)
                        && (wis_video_hw_started == 0) && (wis_audio_hw_started == 0))
                    {
                        reboot_now = 1;
                    }
                }
            }
        }

        if(stop_netconnect == 1)    // если по радиоканалу пришел сигнал ВЫКЛючения сети
        {
            if(!network_off)
            {
                if(is_access_point == 1)
                {
                    system("/usr/bin/killall hostapd > /dev/null");
                    system("/usr/bin/killall udhcpd > /dev/null");
                }
                else
                {
                    // остановить wifi
                    system("/usr/bin/killall wpa_supplicant > /dev/null");
                    system("/usr/bin/killall udhcpc > /dev/null");
                }
                usleep(2000000);

                network_off         = 1;
                is_netconnect_on    = 0;
            }
        }
        else if(stop_netconnect == 0) // если по радиоканалу пришел сигнал ВКЛючения сети
        {
            if(network_off)
            {
                wifi_sleep_condition = 0;
                if(is_access_point == 1)
                {
                    //ReadNetSettings();
                    //set_access_point();

                    //system("/sbin/ifconfig wlan0 192.168.0.1 netmask 255.255.255.0 up");
                    //usleep(2000000);
                    system("/usr/local/bin/hostapd -B /etc/hostapd.conf");
                    usleep(2000000);
                    system("/sbin/ifconfig wlan0 192.168.0.1");
                    usleep(500000);
                    system("/usr/sbin/udhcpd -S /etc/udhcpd.conf");
                    usleep(1000000);
                }
                else
                {
                    // при запуске режима абонент каждый раз происходит задержка кадров видео,
                    // которую решить не удалось, нужно реализовывать балансировку подачи кадров:
                    // иногда пропускать кадры, иногда добавлять дополнительные
                    //usleep(2000000);
                    system("/usr/local/sbin/wpa_supplicant -Dnl80211 -c/etc/wpa_supplicant.conf -iwlan0 -qq -B > /dev/null");
                    usleep(2000000);

                    while(1)
                    {
                        connect_res = check_wifi_connection();
                        if(connect_res == SUCCESS)
                        {
                            break;
                        }
                        usleep(1000000);
                        currentCommand = gblGetCmd();
                        if((currentCommand == FINISH) || (currentCommand == SLEEP))
                        {
                            //debug("Net connection thread finishing ...\r\n");
                            goto cleanup;
                        }
                    }
                    usleep(2000000);
                    system("/sbin/udhcpc -q -b -i wlan0");
                    usleep(3000000);
                }

                // для правильной работы библиотеки live для видеотрансляции
                system("/sbin/route add -host 228.67.43.91 dev wlan0");

                network_off         = 0;
                last_time_connected = uptime();
            }
        }

        // если поменялись параметры аудио/видео
        if((reboot_now || strm_error || 
            ((video_bitrate != video_bitrate_prev) || (audio_channels != audio_channels_prev))))
        {

            reboot_now  = 0;
            strm_error  = 0;

        	if (initMask & WISSTREAMTHREADCREATED)
		    {
		    	wis_stop_streaming();

		    	if(hRendezvousFinishWSS != NULL)
                {
                    Rendezvous_meet(hRendezvousFinishWSS);
                }

		    	if (pthread_join(wisStreamThread, &ret) == 0)
		    	{
			        if (ret == THREAD_FAILURE)
			        {
			            status = THREAD_FAILURE;
			            WARN("Failed to stop wisStreamThread\r\n");
			        }
			        else
			        {
                        initMask = initMask & ~WISSTREAMTHREADCREATED;

                        if(hRendezvousFinishWSS) 
                        {
                            Rendezvous_delete(hRendezvousFinishWSS);
                            hRendezvousFinishWSS = NULL;
                        }

			        	// запустить стриминг с камеры через wifi(устройство начинает ожидать запроса на стриминг)
					    ///* Set the video streaming thread priority */
					    schedParam.sched_priority = WISSTREAM_THREAD_PRIORITY;
					    if (pthread_attr_setschedparam(&attr, &schedParam)) 
					    {
					        ERR("Failed to set scheduler parameters\r\n");
					        logEvent(log_REC_APL_INIT_FAILED);
                            internal_error = 1;
					    }

                        hRendezvousFinishWSS                = Rendezvous_create(pairSync, &rzvAttrs);
                        wisStreamEnv.hRendezvousFinish   = hRendezvousFinishWSS;

					    // Create the thread for video streaming
					   	if (pthread_create(&wisStreamThread, &attr, WISStreamThrFxn, &wisStreamEnv)) 
					    {
					        ERR("Failed to create WIS streamer thread\n");
					        logEvent(log_REC_APL_INIT_FAILED);
                            internal_error = 1;
					    }

					    initMask |= WISSTREAMTHREADCREATED;
			        }
			    }
			}
        }
        usleep(500000);
    }

cleanup:
    debug("Net connection thread finishing(%i)...\r\n", __LINE__);

	is_netconnect_on = 0;

	while(curEthIf.apps[1].if_handle == 1)
	{
	    makewaittime(&cond_time, 0, 500000000); // 500 ms
	    pthread_mutex_lock(&socket_mutex);
        err = pthread_cond_timedwait(&socket_cond, &socket_mutex, &cond_time);
	    if(err != 0) 
	    {
            if(err == ETIMEDOUT)
            {
                ERR("Sleeping failed!\r\n");
            }
            else if(err != ETIMEDOUT)
            {
                ERR("Exit pthread_cond_timedwait with code %i\r\n", err);
            }
            logEvent(log_REC_APL_INTERNAL_ERROR_OCCURED);
	        pthread_mutex_unlock(&socket_mutex);
	        internal_error = 1;
	    }
	    pthread_mutex_unlock(&socket_mutex);
        usleep(50000);
	}
    
    THREAD_JOIN_EX(WISSTREAMTHREADCREATED, hRendezvousFinishWSS, wisStreamThread, wis_stop_streaming);
    THREAD_JOIN(UCPSERVICETHREADCREATED, hRendezvousFinishUS, ucpServiceThread);
    THREAD_JOIN(SETTINGSERVERTHREADCREATED, hRendezvousFinishSS, settingServerThread);
    THREAD_JOIN(AVRECSERVICETHREADCREATED, hRendezvousFinishAS, avRecServiceThread);
    
    RANDEZVOUS_DEL(hRendezvousFinishSS);
    RANDEZVOUS_DEL(hRendezvousFinishAS);
    RANDEZVOUS_DEL(hRendezvousFinishWSS);    
    RANDEZVOUS_DEL(hRendezvousFinishUS);
    RANDEZVOUS_DEL(hRendezvousInitSetServ);
    
    pthread_mutex_destroy(&socket_mutex);
	pthread_cond_destroy(&socket_cond);    
    pthread_attr_destroy(&attr);
    
    // остановить wifi
    if(is_access_point == 1)
    {
        system("/usr/bin/killall udhcpd > /dev/null");
        system("/usr/bin/killall hostapd > /dev/null");        
    }
    else
    {        
        system("/usr/bin/killall udhcpc > /dev/null");
        system("/usr/bin/killall wpa_supplicant > /dev/null");        
    }
    
    system("/usr/bin/killall smbd > /dev/null");
    system("/usr/bin/killall nmbd > /dev/null");
    
    if(envp->hRendezvousFinishNC)
        Rendezvous_meet(envp->hRendezvousFinishNC);

    debug("NetComm thread finished\r\n");
    return status;
}