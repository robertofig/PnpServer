#ifndef PNP_SERVER_H
#define PNP_SERVER_H

#include "tinybase-queues.h"
#include "tinybase-platform.h"
#include "tinyserver.h"
#include "tinyserver-http.h"


#define APP_ARENA_SIZE Megabyte(1) // Per-app fized memory size.
#define SERVER_MAX_BODY_SIZE Megabyte(100) // If payload is larger than this, drop conn.

typedef void (*app_init)(void*);
typedef void (*module_main)(void*, void*);

struct parsed_args
{
    void* RootPath;
    void* FilesFolder;
    u16 Port;
};

struct app_info
{
    char Name[255];
    u8 NameSize;
    module_main Entry;
    buffer Arena;
};

struct pnp_server
{
    char* ServerName;
    
    char FilesPath[MAX_PATH_SIZE];
    u32 BasePathSize;
    u32 FilesPathSize;
    
    u32 NumApps;
    app_info Apps[128];
    mutex LoadAppMutex;
};

enum io_stage
{
    IoStage_None,
    IoStage_Accepting,
    IoStage_Reading,
    IoStage_ReadingBody,
    IoStage_App,
    IoStage_SendingHeader,
    IoStage_SendingCookie,
    IoStage_SendingPayload,
    IoStage_Closing,
    IoStage_Terminating,
};

struct io_info
{
    io_stage IoStage;
    buffer IoBuffer;
    
    ts_request Request;
    ts_body Body;
    ts_response Response;
    
    app_info* AppInfo;
    semaphore Semaphore;
};

global pnp_server gServerInfo;


#endif //PNP_SERVER_H
