#include "pnp-server.h"
#include "pnp-server-app.cpp"

global pnp_server gServerInfo;

internal void
LoadApps(void)
{
    iter_dir Iter = {0};
    path FilesPath = Path(gServerInfo.FilesPath);
    FilesPath.WriteCur = gServerInfo.BasePathSize;
    InitIterDir(&Iter, FilesPath);
    
    char WWWRootBuf[MAX_PATH_SIZE] = {0};
    path WWWRoot = Path(WWWRootBuf);
    AppendStringToString(StrLit("wwwroot"), &WWWRoot);
    
    char AppExtBuf[10] = {0};
    path AppExt = Path(AppExtBuf);
#if defined (TT_WINDOWS)
    AppendStringToString(StrLit(".dll"), &AppExt);
#else
    AppendStringToString(StrLit(".so"), &AppExt);
#endif
    
    while (ListFiles(&Iter))
    {
        path Filename = PathLit(Iter.Filename);
        if (Iter.IsDir)
        {
            if (EqualStrings(WWWRoot, Filename))
            {
                AppendPathToPath(WWWRoot, &FilesPath);
                gServerInfo.FilesPathSize = FilesPath.WriteCur;
            }
        }
        else if (gServerInfo.NumApps < ArrayCount(gServerInfo.Apps))
        {
            char* ExtPtr = (char*)CharInString('.', Filename, RETURN_PTR_FIND|SEARCH_REVERSE);
            if (ExtPtr)
            {
                path Ext = PathLit(ExtPtr);
                if (EqualStrings(Ext, AppExt))
                {
                    AppendPathToPath(Filename, &FilesPath);
                    file App = LoadExternalLibrary(FilesPath.Base);
                    if (App)
                    {
                        module_main AppEntry = (module_main)LoadExternalSymbol(App, "ModuleMain");
                        if (AppEntry)
                        {
                            app_info* AppInfo = &gServerInfo.Apps[gServerInfo.NumApps++];
                            AppInfo->Entry = AppEntry;
                            
                            Filename.WriteCur = ExtPtr - Filename.Base;
                            string AppInfoName = String(AppInfo->Name, 0, sizeof(AppInfo->Name), EC_UTF8);
                            Transcode(Filename, &AppInfoName);
                            AppInfo->NameSize = AppInfoName.WriteCur;
                            
                            AppInfo->Arena.Base = (u8*)GetMemory(APP_ARENA_SIZE, 0, MEM_READ|MEM_WRITE);
                            AppInfo->Arena.Size = APP_ARENA_SIZE;
                            
                            app_init AppInit = (app_init)LoadExternalSymbol(App, "AppInit");
                            if (AppInit)
                            {
                                AppInit(&AppInfo->Arena);
                            }
                        }
                    }
                    FilesPath.WriteCur = gServerInfo.BasePathSize;
                    FilesPath.Base[FilesPath.WriteCur] = 0;
                }
            }
        }
    }
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
    app_info* Result = NULL;
    
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
            Result = AppInfo;
            break;
        }
    }
    
    return Result;
}

internal char*
ExtToMIME(string Ext)
{
    if (EqualStrings(Ext, StrLit("htm"))) return "text/html; charset=utf-8";
    if (EqualStrings(Ext, StrLit("html"))) return "text/html; charset=utf-8";
    if (EqualStrings(Ext, StrLit("jpg"))) return "image/jpeg";
    if (EqualStrings(Ext, StrLit("jpeg"))) return "image/jpeg";
    
    return "application/octet-stream";
}

internal bool
RecvFullRequestBody(ts_io* Conn, ts_body* Body, usz MaxBodySize)
{
    io_info* Info = (io_info*)&Conn[1];
    Info->Stage = IoStage_ReadingBody;
    
    u64 AmountReceived = Conn->BytesTransferred - Info->Request.HeaderSize;
    u64 AmountToRecv = Body->Size - AmountReceived;
    u64 AmountLeftOnIoBuffer = Info->IoBufferSize - (Info->Request.HeaderSize + AmountReceived);
    
    // If size is too big, read in place until it has read everything. This is so we can return
    // a response with status code 413. Many clients (like browsers) only start reading after
    // sending their entire payload, so this makes sure it's read, but not processed. Content
    // of read is discarded.
    if (Body->Size > MaxBodySize)
    {
        Info->Response.StatusCode = 413;
        
        usz ThrowawayMemSize = Megabyte(1);
        void* ThrowawayMem = GetMemory(ThrowawayMemSize, 0, MEM_WRITE);
        if (ThrowawayMem)
        {
            while (AmountToRecv > 0)
            {
                u32 BytesToRecv = Min(AmountToRecv, ThrowawayMemSize);
                RecvPackets(Conn, &ThrowawayMem, &BytesToRecv, 1);
                
                u32 BytesRecv = WaitOnIoCompletion(Conn->Socket, &Conn->Async);
                if (BytesRecv == 0)
                {
                    Info->Response.StatusCode = 408;
                    return false;
                }
                AmountToRecv -= BytesRecv;
            }
            FreeMemory(ThrowawayMem);
        }
        return false;
    }
    
    // Checks if size left in Header buffer is enough to fit entire payload. Allocs new if not.
    if (AmountToRecv > AmountLeftOnIoBuffer)
    {
        void* NewBuffer = GetMemory(Body->Size, 0, MEM_READ|MEM_WRITE);
        if (!NewBuffer)
        {
            Info->Response.StatusCode = 503;
            return false;
        }
        CopyData(NewBuffer, Body->Size, Body->Base, AmountReceived);
        Body->Base = (u8*)NewBuffer;
    }
    
    // Receives entire payload.
    while (AmountToRecv > 0)
    {
        void* RecvPtr = Body->Base + AmountReceived;
        u32 BytesToRecv = Min(AmountToRecv, U32_MAX);
        RecvPackets(Conn, &RecvPtr, &BytesToRecv, 1);
        
        u32 BytesRecv = WaitOnIoCompletion(Conn->Socket, &Conn->Async);
        if (BytesRecv == 0)
        {
            Info->Response.StatusCode = 408;
            return false;
        }
        AmountReceived += BytesRecv;
        AmountToRecv -= BytesRecv;
    }
    
    return true;
}

void*
AppThread(void* Params)
{
    ts_io* Conn = (ts_io*)Params;
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
    
    if (Http.Body && ((usz)Http.Body < (usz)Info->IoBuffer
                      || (usz)Http.Body > (usz)(Info->IoBuffer + Info->IoBufferSize)))
    {
        FreeMemory(Http.Body);
    }
    
    Info->Response.StatusCode = Http.ReturnCode;
    Info->Response.PayloadType = Http.PayloadType;
    Info->Response.PayloadSize = Http.PayloadSize;
    Info->Response.CookiesSize = Http.CookiesSize;
    
    Info->Stage = IoStage_App;
    SendToIoQueue(Info->IoQueue, Conn);
    
    return 0;
}

internal void
EnterApp(ts_io* Conn, io_info* Info)
{
    // TODO: Take thread from threadpool, only create new one if pool is empty.
    usz ThreadId = 0;
    file NewThread = ThreadCreate(AppThread, Conn, &ThreadId, true);
    CloseFileHandle(NewThread);
}

internal void
FillBuffersAndSend(ts_io* Conn, void* Header, u32 HeaderSize, void* Cookies, u32 CookiesSize, void* Payload, u32 PayloadSize)
{
    int Idx = 0;
    void* OutBuffers[3] = { 0, 0, 0 };
    u32 OutBuffersSize[3] = { 0, 0, 0 };
    
    if (HeaderSize > 0)
    {
        OutBuffers[Idx] = Header;
        OutBuffersSize[Idx++] = HeaderSize;
    }
    if (CookiesSize > 0)
    {
        OutBuffers[Idx] = Cookies;
        OutBuffersSize[Idx++] = CookiesSize;
    }
    if (PayloadSize > 0)
    {
        OutBuffers[Idx] = Payload;
        OutBuffersSize[Idx++] = PayloadSize;
    }
    
    SendPackets(Conn, OutBuffers, OutBuffersSize, Idx);
}

internal void
PrepareResponse(ts_io* Conn, io_info* Info)
{
    if (Info->Response.StatusCode == 200)
    {
        string Connection = GetHeaderByKey(&Info->Request, "Connection");
        Info->Response.KeepAlive = (EqualStrings(Connection, StrLit("keep-alive"))
                                    || EqualStrings(Connection, StrLit("Keep-Alive")));
        Info->Response.Version = Info->Request.Version;
    }
    
    string OutBuffer = String(Info->IoBuffer, 0, Info->IoBufferSize, EC_ASCII);
    CraftHttpResponseHeader(&Info->Response, &OutBuffer);
    
    Conn->BytesTransferred = 0;
    Info->Stage = IoStage_Sending;
    
    FillBuffersAndSend(Conn, OutBuffer.Base, OutBuffer.WriteCur, Info->Response.Cookies, Info->Response.CookiesSize, Info->Response.Payload, Info->Response.PayloadSize);
}

internal void
ProcessReading(ts_io* Conn, io_info* Info)
{
    string InBuffer = String(Info->IoBuffer, Conn->BytesTransferred, Info->IoBufferSize, EC_ASCII);
    ts_http_parse Result = ParseHttpHeader(InBuffer, &Info->Request);
    
    if (Result == HttpParse_OK)
    {
        string Resource = String(Info->Request.Base + Info->Request.UriOffset, Info->Request.PathSize, 0, EC_ASCII);
        
        switch (Info->Request.Verb)
        {
            case HttpVerb_Post:
            case HttpVerb_Put:
            {
                Info->Body = GetBodyInfo(&Info->Request);
                if (Info->Body.Base)
                {
                    // If too large, break connection.
                    if (Info->Body.Size > SERVER_MAX_BODY_SIZE)
                    {
                        Info->Stage = IoStage_Terminating;
                        SendToIoQueue(Info->IoQueue, Conn);
                        break;
                    }
                }
                else
                {
                    Info->Response.StatusCode = 400;
                    PrepareResponse(Conn, Info);
                    break;
                }
            }
            
            case HttpVerb_Delete:
            {
                app_info* AppInfo = GetResourceAppInfo(Resource);
                if (AppInfo)
                {
                    Info->AppInfo = AppInfo;
                    EnterApp(Conn, Info);
                }
                else
                {
                    Info->Response.StatusCode = 404;
                    PrepareResponse(Conn, Info);
                }
            } break;
            
            case HttpVerb_Get:
            {
                string Ext;
                app_info* AppInfo = GetResourceAppInfo(Resource);
                if (AppInfo)
                {
                    Info->AppInfo = AppInfo;
                    EnterApp(Conn, Info);
                }
                else if ((Ext = GetResourceExt(Resource)).Base)
                {
                    char ResourcePathBuf[MAX_PATH_SIZE] = {0};
                    path ResourcePath = Path(ResourcePathBuf);
                    Resource.Base++;
                    Resource.WriteCur--;
                    AppendDataToPath(gServerInfo.FilesPath, gServerInfo.FilesPathSize, &ResourcePath);
                    AppendStringToPath(Resource, &ResourcePath);
                    
                    file ResourceFile = OpenFileHandle(ResourcePathBuf, READ_SHARE|ASYNC_FILE);
                    if (ResourceFile != INVALID_FILE)
                    {
                        // Puts file handle in Body pointer because it's not being used. After file is done
                        // reading, this handle will be freed.
                        Info->Body.Base = (u8*)ResourceFile;
                        
                        Info->Response.PayloadSize = FileSizeOf(ResourceFile);
                        Info->Response.Payload = (char*)GetMemory(Info->Response.PayloadSize, 0, MEM_READ|MEM_WRITE);
                        Info->Response.PayloadType = ExtToMIME(Ext);
                        
                        AddFileToIoQueue(ResourceFile, Info->IoQueue);
                        
                        Info->Stage = IoStage_Payload;
                        Conn->BytesTransferred = 0;
                        ReadFileAsync(ResourceFile, Info->Response.Payload, Info->Response.PayloadSize, &Conn->Async);
                    }
                    else // File not found.
                    {
                        Info->Response.StatusCode = 404;
                        PrepareResponse(Conn, Info);
                    }
                }
                else // Not a valid app, nor a file.
                {
                    Info->Response.StatusCode = 404;
                    PrepareResponse(Conn, Info);
                }
            } break;
            
            default: // Implement new Verbs here.
            {
                Info->Response.StatusCode = 501;
                PrepareResponse(Conn, Info);
            }
        }
    }
    
    else if (Result == HttpParse_HeaderIncomplete)
    {
        void* InBuffer[1] = { Info->IoBuffer + Info->Request.HeaderSize };
        u32 InBufferSize[1] = { Info->IoBufferSize - Info->Request.HeaderSize };
        Info->Stage = IoStage_Reading;
        RecvPackets(Conn, InBuffer, InBufferSize, 1);
    }
    
    else if (Result == HttpParse_HeaderInvalid
             || Result == HttpParse_HeaderMalicious)
    {
        Info->Response.StatusCode = 400;
        PrepareResponse(Conn, Info);
    }
    
    else if (Result == HttpParse_TooManyHeaders)
    {
        Info->Stage = IoStage_Terminating;
        SendToIoQueue(Info->IoQueue, Conn);
    }
}

internal void
CleanupInfo(io_info* Info)
{
    memset(Info->IoBuffer, 0, Info->IoBufferSize);
    
    if (Info->Response.Cookies) FreeMemory(Info->Response.Cookies);
    if (Info->Response.Payload) FreeMemory(Info->Response.Payload);
    memset(&Info->Request, 0, sizeof(ts_request));
    memset(&Info->Response, 0, sizeof(ts_response));
}


void*
IoThread(void* Param)
{
    io_thread_params* IoParams = (io_thread_params*)Param;
    
    for (;;)
    {
        ts_io* Conn;
        u32 BytesTransferred = WaitOnIoQueue(&IoParams->IoQueue, &Conn);
        
        io_info* Info = (io_info*)&Conn[1];
        Conn->BytesTransferred += BytesTransferred;
        switch (Info->Stage)
        {
            case IoStage_Accepting:
            {
                if (BytesTransferred > 0
                    && BindClientToServer(Conn->Socket, IoParams->Listening))
                {
                    Info->IoQueue = &IoParams->IoQueue;
                    ProcessReading(Conn, Info);
                }
                else
                {
                    Info->Stage = IoStage_Terminating;
                    SendToIoQueue(&IoParams->IoQueue, Conn);
                }
                // From here it can go to IoStage_Reading (if recv incomplete or preparing payload),
                // IoStage_App (if it's an app call), IoStage_Sending (if header was refused or error),
                // of IoStage_Terminating (if conn needs to be aborted).
            } break;
            
            case IoStage_Reading:
            {
                if (BytesTransferred == 0)
                {
                    Info->Stage = IoStage_Terminating;
                    SendToIoQueue(&IoParams->IoQueue, Conn);
                }
                else
                {
                    ProcessReading(Conn, Info);
                }
            } break;
            
            case IoStage_Payload:
            {
                if (Conn->BytesTransferred == Info->Response.PayloadSize)
                {
                    file File = (file)Info->Body.Base;
                    CloseFileHandle(File);
                    
                    Info->Response.StatusCode = 200;
                    PrepareResponse(Conn, Info);
                }
                else { /* Waits for more payload completion messages. */ }
            } break;
            
            case IoStage_App:
            {
                if (BytesTransferred == 0)
                {
                    PrepareResponse(Conn, Info);
                }
                else
                {
                    // This is actually IoStage_ReadingBody, in case the packages get unqueued after
                    // the app exits and changes stage to IoStage_App. Ignore.
                }
            } break;
            
            case IoStage_Sending:
            {
                if (BytesTransferred == 0)
                {
                    Info->Stage = IoStage_Terminating;
                    SendToIoQueue(&IoParams->IoQueue, Conn);
                }
                
                // Still sending outheader/cookies/payload. Keep sending.
                // After everything is sent, send to closing if need, or RecvPackets() if conn is open.
                
                else
                {
                    if (Conn->BytesTransferred >= Info->Response.HeaderSize)
                    {
                        if (Conn->BytesTransferred >= (Info->Response.HeaderSize + Info->Response.CookiesSize))
                        {
                            if (Conn->BytesTransferred >= (Info->Response.HeaderSize + Info->Response.CookiesSize + Info->Response.PayloadSize))
                            {
                                bool KeepAlive = Info->Response.KeepAlive;
                                CleanupInfo(Info);
                                
                                if (KeepAlive)
                                {
                                    Info->Stage = IoStage_Reading;
                                    Conn->BytesTransferred = 0;
                                    RecvPackets(Conn, (void**)&Info->IoBuffer, &Info->IoBufferSize, 1);
                                }
                                else
                                {
                                    Info->Stage = IoStage_Closing;
                                    DisconnectSocket(Conn);
                                }
                            }
                            else // Still sending payload.
                            {
                                u32 AmountPayloadSent = Conn->BytesTransferred - Info->Response.HeaderSize - Info->Response.CookiesSize;
                                FillBuffersAndSend(Conn, 0, 0, 0, 0, Info->Response.Payload + AmountPayloadSent, Info->Response.PayloadSize - AmountPayloadSent);
                            }
                        }
                        else // Still sending cookies.
                        {
                            u32 AmountCookiesSent = Conn->BytesTransferred - Info->Response.HeaderSize;
                            FillBuffersAndSend(Conn, 0, 0, Info->Response.Cookies + AmountCookiesSent, Info->Response.CookiesSize - AmountCookiesSent, Info->Response.Payload, Info->Response.PayloadSize);
                        }
                    }
                    else // Still sending header.
                    {
                        FillBuffersAndSend(Conn, Info->IoBuffer + Conn->BytesTransferred, Info->Response.HeaderSize - Conn->BytesTransferred, Info->Response.Cookies, Info->Response.CookiesSize, Info->Response.Payload, Info->Response.PayloadSize);
                    }
                }
            } break;
            
            case IoStage_Terminating:
            {
                TerminateConn(Conn);
                CleanupInfo(Info);
                MPSCQueuePush(IoParams->SocketQueue, Conn);
            } break;
            
            case IoStage_Closing:
            {
                MPSCQueuePush(IoParams->SocketQueue, Conn);
            } break;
        }
    }
    
    return 0;
}

external void
PnpAppServer(void)
{
    InitServer();
    
    LoadApps();
    gServerName = "Pnp AppServer - v.05";
    
    u32 NumThreads = gSysInfo.NumThreads;
    ts_io_queue IoQueue = SetupIoQueue(NumThreads);
    ts_listen Listening = SetupListeningSocket(Proto_TCPIP4, 50000, IoQueue);
    
    usz MaxNumConns = 1024;
    
    usz FullConnSize = sizeof(ts_io) + sizeof(io_info);
    void* IoBuffer = GetMemory(MaxNumConns * FullConnSize, NULL, MEM_READ|MEM_WRITE);
    buffer IoArena = Buffer((u8*)IoBuffer, 0, MaxNumConns * FullConnSize);
    
    mpsc_queue SocketQueue;
    InitMPSCQueue(&SocketQueue);
    for (usz Count = 0; Count < MaxNumConns; Count++)
    {
        ts_io* Conn = PushSize(&IoArena, FullConnSize, ts_io);
        Conn->Socket = INVALID_SOCKET;
        MPSCQueuePush(&SocketQueue, Conn);
    }
    
    io_thread_params Params = { IoQueue, &SocketQueue, Listening.Socket };
    for (usz Count = 0; Count < NumThreads; Count++)
    {
        usz ThreadId = 0;
        file Thread = ThreadCreate(IoThread, &Params, &ThreadId, true);
        CloseFileHandle(Thread);
    }
    
    for (;;)
    {
        ts_event Event = ListenForEvents(&Listening.Socket, &Listening.Event, 1);
        if (Event == Event_Accept)
        {
            ts_io* Conn = (ts_io*)MPSCQueuePop(&SocketQueue);
            if (Conn)
            {
                io_info* Info = (io_info*)&Conn[1];
                if (!Info->IoBuffer)
                {
                    Info->IoBuffer = (char*)GetMemory(Kilobyte(4), NULL, MEM_READ|MEM_WRITE);
                    Info->IoBufferSize = Kilobyte(4);
                }
                
                Info->Stage = IoStage_Accepting;
                if (!AcceptConn(Listening, Conn, Info->IoBuffer, Info->IoBufferSize, IoQueue))
                {
                    MPSCQueuePush(&SocketQueue, Conn);
                }
            }
            else
            {
                // TODO: create more conns?
            }
        }
    }
}