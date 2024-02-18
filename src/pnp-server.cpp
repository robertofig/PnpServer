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
        // TODO: change this to a hashtable lookup.
        
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
    // TODO: implement full list of MIME types here.
    
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
    io_aux* Aux = (io_aux*)&Conn[1];
    Aux->IoStage = IoStage_ReadingBody;
    
    u64 AmountReceived = Aux->IoBuffer.WriteCur - Aux->Request.HeaderSize;
    u64 AmountToRecv = Body->Size - AmountReceived;
    u64 AmountLeftOnIoBuffer = Aux->IoBuffer.Size - Aux->IoBuffer.WriteCur;
    
    if (Body->Size <= MaxBodySize)
    {
        // Checks if size left in Header buffer is enough to fit entire payload.
        // Allocs new if not.
        
        if (AmountToRecv > AmountLeftOnIoBuffer)
        {
            buffer NewBuffer = GetMemory(Body->Size, 0, MEM_READ|MEM_WRITE);
            if (!NewBuffer.Base)
            {
                Aux->Response.StatusCode = 503;
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
            
            // This semaphore is so we only continue after the previous recv
            // has been dequeued (we need the BytesReceived to know how much
            // to recv next).
            
            WaitOnSemaphore(&Aux->Semaphore);
            
            AmountReceived += Conn->BytesTransferred;
            AmountToRecv -= Conn->BytesTransferred;
        }
        return true;
    }
    else
    {
        Aux->Response.StatusCode = 413;
        DisconnectSocket(Conn, TS_DISCONNECT_RECV);
        return false;
    }
}

THREAD_PROC(AppThread)
{
    ts_io* Conn = (ts_io*)Arg;
    io_aux* Aux = (io_aux*)&Conn[1];
    
    http Http = {0};
    Http.Object = Conn;
    Http.Arena = (app_arena*)&Aux->AppInfo->Arena;
    Http.Verb = (http_verb)Aux->Request.Verb;
    Http.Path = Aux->Request.Base + Aux->Request.UriOffset;
    Http.PathSize = Aux->Request.PathSize;
    Http.Query = Http.Path + Http.PathSize + 1;
    Http.QuerySize = Aux->Request.QuerySize;
    Http.HeaderCount = Aux->Request.NumHeaders;
    Http.Body = Aux->Body.Base;
    Http.BodySize = Aux->Body.Size;
    Http.ContentType = Aux->Body.ContentType;
    Http.ContentTypeSize = Aux->Body.ContentTypeSize;
    
    Http.GetHeaderByKey = AppGetHeaderByKey;
    Http.GetHeaderByIdx = AppGetHeaderByIdx;
    Http.RecvFullRequestBody = AppRecvFullRequestBody;
    Http.ParseFormData = AppParseFormData;
    Http.GetFormFieldByName = AppGetFormFieldByName;
    Http.GetFormFieldByIdx = AppGetFormFieldByIdx;
    Http.AllocPayload = AppAllocPayload;
    Http.AllocCookies = AppAllocCookies;
    
    Aux->AppInfo->Entry(&Http, &Aux->AppInfo->Arena);
    
    Aux->Response.StatusCode = Http.ReturnCode;
    Aux->Response.MimeType = Http.PayloadType;
    Aux->Response.PayloadSize = Http.PayloadSize;
    Aux->Response.CookiesSize = Http.CookiesSize;
    
    Aux->IoStage = IoStage_App; // Has to be reset here because app can change it.
    Conn->BytesTransferred = 0;
    SendToIoQueue(Conn);
    
    return 0;
}

internal void
EnterApp(ts_io* Conn, io_aux* Aux)
{
    // TODO: Take thread from threadpool, only create new one if pool is empty.
    
    thread NewThread = InitThread(AppThread, Conn, false);
    CloseThread(&NewThread);
}

internal void
PrepareResponse(ts_io* Conn, io_aux* Aux)
{
    if (Aux->Response.StatusCode == 200)
    {
        string Connection = GetHeaderByKey(&Aux->Request, "Connection");
        Aux->Response.KeepAlive = (EqualStrings(Connection, StringLit("keep-alive"))
                                   || EqualStrings(Connection, StringLit("Keep-Alive")));
        Aux->Response.Version = Aux->Request.Version;
    }
    
    string OutBuffer = String(Aux->IoBuffer.Base, 0, Aux->IoBuffer.Size, EC_ASCII);
    CraftHttpResponseHeader(&Aux->Response, &OutBuffer, gServerInfo.ServerName);
    
    Conn->BytesTransferred = 0;
    Conn->IoBuffer = (u8*)OutBuffer.Base;
    Conn->IoSize = OutBuffer.WriteCur;
}

internal void
PreparePayload(string Resource, io_aux* Aux, string Ext)
{
    char ResourcePathBuf[MAX_PATH_SIZE] = {0};
    path ResourcePath = Path(ResourcePathBuf);
    AdvanceString(&Resource, 1);
    AppendDataToPath(gServerInfo.FilesPath, gServerInfo.FilesPathSize, &ResourcePath);
    AppendStringToPath(Resource, &ResourcePath);
    
    file File = OpenFileHandle(ResourcePathBuf, READ_SHARE);
    if (File != INVALID_FILE)
    {
        Aux->Response.Payload = (char*)File;
        Aux->Response.PayloadSize = FileSizeOf(File);
        Aux->Response.MimeType = ExtToMIME(Ext);
        Aux->Response.PayloadIsFile = true;
        
        Aux->Response.StatusCode = 200;
        Aux->IoStage = IoStage_SendingHeader;
    }
    else // File not found.
    {
        Aux->Response.StatusCode = 404;
        Aux->IoStage = IoStage_SendingHeader;
    }
}

internal void
ProcessReading(ts_io* Conn, io_aux* Aux)
{
    string InBuffer = {0};
    InBuffer.Buffer = Aux->IoBuffer;
    InBuffer.Enc = EC_ASCII;
    ts_http_parse Result = ParseHttpHeader(InBuffer, &Aux->Request);
    
    if (Result == HttpParse_OK)
    {
        string Resource = String(Aux->Request.Base + Aux->Request.UriOffset,
                                 Aux->Request.PathSize, 0, EC_ASCII);
        
        switch (Aux->Request.Verb)
        {
            case HttpVerb_Post:
            case HttpVerb_Put:
            {
                Aux->Body = GetBodyInfo(&Aux->Request);
                if (!Aux->Body.Base)
                {
                    Aux->Response.StatusCode = 400;
                    Aux->IoStage = IoStage_SendingHeader;
                    break;
                }
                // Else, falls back on HttpVerb_Delete below.
            }
            
            case HttpVerb_Delete:
            {
                app_info* AppInfo = GetResourceAppInfo(Resource);
                if (AppInfo)
                {
                    Aux->AppInfo = AppInfo;
                    Aux->IoStage = IoStage_App;
                }
                else
                {
                    Aux->Response.StatusCode = 404;
                    Aux->IoStage = IoStage_SendingHeader;
                }
            } break;
            
            case HttpVerb_Get:
            {
                string Ext = GetResourceExt(Resource);
                if (Ext.Base)
                {
                    // If it has an extension, it's a file.
                    PreparePayload(Resource, Aux, Ext);
                }
                else
                {
                    // If not, first assume it to be an app.
                    app_info* AppInfo = GetResourceAppInfo(Resource);
                    if (AppInfo)
                    {
                        Aux->AppInfo = AppInfo;
                        Aux->IoStage = IoStage_App;
                    }
                    else
                    {
                        // If no app was found, a final attempt at it being a file.
                        PreparePayload(Resource, Aux, Ext);
                    }
                }
            } break;
            
            default: // Implement new Verbs here.
            {
                Aux->Response.StatusCode = 501;
                Aux->IoStage = IoStage_SendingHeader;
            }
        }
    }
    
    else if (Result == HttpParse_HeaderIncomplete)
    {
        Aux->IoStage = IoStage_Reading;
    }
    
    else if (Result == HttpParse_HeaderInvalid
             || Result == HttpParse_HeaderMalicious)
    {
        Aux->Response.StatusCode = 400;
        Aux->IoStage = IoStage_SendingHeader;
    }
    
    else if (Result == HttpParse_TooManyHeaders)
    {
        Aux->IoStage = IoStage_Terminating;
    }
}

internal void
SendPayload(ts_io* Conn, io_aux* Aux, usz Offset)
{
    if (Aux->Response.PayloadIsFile)
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
CleanupAux(io_aux* Aux)
{
    // This frees all memory and blanks everything, except for [.IoBuffer] and
    // [Semaphore], which can be reused in a new connection.
    
    ClearBuffer(&Aux->IoBuffer);
    if ((Aux->Body.Base < Aux->IoBuffer.Base)
        || (Aux->Body.Base > (Aux->IoBuffer.Base+Aux->IoBuffer.Size)))
    {
        buffer Body = Buffer(Aux->Body.Base, 0, Aux->Body.Size);
        FreeMemory(&Body);
    }
    if (Aux->Response.Cookies)
    {
        buffer Cookies = Buffer(Aux->Response.Cookies, 0, Aux->Response.CookiesSize);
        FreeMemory(&Cookies);
    }
    if (Aux->Response.Payload)
    {
        if (Aux->Response.PayloadIsFile)
        {
            CloseFileHandle((file)Aux->Response.Payload);
        }
        else
        {
            buffer Payload = Buffer(Aux->Response.Payload, 0, Aux->Response.PayloadSize);
            FreeMemory(&Payload);
        }
    }
    memset(&Aux->Request, 0, sizeof(ts_request));
    memset(&Aux->Body, 0, sizeof(ts_body));
    memset(&Aux->Response, 0, sizeof(ts_response));
    Aux->IoStage = IoStage_None;
}

internal void
CleanupConn(ts_io* Conn)
{
    file Socket = Conn->Socket;
    memset(Conn, 0, sizeof(ts_io));
    Conn->Socket = Socket;
}

internal void
WrapUpTransaction(ts_io* Conn, io_aux* Aux)
{
    if (Aux->Response.KeepAlive
        && Conn->Status == Status_Connected) // If it's simplex, we need to disconnect.
    {
        CleanupAux(Aux);
        Aux->IoStage = IoStage_Reading;
        Conn->BytesTransferred = 0;
        Conn->IoBuffer = Aux->IoBuffer.Base;
        Conn->IoSize = Aux->IoBuffer.Size;
        memset(Conn->InternalData, 0, TS_INTERNAL_DATA_SIZE);
        RecvData(Conn);
    }
    else
    {
        Aux->IoStage = IoStage_Terminating;
        DisconnectSocket(Conn, TS_DISCONNECT_BOTH);
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
        io_aux* Aux = (io_aux*)&Conn[1];
        
        //===========================
        // Check for IO failures.
        //===========================
        
        if (Conn->Status == Status_Aborted)
        {
            Aux->IoStage = IoStage_Terminating;
            DisconnectSocket(Conn, TS_DISCONNECT_BOTH);
        }
        
        else if (Conn->Status == Status_Error)
        {
            TerminateConn(Conn);
            CleanupAux(Aux);
            CleanupConn(Conn);
            pnp_info* Info = (pnp_info*)(Conn - sizeof(pnp_info*));
            MPSCFreeListPush(SocketList, Info);
        }
        
        //========================================
        // Process the conn based on the IoStage.
        //========================================
        
        else if (Aux->IoStage == IoStage_Accepting
                 || Aux->IoStage == IoStage_Reading)
        {
            Aux->IoBuffer.WriteCur += Conn->BytesTransferred;
            ProcessReading(Conn, Aux);
            
            // From here it can go to IoStage_Reading (if recv incomplete or preparing
            // payload), IoStage_App (if it's an app call), IoStage_Sending (if header
            // was refused or error), of IoStage_Terminating (if conn needs to be
            // aborted).
            
            if (Aux->IoStage == IoStage_Reading)
            {
                Conn->IoBuffer = Aux->IoBuffer.Base + Aux->IoBuffer.WriteCur;
                Conn->IoSize = Aux->IoBuffer.Size - Aux->IoBuffer.WriteCur;
                RecvData(Conn);
            }
            else if (Aux->IoStage == IoStage_App)
            {
                EnterApp(Conn, Aux);
            }
            else if (Aux->IoStage == IoStage_SendingHeader)
            {
                PrepareResponse(Conn, Aux);
                SendData(Conn);
            }
            else // IoStage_Terminating
            {
                DisconnectSocket(Conn, TS_DISCONNECT_BOTH);
            }
        }
        
        else if (Aux->IoStage == IoStage_ReadingBody)
        {
            // No processing is done here. This just releases the semaphore so that
            // the app can continue running.
            
            IncreaseSemaphore(&Aux->Semaphore);
        }
        
        else if (Aux->IoStage == IoStage_App)
        {
            Aux->IoStage = IoStage_SendingHeader;
            PrepareResponse(Conn, Aux);
            SendData(Conn);
        }
        
        else if (Aux->IoStage == IoStage_SendingHeader)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                if (Aux->Response.CookiesSize > 0)
                {
                    Aux->IoStage = IoStage_SendingCookie;
                    Conn->IoBuffer = (u8*)Aux->Response.Cookies;
                    Conn->IoSize = Aux->Response.CookiesSize;
                    SendData(Conn);
                }
                else if (Aux->Response.PayloadSize > 0)
                {
                    Aux->IoStage = IoStage_SendingPayload;
                    Conn->IoFile = (file)Aux->Response.Payload;
                    Conn->IoSize = Aux->Response.PayloadSize;
                    SendPayload(Conn, Aux, 0);
                }
                else
                {
                    WrapUpTransaction(Conn, Aux);
                }
            }
            else
            {
                Conn->IoBuffer += Conn->BytesTransferred;
                Conn->IoSize -= Conn->BytesTransferred;
                SendData(Conn);
            }
        }
        
        else if (Aux->IoStage == IoStage_SendingCookie)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                if (Aux->Response.PayloadSize > 0)
                {
                    Aux->IoStage = IoStage_SendingPayload;
                    Conn->IoFile = (file)Aux->Response.Payload;
                    Conn->IoSize = Aux->Response.PayloadSize;
                    SendPayload(Conn, Aux, 0);
                }
                else
                {
                    WrapUpTransaction(Conn, Aux);
                }
            }
            else
            {
                Conn->IoBuffer += Conn->BytesTransferred;
                Conn->IoSize -= Conn->BytesTransferred;
                SendData(Conn);
            }
        }
        
        else if (Aux->IoStage == IoStage_SendingPayload)
        {
            if (Conn->BytesTransferred == Conn->IoSize)
            {
                WrapUpTransaction(Conn, Aux);
            }
            else
            {
                SendPayload(Conn, Aux, Conn->BytesTransferred);
            }
        }
        
        else if (Aux->IoStage == IoStage_Terminating)
        {
            CleanupAux(Aux);
            CleanupConn(Conn);
            pnp_info* Info = (pnp_info*)(Conn - sizeof(pnp_info*));
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
    usz IoBufferSize = IoPageSize - sizeof(pnp_info);
    buffer SocketMem = GetMemory(MaxNumConns * IoPageSize, NULL, MEM_WRITE);
    
    mpsc_freelist SocketList;
    InitMPSCFreeList(&SocketList);
    for (usz Count = 0; Count < MaxNumConns; Count++)
    {
        pnp_info* Info = PushSize(&SocketMem, IoPageSize, pnp_info);
        Info->Conn.Socket = INVALID_FILE;
        Info->Aux.IoBuffer.Base = (u8*)Info + sizeof(pnp_info);
        Info->Aux.IoBuffer.Size = IoBufferSize;
        Info->Aux.Semaphore = InitSemaphore(0);
        MPSCFreeListPush(&SocketList, Info);
    }
    
    u32 NumThreads = 1; //gSysInfo.NumThreads;
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
            pnp_info* Info = (pnp_info*)MPSCFreeListPop(&SocketList);
            if (Info)
            {
                Info->Aux.IoStage = IoStage_Accepting;
                Info->Conn.IoBuffer = Info->Aux.IoBuffer.Base;
                Info->Conn.IoSize = Info->Aux.IoBuffer.Size;
                
                if (!AcceptConn(Listen, &Info->Conn))
                {
                    MPSCFreeListPush(&SocketList, Info);
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