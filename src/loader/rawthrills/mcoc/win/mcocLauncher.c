// mcoc2.exe: Marvel Contest of Champions on Windows (see mcocWin.c). It
// stands where Unity's WindowsPlayer.exe would (whose only work is to call
// UnityPlayer.dll's UnityMain): mcocwin.dll, beside it, hooks Mono and starts
// Unity.
//   mcoc2.exe [-c settings.ini] [Unity's own arguments]
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

typedef int (*McocWinMain)(HINSTANCE, const wchar_t *, int);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prev, LPWSTR args, int show)
{
    wchar_t path[MAX_PATH], *slash;
    HMODULE lib;
    McocWinMain start;
    (void)prev;
    GetModuleFileNameW(NULL, path, MAX_PATH);
    if ((slash = wcsrchr(path, L'\\')))
        wcscpy(slash + 1, L"mcocwin.dll");
    if (!(lib = LoadLibraryW(path)) || !(start = (McocWinMain)(void *)GetProcAddress(lib, "mcocWinMain")))
    {
        MessageBoxW(NULL, L"mcocwin.dll is missing beside mcoc2.exe.", L"Marvel Contest of Champions", MB_ICONERROR);
        return 1;
    }
    return start(instance, args, show);
}
