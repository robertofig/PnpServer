#include "pnp-server.cpp"

#pragma comment(lib, "user32")
#pragma comment(lib, "shell32")

void ArgSetupAndRun(wchar_t* RootPath)
{
    path BasePath = Path(gServerInfo.FilesPath);
    AppendCWDToPath(&BasePath);
    
    gServerInfo.HtmlEncoding = "; charset=utf8";
    
    if (RootPath)
    {
        AppendArrayToPath(RootPath, &BasePath);
        if (!IsExistingDir(BasePath.Base))
        {
            WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), BasePath.Base, BasePath.WriteCur, 0, 0);
            ExitProcess(-1);
        }
        gServerInfo.BasePathSize = BasePath.WriteCur;
    }
    
    PnpAppServer();
}

#if defined(PNP_DEBUG)
int wmain(int Argc, wchar_t** Argv)
{
    wchar_t* PathStart = (Argc >= 2) ? Argv[1] : NULL;
    ArgSetupAndRun(PathStart);
    
    return 0;
}
#else
void Entry(void)
{
    int Argc = 0;
    LPWSTR* Argv = CommandLineToArgvW(GetCommandLineW(), &Argc);
    
    wchar_t* PathStart = (Argc >= 2) ? Argv[1] : NULL;
    ArgSetupAndRun(PathStart);
    
    ExitProcess(0);
}
#endif