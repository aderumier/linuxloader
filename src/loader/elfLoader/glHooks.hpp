#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void* GLHooks_GetProcAddress(const char* procName);

// Real-GL passthroughs (no Lindbergh wrappers), in cdecl: the system GL's
// stdcall entry points behind a cdecl front, for games that must keep the
// real GL (Raw Thrills, Roxor). NULL when the name has no adapter.
void* GLHooks_RealGlProc(const char* procName);

#ifdef __cplusplus
}
#endif
