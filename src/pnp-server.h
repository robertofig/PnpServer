#ifndef PNP_SERVER_H
#define PNP_SERVER_H

#include "tinybase-platform.h"
#include "tinyserver.h"
#include "tinyserver-http.h"

#define APP_ARENA_SIZE Megabyte(1) // Per-app fized memory size.
#define SERVER_MAX_BODY_SIZE Megabyte(100) // If payload is larger than this, drop conn.

typedef void (*app_init)(void*);
typedef void (*module_main)(void*, void*);

struct app_info
{
    char Name[255];
    u8 NameSize;
    module_main Entry;
    buffer Arena;
};

struct pnp_server
{
    char* HtmlEncoding;
    
    char FilesPath[MAX_PATH_SIZE];
    u16 BasePathSize;
    u16 FilesPathSize;
    
    u32 NumApps;
    app_info Apps[32];
};

struct io_thread_params
{
    ts_io_queue IoQueue;
    mpsc_queue* SocketQueue;
    file Listening;
};

enum io_stage
{
    IoStage_Accepting,
    IoStage_Reading,
    IoStage_ReadingBody,
    IoStage_App,
    IoStage_Payload,
    IoStage_Sending,
    IoStage_Closing,
    IoStage_Terminating,
};

struct io_info
{
    ts_io_queue* IoQueue;
    io_stage Stage;
    
    u32 IoBufferSize;
    char* IoBuffer;
    
    ts_request Request;
    ts_body Body;
    ts_response Response;
    
    app_info* AppInfo;
};

internal bool RecvFullRequestBody(ts_io* Conn, ts_body* Body, usz MaxBodySize);

#endif //PNP_SERVER_H
