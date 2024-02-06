#include "pnp-server.h"
#include "pnp-server-app.cpp"


internal app_info*
LoadApp(string AppName)
{
    app_info* Result = NULL;
    
    LockOnMutex(&gServerInfo.LoadAppMutex);
    
    if (gServerInfo.NumApps < ArrayCount(gServerInfo.Apps))
    {
        // Code below makes sure the AppName is in the correct system encoding, to
        // perform the comparisons to the folder names.
        
        char AppNameTranscodedBuf[MAX_PATH_SIZE] = {0};
        path AppNameTranscoded = Path(AppNameTranscodedBuf);
        Transcode(AppName, &AppNameTranscoded);
        
        iter_dir MainIter = {0};
        path BasePath = Path(gServerInfo.FilesPath);
        BasePath.WriteCur = gServerInfo.BasePathSize;
        InitIterDir(&MainIter, BasePath);
        
        char LibExtTranscodedPtr[32] = {0}; 
        path LibExtTranscoded = Path(LibExtTranscodedPtr);
        string LibExt = StringLit(DYNAMIC_LIB_EXT);
        Transcode(LibExt, &LibExtTranscoded);
        
        while (ListFiles(&MainIter))
        {
            if (!MainIter.IsDir) continue;
            
            path FolderName = PathCString(MainIter.Filename);
            if (!EqualStrings(FolderName, AppNameTranscoded)) continue;
            
            iter_dir InnerIter = {0};
            InitIterDir(&InnerIter, FolderName);
            while (ListFiles(&InnerIter))
            {
                if (InnerIter.IsDir) continue;
                
                path Filename = PathCString(InnerIter.Filename);
                u8* ExtPtr = (u8*)CharInString('.', Filename,
                                               RETURN_PTR_FIND|SEARCH_REVERSE);
                if (ExtPtr == NULL
                    || memcmp(ExtPtr, LibExtTranscoded.Base,
                              LibExtTranscoded.WriteCur) != 0)
                {
                    continue;
                }
                
                char FullAppPathBuf[MAX_PATH_SIZE] = {0};
                path FullAppPath = Path(FullAppPathBuf);
                AppendPathToPath(BasePath, &FullAppPath);
                AppendPathToPath(FolderName, &FullAppPath);
                AppendPathToPath(Filename, &FullAppPath);
                
                file App = LoadExternalLibrary(FullAppPath.Base);
                if (App)
                {
                    void* AppEntry = LoadExternalSymbol(App, "ModuleMain");
                    if (AppEntry)
                    {
                        app_info* AppInfo = &gServerInfo.Apps[gServerInfo.NumApps++];
                        AppInfo->Entry = (module_main)AppEntry;
                        CopyData(AppInfo->Name, sizeof(AppInfo->Name),
                                 AppName.Base, AppName.WriteCur);
                        AppInfo->NameSize = AppName.WriteCur;
                        AppInfo->Arena = GetMemory(APP_ARENA_SIZE, 0, MEM_WRITE);
                        
                        app_init AppInit = (app_init)LoadExternalSymbol(App, "AppInit");
                        if (AppInit)
                        {
                            AppInit(&AppInfo->Arena);
                        }
                        
                        Result = AppInfo;
                        break;
                    }
                }
            }
        }
    }
    
    UnlockMutex(&gServerInfo.LoadAppMutex);
    
    return Result;
}

internal string
GetResourceExt(string Path)
{
    string Result = {0};
    
    usz PointIdx = CharInString('.', Path, RETURN_IDX_AFTER | SEARCH_REVERSE);
    usz SlashIdx = CharInString('/', Path, RETURN_IDX_AFTER | SEARCH_REVERSE);
    if (PointIdx != INVALID_IDX && PointIdx > SlashIdx)
    {
        Result = String(Path.Base + PointIdx, Path.WriteCur - PointIdx, 0, EC_ASCII);
    }
    
    return Result;
}

internal app_info*
GetResourceAppInfo(string Resource)
{
    AdvanceBuffer(&Resource.Buffer, 1); // Jump first "/".
    usz SlashIdx = CharInString('/', Resource, RETURN_IDX_FIND);
    if (SlashIdx != INVALID_IDX)
    {
        Resource.WriteCur = SlashIdx;
    }
    
    for (i32 AppIdx = 0; AppIdx < gServerInfo.NumApps; AppIdx++)
    {
        app_info* AppInfo = &gServerInfo.Apps[AppIdx];
        string AppName = String(AppInfo->Name, AppInfo->NameSize, 0, EC_ASCII);
        if (EqualStrings(AppName, Resource))
        {
            return AppInfo;
        }
    }
    
    app_info* Result = LoadApp(Resource);
    return Result;
}

internal char*
ExtToMIME(string Ext)
{
    // TODO: change this to a hashtable lookup.
    
    if (EqualStrings(Ext, StringLit("htm"))) return "text/html; charset=utf-8";
    if (EqualStrings(Ext, StringLit("html"))) return "text/html; charset=utf-8";
    if (EqualStrings(Ext, StringLit("css"))) return "text/css";
    if (EqualStrings(Ext, StringLit("js"))) return "text/javascript";
    if (EqualStrings(Ext, StringLit("txt"))) return "text/plain";
    
    if (EqualStrings(Ext, StringLit("jpg"))) return "image/jpeg";
    if (EqualStrings(Ext, StringLit("jpeg"))) return "image/jpeg";
    if (EqualStrings(Ext, StringLit("png"))) return "image/png";
    if (EqualStrings(Ext, StringLit("gif"))) return "image/gif";
    if (EqualStrings(Ext, StringLit("bmp"))) return "image/bmp";
    
    if (EqualStrings(Ext, StringLit("otf"))) return "font/otf";
    if (EqualStrings(Ext, StringLit("ttf"))) return "font/ttf";
    
    if (EqualStrings(Ext, StringLit("json"))) return "application/json";
    if (EqualStrings(Ext, StringLit("xml"))) return "application/xml";
    
    return "application/octet-stream";
}

internal bool
RecvFullRequestBody(ts_io* Conn, ts_body* Body, usz MaxBodySize)
{
    io_info* Info = (io_info*)&Conn[1];
    Info->IoStage = IoStage_ReadingBody;
    
    u64 AmountReceived = Info->IoBuffer.WriteCur - Info->Request.HeaderSize;
    u64 AmountToRecv = Body->Size - AmountReceived;
    u64 AmountLeftOnIoBuffer = Info->IoBuffer.Size - Info->IoBuffer.WriteCur;
    
    // If size is too big, read in place until it has read everything. This is so we
    // can return a response with status code 413. Many clients (like browsers) only
    // start reading after sending their entire payload, so this makes sure it's read,
    // but not processed. Content of read is discarded.
    
    if (Body->Size > MaxBodySize)
    {
        Info->Response.StatusCode = 413;
        
        usz ThrowawayMemSize = Megabyte(1);
        buffer ThrowawayMem = GetMemory(ThrowawayMemSize, 0, MEM_WRITE);
        if (ThrowawayMem.Base)
        {
            while (AmountToRecv > 0)
            {
                u32 BytesToRecv = Min(AmountToRecv, ThrowawayMemSize);
                Conn->IoBuffer = ThrowawayMem.Base;
                Conn->IoSize = BytesToRecv;
                RecvData(Conn);
                
                WaitOnSemaphore(&Info->Semaphore);
                
                AmountToRecv -= Conn->BytesTransferred;
            }
            FreeMemory(&ThrowawayMem);
        }
        else
        {
            Info->IoStage = IoStage_Terminating;
            return false;
        }
    }
    
    // Checks if size left in Header buffer is enough to fit entire payload.
    // Allocs new if not.
    
    if (AmountToRecv > AmountLeftOnIoBuffer)
    {
        buffer NewBuffer = GetMemory(Body->Size, 0, MEM_READ|MEM_WRITE);
        if (!NewBuffer.Base)
        {
            Info->Response.StatusCode = 503;
            return false;
        }
        CopyData(NewBuffer.Base, NewBuffer.Size, Body->Base, AmountReceived);
        Body->Base = NewBuffer.Base;
    }
    
    // Receives entire payload.
    while (AmountToRecv > 0)
    {
        u32 BytesToRecv = Min(AmountToRecv, U32_MAX);
        Conn->IoBuffer = Body->Base + AmountReceived;
        Conn->IoSize = BytesToRecv;
        RecvData(Conn);
        
        WaitOnSemaphore(&Info->Semaphore);
        
        AmountReceived += Conn->BytesTransferred;
        AmountToRecv -= Conn->BytesTransferred;
    }
    
    return true;
}

THREAD_PROC(AppThread)
{
    ts_io* Conn = (ts_io*)Arg;
    io_info* Info = (io_info*)&Conn[1];
    
    http Http = {0};
    Http.Object = Conn;
    Http.Arena = (app_arena*)&Info->AppInfo->Arena;
    Http.Verb = (http_verb)Info->Request.Verb;
    Http.Path = Info->Request.Base + Info->Request.UriOffset;
    Http.PathSize = Info->Request.PathSize;
    Http.Query = Http.Path + Http.PathSize + 1;
    Http.QuerySize = Info->Request.QuerySize;
    Http.HeaderCount = Info->Request.NumHeaders;
    Http.Body = Info->Body.Base;
    Http.BodySize = Info->Body.Size;
    Http.ContentType = Info->Body.ContentType;
    Http.ContentTypeSize = Info->Body.ContentTypeSize;
    
    Http.GetHeaderByKey = AppGetHeaderByKey;
    Http.GetHeaderByIdx = AppGetHeaderByIdx;
    Http.RecvFullRequestBody = AppRecvFullRequestBody;
    Http.ParseFormData = AppParseFormData;
    Http.GetFormFieldByName = AppGetFormFieldByName;
    Http.GetFormFieldByIdx = AppGetFormFieldByIdx;
    Http.AllocPayload = AppAllocPayload;
    Http.AllocCookies = AppAllocCookies;
    
    Info->AppInfo->Entry(&Http, &Info->AppInfo->Arena);
    
    Info->Response.StatusCode = Http.ReturnCode;
    Info->Response.MimeType = Http.PayloadType;
    Info->Response.PayloadSize = Http.PayloadSize;
    Info->Response.CookiesSize = Http.CookiesSize;
    
    Info->IoStage = IoStage_App; // Has to be reset here because app can change it.
    Conn->BytesTransferred = 0;
    SendToIoQueue(Conn);
    
    return 0;
}

internal void
EnterApp(ts_io* Conn, io_info* Info)
{
    // TODO: Take thread from threadpool, only create new one if pool is empty.
    
    thread NewThread = InitThread(AppThread, Conn, false);
    CloseThread(&NewThread);
}

internal void
PrepareResponse(ts_io* Conn, io_info* Info)
{
    if (Info->Response.StatusCode == 200)
    {
        string Connection = GetHeaderByKey(&Info->Request, "Connection");
        Info->Response.KeepAlive = (EqualStrings(Connection, StringLit("keep-alive"))
                                    || EqualStrings(Connection, StringLit("Keep-Alive")));
        Info->Response.Version = Info->Request.Version;
    }
    
    string OutBuffer = String(Info->IoBuffer.Base, 0, Info->IoBuffer.Size, EC_ASCII);
    CraftHttpResponseHeader(&Info->Response, &OutBuffer, gServerInfo.ServerName);
    
    Conn->BytesTransferred = 0;
    Conn->IoBuffer = (u8*)OutBuffer.Base;
    Conn->IoSize = OutBuffer.WriteCur;
}

internal void
ProcessReading(ts_io* Conn, io_info* Info)
{
    string InBuffer = {0};
    InBuffer.Buffer = Info->IoBuffer;
    InBuffer.Enc = EC_ASCII;
    ts_http_parse Result = ParseHttpHeader(InBuffer, &Info->Request);
    
    if (Result == HttpParse_OK)
    {
        string Resource = String(Info->Request.Base + Info->Request.UriOffset,
                                 Info->Request.PathSize, 0, EC_ASCII);
        
        switch (Info->Request.Verb)
        {
            case HttpVerb_Post:
            case HttpVerb_Put:
            {
                Info->Body = GetBodyInfo(&Info->Request);
                if (Info->Body.Base)
                {
                    // This may not be the entire body. Further reads to receive
                    // the rest of the body may be performed in the app, if
                    // necessary.
                    
                    if (Info->Body.Size > SERVER_MAX_BODY_SIZE)
                    {
                        // Body too large, break connection.
                        Info->IoStage = IoStage_Terminating;
                        break;
                    }
                    else { /* Falls back to HttpVerb_Delete below. */ }
                }
                else
                {
                    Info->Response.StatusCode = 400;
                    Info->IoStage = IoStage_SendingHeader;
                    break;
                }
            }
            
            case HttpVerb_Delete:
            {
                app_info* AppInfo = GetResourceAppInfo(Resource);
                if (AppInfo)
                {
                    Info->AppInfo = AppInfo;
                    Info->IoStage = IoStage_App;
                }
                else
                {
                    Info->Response.StatusCode = 404;
                    Info->IoStage = IoStage_SendingHeader;
                }
            } break;
            
            case HttpVerb_Get:
            {
                string Ext;
                app_info* AppInfo = GetResourceAppInfo(Resource);
                if (AppInfo)
                {
                    Info->AppInfo = AppInfo;
                    Info->IoStage = IoStage_App;
                }
                else if ((Ext = GetResourceExt(Resource)).Base)
                {
                    char ResourcePathBuf[MAX_PATH_SIZE] = {0};
                    path ResourcePath = Path(ResourcePathBuf);
                    AdvanceString(&Resource, 1);
                    AppendDataToPath(gServerInfo.FilesPath, gServerInfo.FilesPathSize,
                                     &ResourcePath);
                    AppendStringToPath(Resource, &ResourcePath);
                    
                    file File = OpenFileHandle(ResourcePathBuf, READ_SHARE);
                    if (File != INVALID_FILE)
                    {
                        Info->Response.Payload = (char*)File;
                        Info->Response.PayloadSize = FileSizeOf(File);
                        Info->Response.MimeType = ExtToMIME(Ext);
                        Info->Response.PayloadIsFile = true;
                        
                        Info->Response.StatusCode = 200;
                        Info->IoStage = IoStage_SendingHeader;
                    }
                    else // File not found.
                    {
                        Info->Response.StatusCode = 404;
                        Info->IoStage = IoStage_SendingHeader;
                    }
                }
                else // Not a valid app, nor a file.
                {
                    Info->Response.StatusCode = 404;
                    Info->IoStage = IoStage_SendingHeader;
                }
            } break;
            
            default: // Implement new Verbs here.
            {
                Info->Response.StatusCode = 501;
                Info->IoStage = IoStage_SendingHeader;
            }
        }
    }
    
    else if (Result == HttpParse_HeaderIncomplete)
    {
        Info->IoStage = IoStage_Reading;
    }
    
    else if (Result == HttpParse_HeaderInvalid
             || Result == HttpParse_HeaderMalicious)
    {
        Info->Response.StatusCode = 400;
        Info->IoStage = IoStage_SendingHeader;
    }
    
    else if (Result == HttpParse_TooManyHeaders)
    {
        Info->IoStage = IoStage_Terminating;
    }
}

internal void
SendPayload(ts_io* Conn, io_info* Info, usz Offset)
{
    if (Info->Response.PayloadIsFile)
    {
        Conn->IoSize -= Offset;
        SendFile(Conn);
    }
    else
    {
        Conn->IoBuffer += Offset;
        Conn->IoSize -= Offset;
        SendData(Conn);
    }
}

internal void
CleanupInfo(io_info* Info)
{
    // This frees all memory and blanks everything, except for [.IoBuffer] and
    // [Semaphore], which can be reused in a new connection.
    
    ClearBuffer(&Info->IoBuffer);
    if ((Info->Body.Base < Info->IoBuffer.Base)
        || (Info->Body.Base > (Info->IoBuffer.Base+Info->IoBuffer.Size)))
    {
        buffer Body = Buffer(Info->Body.Base, 0, Info->Body.Size);
        FreeMemory(&Body);
    }
    if (Info->Response.Cookies)
    {
        buffer Cookies = Buffer(Info->Response.Cookies, 0, Info->Response.CookiesSize);
        FreeMemory(&Cookies);
    }
    if (Info->Response.Payload)
    {
        if (Info->Response.PayloadIsFile)
        {
            CloseFileHandle((file)Info->Response.Payload);
        }
        else
        {
            buffer Payload = Buffer(Info->Response.Payload, 0, Info->Response.PayloadSize);
            FreeMemory(&Payload);
        }
    }
    memset(&Info->Request, 0, sizeof(ts_request));
    memset(&Info->Body, 0, sizeof(ts_body));
    memset(&Info->Response, 0, sizeof(ts_response));
    Info->IoStage = IoStage_None;
}

internal void
CleanupConn(ts_io* Conn)
{
    file Socket = Conn->Socket;
    memset(Conn, 0, sizeof(ts_io));
    Conn->Socket = Socket;
}

internal void
WrapUpTransaction(ts_io* Conn, io_info* Info)
{
    if (Info->Response.KeepAlive)
    {
        CleanupInfo(Info);
        Info->IoStage = IoStage_Reading;
        Conn->BytesTransferred = 0;
        Conn->IoBuffer = Info->IoBuffer.Base;
        Conn->IoSize = Info->IoBuffer.Size;
        memset(Conn->InternalData, 0, TS_INTERNAL_DATA_SIZE);
        RecvData(Conn);
    }
    else
    {
        Info->IoStage = IoStage_Terminating;
        DisconnectSocket(Conn);
    }
}

THREAD_PROC(IoThread)
{
    mpsc_freelist* SocketList = (mpsc_freelist*)Arg;
    
    for (;;)
    {
        // This will block until something is successfully dequeued.
        ts_io* Conn = WaitOnIoQueue();
        if (Conn->Status == Status_Error)
        {
            // TODO: how to treat this?
        }
        io_info* Info = (io_info*)&Conn[1];
        
        //===========================
        // Check for IO failures.
        //===========================
        
        if (Conn->Status == Status_Aborted)
        {
            Info->IoStage = IoStage_Terminating;
            DisconnectSocket(Conn);
        }
        
        else if (Conn->Status == Status_Error)
        {
            TerminateConn(Conn);
            CleanupInfo(Info);
            CleanupConn(Conn);
            MPSCFreeListPush(SocketList, Conn);
        }
        
        //========================================
        // Process the conn based on the IoStage.
        //========================================
        
        else if (Info->IoStage == IoStage_Accepting
                 || Info->IoStage == IoStage_Reading)
        {
            Info->IoBuffer.WriteCur += Conn->BytesTransferred;
            ProcessReading(Conn, Info);
            
            // From here it can go to IoStage_Reading (if recv incomplete or preparing
            // payload), IoStage_App (if it's an app call), IoStage_Sending (if header
            // was refused or error), of IoStage_Terminating (if conn needs to be
            // aborted).
            
            if (Info->IoStage == IoStage_Reading)
            {
                Conn->IoBuffer = Info->IoBuffer.Base + Info->IoBuffer.WriteCur;
                Conn->IoSize = Info->IoBuffer.Size - Info->IoBuffer.WriteCur;
                RecvData(Conn);
            }
            else if (Info->IoStage == IoStage_App)
            {
                EnterApp(Conn, Info);
            }
            else if (Info->IoStage == IoStage_SendingHeader)
            {
                PrepareResponse(Conn, Info);
                SendData(Conn);
            }
            else // IoStage_Terminating
            {
                DisconnectSocket(Conn);
            }
        }
        
        else if (Info->IoStage == IoStage_ReadingBody)
        {
            // No processing is done here. This just releases the semaphore so that
            // the app can continue running.
            
            IncreaseSemaphore(&Info->Semaphore);
        }
        
        else if (Info->IoStage == IoStage_App)
        {
            Info->IoStage = IoStage_SendingHeader;
            PrepareResponse(Conn, Info);
            SendData(Conn);
        }
        
        else if (Info->IoStage == IoStage_SendingHeader)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                if (Info->Response.CookiesSize > 0)
                {
                    Info->IoStage = IoStage_SendingCookie;
                    Conn->IoBuffer = (u8*)Info->Response.Cookies;
                    Conn->IoSize = Info->Response.CookiesSize;
                    SendData(Conn);
                }
                else if (Info->Response.PayloadSize > 0)
                {
                    Info->IoStage = IoStage_SendingPayload;
                    Conn->IoFile = (file)Info->Response.Payload;
                    Conn->IoSize = Info->Response.PayloadSize;
                    SendPayload(Conn, Info, 0);
                }
                else
                {
                    WrapUpTransaction(Conn, Info);
                }
            }
            else
            {
                Conn->IoBuffer += Conn->BytesTransferred;
                Conn->IoSize -= Conn->BytesTransferred;
                SendData(Conn);
            }
        }
        
        else if (Info->IoStage == IoStage_SendingCookie)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                if (Info->Response.PayloadSize > 0)
                {
                    Info->IoStage = IoStage_SendingPayload;
                    Conn->IoFile = (file)Info->Response.Payload;
                    Conn->IoSize = Info->Response.PayloadSize;
                    SendPayload(Conn, Info, 0);
                }
                else
                {
                    WrapUpTransaction(Conn, Info);
                }
            }
            else
            {
                Conn->IoBuffer += Conn->BytesTransferred;
                Conn->IoSize -= Conn->BytesTransferred;
                SendData(Conn);
            }
        }
        
        else if (Info->IoStage == IoStage_SendingPayload)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                WrapUpTransaction(Conn, Info);
            }
            else
            {
                SendPayload(Conn, Info, Conn->BytesTransferred);
            }
        }
        
        else if (Info->IoStage == IoStage_Terminating)
        {
            CleanupInfo(Info);
            CleanupConn(Conn);
            MPSCFreeListPush(SocketList, Conn);
        }
    }
    
    return 0;
}


internal bool
PnpAppServer(parsed_args Args)
{
    InitServer();
    
    path ReferencePath = Path(gServerInfo.FilesPath);
    AppendArrayToPath(Args.RootPath, &ReferencePath);
    if (!IsExistingDir(ReferencePath.Base))
    {
        return false;
    }
    gServerInfo.BasePathSize = ReferencePath.WriteCur;
    AppendArrayToPath(Args.FilesFolder, &ReferencePath);
    if (!IsExistingDir(ReferencePath.Base))
    {
        return false;
    }
    gServerInfo.FilesPathSize = ReferencePath.WriteCur;
    
    gServerInfo.ServerName = "PnP AppServer";
    gServerInfo.LoadAppMutex = InitMutex();
    
    if (!AddListeningSocket(Proto_TCPIP4, Args.Port))
    {
        return false;
    }
    
    usz MaxNumConns = 1024;
    usz IoPageSize = Kilobyte(4);
    usz FullConnSize = sizeof(ts_io) + sizeof(io_info);
    usz IoBufferSize = IoPageSize - FullConnSize;
    buffer SocketMem = GetMemory(MaxNumConns * IoPageSize, NULL, MEM_WRITE);
    
    mpsc_freelist SocketList;
    InitMPSCFreeList(&SocketList);
    for (usz Count = 0; Count < MaxNumConns; Count++)
    {
        ts_io* Conn = PushSize(&SocketMem, IoPageSize, ts_io);
        Conn->Socket = INVALID_FILE;
        io_info* Info = (io_info*)&Conn[1];
        Info->IoBuffer.Base = (u8*)Conn + FullConnSize;
        Info->IoBuffer.Size = IoBufferSize;
        Info->Semaphore = InitSemaphore(0);
        MPSCFreeListPush(&SocketList, Conn);
    }
    
    u32 NumThreads = gSysInfo.NumThreads;
    for (usz Count = 0; Count < NumThreads; Count++)
    {
        thread Thread = InitThread(IoThread, &SocketList, true);
        CloseThread(&Thread);
    }
    
    for (;;)
    {
        ts_listen Listen = ListenForConnections();
        if (Listen.Socket != INVALID_FILE)
        {
            ts_io* Conn = (ts_io*)MPSCFreeListPop(&SocketList);
            if (Conn)
            {
                io_info* Info = (io_info*)&Conn[1];
                Info->IoStage = IoStage_Accepting;
                Conn->IoBuffer = Info->IoBuffer.Base;
                Conn->IoSize = Info->IoBuffer.Size;
                
                if (!AcceptConn(Listen, Conn))
                {
                    MPSCFreeListPush(&SocketList, Conn);
                }
            }
            else
            {
                // TODO: create more conns?
            }
        }
    }
    
    CloseServer();
    
    return true;
}

#if defined(TT_WINDOWS)
int wmain(int Argc, wchar_t** Argv)
{
    parsed_args Args = {0};
    Args.RootPath = (Argc >= 2) ? Argv[1] : L".";
    Args.FilesFolder = (Argc >= 3) ? Argv[2] : L"wwwroot";
    Args.Port = (Argc >= 4) ? StringToUInt(StringC(Argv[3], EC_UTF16LE)) : 8080;
    
    PnpAppServer(Args);
    return 0;
}
#else
int main(int Argc, char** Argv)
{
    parsed_args Args = {0};
    Args.RootPath = (Argc >= 2) ? Argv[1] : ".";
    Args.FilesFolder = (Argc >= 3) ? Argv[2] : "wwwroot";
    Args.Port = (Argc >= 4) ? StringToUInt(StringC(Argv[3], EC_UTF8)) : 8080;
    
    PnpAppServer(Args);
    return 0;
}
#endif