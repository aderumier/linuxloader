#pragma once

#include <string>
#include <cstdint>

// Forward declarations to avoid exposing ELFIO directly in headers if possible,
// but since we'll likely use it heavily, including ELFIO might be easier.
// For now, we keep it simple.
namespace ELFIO
{
    class elfio;
}

class ElfLoader;

// The loaded game (the main executable), for the code that patches it
// (Raw Thrills); nullptr before it is loaded.
extern ElfLoader *g_ElfMain;

class ElfLoader
{
  public:
    ElfLoader();
    ~ElfLoader();

    // Pre-reserve the ELF's fixed virtual address range so that DLLs
    // loaded later (e.g. msys-2.0.dll) cannot claim the same region.
    // Must be called before any LoadLibraryA that might conflict.
    static void PreReserveAddressSpace(const std::string &path);

    // Loads the ELF file into memory and resolves dependencies
    bool Load(const std::string &path);

    // Executes the loaded ELF file
    bool Execute(int argc, char **argv, char **envp);

    void SetIsSharedObject(bool isSO)
    {
        m_IsSharedObject = isSO;
    }

    // The game's main-thread stack (the Linux stack Execute builds before
    // jumping to the entry point): Raw Thrills wants more than the 8 MB
    // default, as the Linux launcher's limit (see rawthrills/rtLaunch.c).
    static void SetMainStackSize(uint32_t size);

    // Returns the runtime base address of the loaded ELF (0x08048000 for standard ELFs or the allocated address for PIE)
    void *GetBaseAddress() const
    {
        return m_ImageBase;
    }

    // Look up a symbol in this ELF's dynsym table, returns runtime address
    // or nullptr: from the file's sections, else from the loaded image's
    // .dynamic (dumps without section headers, see ReadImageDynsym).
    void *FindExportedSymbol(const std::string &name) const;

    // Size of a dynsym symbol: 0 when it is not there.
    uint32_t GetSymbolSize(const std::string &name) const;

    // The executable code segment (a PT_LOAD with PF_X) containing the
    // runtime address, biased: for in-place patching of the game's code.
    bool GetCodeSegment(uint32_t address, uint32_t *start, uint32_t *end) const;

    // This ELF's dynamic symbols, one per callback (a 0 return stops):
    // the value is biased, as loaded. The Namco N2 hooks walk these.
    bool ForEachExportedSymbol(int (*callback)(const char *name, uint32_t value, uint32_t size, unsigned char type, unsigned shndx,
                                               void *arg),
                               void *arg) const;

    // The writable code segment (a PT_LOAD with PF_W|PF_X, the game's
    // ".writetext" its GL entry points are written in), or false.
    bool GetWritableCodeSegment(uint32_t *start, uint32_t *end) const;

    // The loaded (relocated) .dynamic section of this ELF, for the C code
    // that reads its relocations and symbols (Raw Thrills): 0 when absent.
    uint32_t GetDynamicSectionAddress() const;

    bool LoadMapAndExport(const std::string &path);
    bool ProcessRelocations();
    bool RunInit();

  private:
    // Register all collected .eh_frame sections with the MinGW DW2 unwinder.
    static void RegisterAllEhFrames();

    // The game's main-thread stack size (see SetMainStackSize).
    static uint32_t s_MainStackSize;

    ELFIO::elfio *m_Elfio;
    bool m_IsSharedObject = false;
    bool m_InitRun = false;
    bool m_Relocated = false;
    bool m_Initialized = false;
    std::string m_Path;

    bool ParseElf(const std::string &path);
    bool MapSegmentsToMemory();
    bool LoadDependencies();
    void LoadImageDependencies();
    void ProcessImageRelocations();
    bool ResolveVTables();
    bool ExportSymbols();

    // Register this ELF in the TLS module registry (see tlsSupport):
    // the main executable always becomes module 1, a shared object gets
    // the next index when it has a PT_TLS segment.
    void RegisterTlsSegment();

    // Base address where the ELF is loaded
    void *m_BaseAddress = nullptr;
    void *m_ImageBase = nullptr;
    uint32_t m_LoadBias = 0;

    // This ELF's index in the TLS DTV (module 1 for the main
    // executable, 0 when a shared object has no TLS).
    uint32_t m_TlsModid = 0;

    // This ELF's dynamic symbol table as loaded in the image (PT_DYNAMIC ->
    // DT_SYMTAB / DT_STRTAB, the table ld.so binds through): a dump taken
    // from a running cabinet can have no section headers at all, so this is
    // the only way to read its exported symbols (Linux's dlsym uses it).
    // Cached on first use; 0 when the image has no readable table.
    mutable bool m_DynsymRead = false;
    mutable uint32_t m_DynsymTable = 0;
    mutable uint32_t m_DynsymStrings = 0;
    mutable uint32_t m_DynsymStrsz = 0;
    mutable uint32_t m_DynsymCount = 0;

    void ReadImageDynsym() const;
    bool ImageAddressValid(uint32_t address) const;
};
