#pragma once

#ifdef __cplusplus
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdint>

class ElfLoader;

class SymbolResolver
{
  public:
    static SymbolResolver &GetInstance()
    {
        static SymbolResolver instance;
        return instance;
    }

    SymbolResolver(SymbolResolver const &) = delete;
    void operator=(SymbolResolver const &) = delete;

    // Initialize library search paths
    void InitSearchPaths(const std::string &libraryPathParam, const std::string &gameElfPath);

    // Add a subdirectory relative to the DLL search base (exeDir/ll-deps or libraryPath)
    void AddDllSearchSubPath(const std::string &subPath);

    // Get the DLL search base path (exeDir/ll-deps or libraryPathParam)
    std::string GetDllSearchBase() const { return m_DllSearchBase; }

    // Register a new mapped library (e.g. "openal32.dll" representing "libopenal.so")
    void RegisterLibrary(const std::string &linuxName, const std::string &windowsName);

    // Load a library mapped by RegisterLibrary
    void LoadNeededLibrary(const std::string &linuxName);

    // The handle of a loaded DLL by file name (case-insensitive), or nullptr.
    void *GetLoadedLibrary(const std::string &dllName);

    // Resolve a function/symbol from the loaded chain of libraries or internal bridges
    void *ResolveSymbol(const std::string &symbolName, std::string *outModuleName);

    // Resolve a symbol by searching ONLY native loaded shared objects (excludes main EXE).
    // Used for R_386_COPY relocations which must copy from the SO, not the EXE's own BSS.
    void *ResolveSymbolInSharedLibs(const std::string &symbolName);

    // Provide the generated VTable for a class/structure
    void *GetVTable(const std::string &className);
    void RegisterVTable(const std::string &className, void *vtablePtr, void **originalSymbolPtr = nullptr);
    // A replacement used only when no loaded library (Linux or Windows)
    // exports the symbol: stubs for libraries a game may ship itself.
    void RegisterFallback(const std::string &symbolName, void *replacement);

    // Register a symbol exported by a dynamically loaded Linux ELF
    void RegisterNativeSymbol(const std::string &symbolName, void *symbolPtr);

    // The main executable's function imports, as relocated (see
    // common/elfImports.h): recorded by the loader, read by the games that
    // stand between the game's calls and their targets.
    void RecordMainImport(const std::string &symbolName, uint32_t *slot);
    const std::vector<std::pair<std::string, uint32_t *>> &MainImports() const { return m_MainImports; }

    // Point the game's import of symbolName at replacement (see
    // common/importHook.h on Windows): its own GOT slot when it imports it,
    // the resolver's map otherwise (its libraries do); returns what the
    // import resolves to, nullptr if it does not.
    void *HookImport(const std::string &symbolName, void *replacement);

    // The address a natively loaded Linux shared object exports under name,
    // or nullptr: the engine of the Counter Strike NEO dump, looked up for
    // its hooks (namco/namcoN2Csneo.c).
    void *FindNativeSymbol(const std::string &symbolName);
    // Whether a Linux shared object of that file name is natively loaded.
    bool IsNativeLoaded(const std::string &fileName);

    // Global relocation and initialization passes
    bool ProcessAllRelocations();
    void PatchAllSOs();
    bool RunAllInits();

  private:
    SymbolResolver();
    ~SymbolResolver()
    {
    }

    // What a symbol resolves to without the resolver's hooks: a native
    // export, a loaded DLL's, or a GL entry point.
    void *FindOriginal(const std::string &symbolName, std::string *outModuleName);

    std::unordered_map<std::string, std::string> m_LibraryMap;
    std::unordered_map<std::string, void *> m_VTables;
    std::unordered_map<std::string, void *> m_Fallbacks;
    std::unordered_map<std::string, void **> m_OriginalSymbolPtrs;
    std::unordered_map<std::string, void *> m_NativeSymbols;
    std::vector<void *> m_LoadedLibraries;        // Stores handles to loaded DLLs
    std::vector<ElfLoader *> m_NativeLoaders;     // Stores loaders for purely native Linux shared objects
    std::vector<std::pair<std::string, uint32_t *>> m_MainImports; // The main executable's function imports (name, GOT slot)
    std::vector<std::string> m_LoadedNativeNames; // Already loaded Linux SOs, by file name
    std::vector<std::pair<uintptr_t, std::string>> m_PendingSOPatches; // Deferred SO patches (base, path)

    // Library search paths
    std::vector<std::string> m_LibrarySearchPaths; // Ordered list of paths to search for libraries

    // Stored paths for late addition of subdirectories
    std::string m_ExeDir;          // Directory of the executable
    std::string m_DllSearchBase;   // Base dll search path (exeDir/ll-deps or libraryPath)
};
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif
    void *bridgeResolveSymbol(const char *symbolName);
    void bridgeLoadNeededLibrary(const char *filename);
    void *bridgeGetLoadedLibrary(const char *dllName);
    void *bridgeHookImport(const char *symbolName, void *replacement);
    // The address a natively loaded Linux shared object exports under name,
    // or NULL (the Counter Strike NEO engine's symbols).
    void *bridgeFindNativeSymbol(const char *symbolName);
    // Loads a Linux shared object (or its mapped DLL) by name, as a
    // DT_NEEDED: 1 when it is natively loaded afterwards.
    int bridgeLoadNativeLibrary(const char *filename);
    void EnsureLibGccLoaded();
    void *GetLibGccSymbol(const char *name);
    int loaderImportSlots(const char **names, uint32_t **slots, int max);
#ifdef __cplusplus
}
#endif