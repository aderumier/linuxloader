#if defined(_WIN32) || defined(__MINGW32__)
#include "elfLoader.hpp"
#include "../log/log.h"
#include "memoryManager.hpp"
#include "symbolResolver.hpp"
#include "linuxStack.hpp"
#include "libcBridge.hpp"
#include "tlsSupport.hpp"
#include <elfio.hpp>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unordered_set>
#include <vector>
#include <windows.h>
#include <winnt.h>
#include <psapi.h>

using namespace ELFIO;

ElfLoader *g_ElfMain = nullptr;

// The game's main-thread stack (see SetMainStackSize): 8 MB, as the Linux
// default; Raw Thrills raises it to the launcher's limit.
uint32_t ElfLoader::s_MainStackSize = 8u * 1024u * 1024u;

void ElfLoader::SetMainStackSize(uint32_t size)
{
    if (size)
        s_MainStackSize = size;
}

static std::vector<void *> s_PendingEhFrames;

void ElfLoader::RegisterAllEhFrames()
{
    if (s_PendingEhFrames.empty())
        return;

    HMODULE hGcc = GetModuleHandleA("libgcc_s_dw2-1.dll");
    if (!hGcc)
        hGcc = LoadLibraryA("libgcc_s_dw2-1.dll");

    if (!hGcc)
    {
        log_error("[EH] WARNING: libgcc_s_dw2-1.dll not found.");
        return;
    }

    typedef void (*RegisterFrameInfoFn)(void *, void *);
    RegisterFrameInfoFn registerFrameInfo =
        (RegisterFrameInfoFn)GetProcAddress(hGcc, "__register_frame_info");

    log_info("[EH] libgcc_s_dw2-1.dll: __register_frame_info=%p", registerFrameInfo);

    if (!registerFrameInfo)
    {
        log_error("[EH] WARNING: __register_frame_info not found in libgcc_s_dw2-1.dll");
        return;
    }

    log_info("[EH] Registering %zu .eh_frame sections with MinGW DW2 unwinder...", s_PendingEhFrames.size());

    for (void *ehFrame : s_PendingEhFrames)
    {
        void *ob = VirtualAlloc(NULL, 128, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!ob) continue;
        memset(ob, 0, 128);

        log_debug("[EH]   __register_frame_info(%p, %p)", ehFrame, ob);
        registerFrameInfo(ehFrame, ob);
        log_debug("[EH]   OK");
    }

    log_info("[EH] All .eh_frame sections registered");
    s_PendingEhFrames.clear();
}

ElfLoader::ElfLoader()
{
    m_Elfio = new elfio();
}

ElfLoader::~ElfLoader()
{
    if (m_Elfio)
    {
        delete m_Elfio;
        m_Elfio = nullptr;
    }
}

void ElfLoader::PreReserveAddressSpace(const std::string &path)
{
    FILE *f = fopen(path.c_str(), "rb");
    if (!f)
        return;

    struct Elf32_Ehdr_Raw
    {
        unsigned char e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint32_t e_entry;
        uint32_t e_phoff;
        uint32_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    } ehdr;

    if (fread(&ehdr, sizeof(ehdr), 1, f) != 1)
    {
        fclose(f);
        return;
    }

    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F')
    {
        fclose(f);
        return;
    }

    if (ehdr.e_ident[4] != 1)
    {
        fclose(f);
        return;
    }
    if (ehdr.e_type != ET_EXEC)
    {
        fclose(f);
        return;
    }

    struct Elf32_Phdr_Raw
    {
        uint32_t p_type;
        uint32_t p_offset;
        uint32_t p_vaddr;
        uint32_t p_paddr;
        uint32_t p_filesz;
        uint32_t p_memsz;
        uint32_t p_flags;
        uint32_t p_align;
    } phdr;

    uint32_t minAddr = UINT32_MAX;
    uint32_t maxAddr = 0;

    fseek(f, ehdr.e_phoff, SEEK_SET);
    for (uint16_t i = 0; i < ehdr.e_phnum; ++i)
    {
        if (fread(&phdr, sizeof(phdr), 1, f) != 1)
            break;

        if (ehdr.e_phentsize > sizeof(phdr))
            fseek(f, ehdr.e_phentsize - sizeof(phdr), SEEK_CUR);

        if (phdr.p_type == PT_LOAD)
        {
            if (phdr.p_vaddr < minAddr)
                minAddr = phdr.p_vaddr;
            if (phdr.p_vaddr + phdr.p_memsz > maxAddr)
                maxAddr = phdr.p_vaddr + phdr.p_memsz;
        }
    }

    fclose(f);

    if (maxAddr <= minAddr)
        return;

    uint32_t alignedBase = static_cast<uint32_t>(minAddr) & ~0xFFFF;
    uint32_t alignedMax = (static_cast<uint32_t>(maxAddr) + 0xFFFF) & ~0xFFFF;
    SIZE_T totalSize = alignedMax - alignedBase;

    MemoryManager::GetInstance().ReserveAddressSpace(totalSize, reinterpret_cast<void *>((uintptr_t)alignedBase));
}

bool ElfLoader::Load(const std::string &path)
{
    if (m_IsSharedObject)
        return LoadMapAndExport(path);
    if (!LoadMapAndExport(path))
        return false;

    if (!SymbolResolver::GetInstance().ProcessAllRelocations())
        return false;

    if (!ProcessRelocations())
        return false;
    SymbolResolver::GetInstance().PatchAllSOs();

    if (!SymbolResolver::GetInstance().RunAllInits())
        return false;
    RegisterAllEhFrames();

    return true;
}

bool ElfLoader::LoadMapAndExport(const std::string &path)
{
    m_Path = path;

    if (!ParseElf(path))
        return false;

    if (!MapSegmentsToMemory())
        return false;

    // Before any symbol export or dependency load, so this ELF's TLS
    // module index is set for its symbols and for the modules it loads
    // (the main executable is always module 1).
    RegisterTlsSegment();

    // The main executable's, before its entry point (Execute): its code is
    // still the file's until the game is identified (the partial CRC).
    if (m_IsSharedObject)
        PatchStackGuardReads();

    if (!ExportSymbols())
        return false;

    if (!ResolveVTables())
        return false;

    if (!LoadDependencies())
        return false;

    return true;
}

void ElfLoader::RegisterTlsSegment()
{
    for (Elf_Half i = 0; i < m_Elfio->segments.size(); ++i)
    {
        segment *pSegment = m_Elfio->segments[i];
        if (pSegment->get_type() != PT_TLS)
            continue;

        Elf64_Addr vaddr = pSegment->get_virtual_address();
        Elf_Xword filesz = pSegment->get_file_size();
        Elf_Xword memsz = pSegment->get_memory_size();
        Elf_Word align = static_cast<Elf_Word>(pSegment->get_align());

        // The mapped .tdata bytes (null for a pure .tbss); for the main
        // executable they are in place (no separate copy is made).
        const uint8_t *image = (filesz > 0) ? reinterpret_cast<const uint8_t *>(vaddr + m_LoadBias) : nullptr;

        m_TlsModid = Tls::RegisterModule(m_Path.c_str(), !m_IsSharedObject, static_cast<uint32_t>(vaddr),
                                         static_cast<uint32_t>(filesz), static_cast<uint32_t>(memsz), align, image);

        if (!m_IsSharedObject && memsz > 0)
            log_warn("TLS: the main ELF has a PT_TLS segment; its local-exec %fs accesses are not emulated (Windows %fs is the TEB)");
        return;
    }

    // The main executable is module 1 even without a TLS segment (its
    // static layout starts at TCB+0x70).
    if (!m_IsSharedObject)
        m_TlsModid = Tls::RegisterModule(m_Path.c_str(), true, 0, 0, 0, 0, nullptr);
}

bool ElfLoader::ParseElf(const std::string &path)
{
    if (!m_Elfio->load(path))
    {
        log_error("Failed to parse ELF file: %s", path.c_str());
        return false;
    }

    if (m_Elfio->get_class() != ELFCLASS32)
    {
        log_error("Only 32-bit ELF files are supported");
        return false;
    }

    if (m_Elfio->get_machine() != EM_386)
    {
        log_error("Only x86 machine type is supported");
        return false;
    }

    log_info("Successfully parsed 32-bit x86 ELF: %s", path.c_str());
    return true;
}

// The stack protector's canary, glibc's %gs:0x14 (the thread's TCB): there
// is no Linux %gs on Windows, so the code reads this instead. One value for
// all threads, as the guard only compares it with its own copy; the low byte
// zero, as glibc's (it stops string overflows).
static uint32_t g_StackGuard = 0x5a3c9e00;

// i386 instructions "op reg, gs:[0x14]": gs prefix, opcode, ModRM with a
// disp32 operand (mod 00, r/m 101), disp32 0x14. mov/add/or/and/sub/xor/cmp.
static bool IsGuardOpcode(uint8_t op)
{
    switch (op)
    {
    case 0x8b: // mov r32, r/m32
    case 0x03: // add
    case 0x0b: // or
    case 0x23: // and
    case 0x2b: // sub
    case 0x33: // xor
    case 0x3b: // cmp r32, r/m32
    case 0x39: // cmp r/m32, r32
        return true;
    }
    return false;
}

// Rewrites the stack protector's %gs:0x14 reads to g_StackGuard's absolute
// address, before the code runs. Each one keeps its length: the segment
// prefix goes, a nop pads the end (jumps land on the instruction's start).
void ElfLoader::PatchStackGuardReads()
{
    const uint32_t guard = (uint32_t)(uintptr_t)&g_StackGuard;
    size_t patched = 0;

    for (const auto &seg : m_Elfio->segments)
    {
        if (seg->get_type() != PT_LOAD || !(seg->get_flags() & PF_X) || seg->get_file_size() < 7)
            continue;
        uint8_t *code = reinterpret_cast<uint8_t *>(seg->get_virtual_address() + m_LoadBias);
        size_t size = (size_t)seg->get_file_size();
        for (size_t i = 0; i + 7 <= size; i++)
        {
            if (code[i] != 0x65)
                continue;
            // mov eax, gs:0x14 (moffs32): a1 <addr>, nop
            if (code[i + 1] == 0xa1 && !memcmp(code + i + 2, "\x14\0\0\0", 4))
            {
                code[i] = 0xa1;
                memcpy(code + i + 1, &guard, 4);
                code[i + 5] = 0x90;
                patched++;
                i += 5;
            }
            else if (IsGuardOpcode(code[i + 1]) && (code[i + 2] & 0xc7) == 0x05 &&
                     !memcmp(code + i + 3, "\x14\0\0\0", 4))
            {
                code[i] = code[i + 1];
                code[i + 1] = code[i + 2];
                memcpy(code + i + 2, &guard, 4);
                code[i + 6] = 0x90;
                patched++;
                i += 6;
            }
        }
    }
    if (patched)
        log_info("Stack protector: %zu %%gs:0x14 reads redirected in %s", patched, m_Path.c_str());
}

bool ElfLoader::MapSegmentsToMemory()
{
    log_info("Mapping segments to memory...");

    Elf_Half segmentsNum = m_Elfio->segments.size();
    if (segmentsNum == 0)
    {
        log_error("No segments found in the ELF file");
        return false;
    }

    Elf64_Addr minLoadAddr = (Elf64_Addr)-1;
    Elf64_Addr maxLoadAddr = 0;

    for (Elf_Half i = 0; i < segmentsNum; ++i)
    {
        segment *pSegment = m_Elfio->segments[i];
        if (pSegment->get_type() == PT_LOAD)
        {
            Elf64_Addr vaddr = pSegment->get_virtual_address();
            Elf_Xword memsize = pSegment->get_memory_size();

            if (vaddr < minLoadAddr)
                minLoadAddr = vaddr;
            if (vaddr + memsize > maxLoadAddr)
                maxLoadAddr = vaddr + memsize;
        }
    }

    if (maxLoadAddr <= minLoadAddr)
    {
        log_error("Invalid load addresses min: 0x%llx max: 0x%llx", minLoadAddr, maxLoadAddr);
        return false;
    }

    Elf64_Addr originalMinLoadAddr = minLoadAddr;

    size_t totalSize = maxLoadAddr - minLoadAddr;
    log_debug("Total memory required for ELF: %zu bytes", totalSize);

    void *requestedAddress = nullptr;
    if (m_Elfio->get_type() == ET_EXEC)
    {
        uint32_t alignedAddr = static_cast<uint32_t>(minLoadAddr) & ~0xFFFF;
        requestedAddress = reinterpret_cast<void *>(alignedAddr);
        size_t offset = minLoadAddr - alignedAddr;
        totalSize += offset;

        minLoadAddr = alignedAddr;

        log_info("ET_EXEC detected. Adjusted allocation address to 64KB boundary: %p, original was 0x%llx", requestedAddress,
                 minLoadAddr + offset);
    }

    m_BaseAddress = MemoryManager::GetInstance().AllocateExecutable(totalSize, requestedAddress);
    if (!m_BaseAddress)
    {
        log_fatal("Failed to allocate %zu bytes for ELF", totalSize);
        return false;
    }

    if (m_Elfio->get_type() == ET_EXEC && m_BaseAddress != requestedAddress)
    {
        log_fatal("ET_EXEC allocation failed at required fixed address %p. Got %p instead.", requestedAddress, m_BaseAddress);
        return false;
    }

    log_debug("Allocated memory at base address: %p", m_BaseAddress);

    m_LoadBias = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(m_BaseAddress)) - static_cast<uint32_t>(minLoadAddr);
    m_ImageBase = reinterpret_cast<void *>(originalMinLoadAddr + m_LoadBias);

    for (Elf_Half i = 0; i < segmentsNum; ++i)
    {
        segment *pSegment = m_Elfio->segments[i];
        if (pSegment->get_type() == PT_LOAD)
        {
            Elf64_Addr vaddr = pSegment->get_virtual_address();
            Elf_Xword filesz = pSegment->get_file_size();
            Elf_Xword memsz = pSegment->get_memory_size();

            uint8_t *dest = reinterpret_cast<uint8_t *>(vaddr + m_LoadBias);

            if (filesz > 0)
            {
                memcpy(dest, pSegment->get_data(), filesz);
            }

            if (memsz > filesz)
            {
                memset(dest + filesz, 0, memsz - filesz);
            }

            log_trace("Mapped PT_LOAD segment to %p, size %zu", dest, (size_t)memsz);
        }
    }
    {
        bool collected = false;
        for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
        {
            section *pSec = m_Elfio->sections[i];
            if (pSec->get_name() == ".eh_frame" && pSec->get_size() > 0)
            {
                void *ehFrameAddr = reinterpret_cast<void *>(pSec->get_address() + m_LoadBias);
                s_PendingEhFrames.push_back(ehFrameAddr);
                log_info("[EH] Collected .eh_frame at %p (size %llu) for %s\n", ehFrameAddr, (unsigned long long)pSec->get_size(), m_Path.c_str());
                collected = true;
                break;
            }
        }
        // Without section headers (the Raw Thrills dumps), .eh_frame is
        // found as ld.so finds it: PT_GNU_EH_FRAME's .eh_frame_hdr holds its
        // address (eh_frame_ptr, usually DW_EH_PE_pcrel | DW_EH_PE_sdata4).
        // Without it, no C++ exception unwinds through the game's frames.
        for (const auto &seg : m_Elfio->segments)
        {
            if (collected || seg->get_type() != 0x6474e550 /* PT_GNU_EH_FRAME */ || seg->get_memory_size() < 8)
                continue;
            const uint8_t *hdr = reinterpret_cast<const uint8_t *>(seg->get_virtual_address() + m_LoadBias);
            int32_t value;
            memcpy(&value, hdr + 4, 4);
            uintptr_t ehFrame = 0;
            if (hdr[0] == 1 && hdr[1] == 0x1b) // pcrel | sdata4
                ehFrame = (uintptr_t)(hdr + 4) + value;
            else if (hdr[0] == 1 && (hdr[1] == 0x03 || hdr[1] == 0x0b)) // absptr udata4 / sdata4
                ehFrame = (uintptr_t)(uint32_t)value + ((m_Elfio->get_type() == ET_DYN) ? m_LoadBias : 0);
            if (!ehFrame || !ImageAddressValid((uint32_t)ehFrame))
            {
                log_warn("[EH] PT_GNU_EH_FRAME of %s: unsupported eh_frame_ptr encoding %02x", m_Path.c_str(), hdr[1]);
                continue;
            }
            s_PendingEhFrames.push_back(reinterpret_cast<void *>(ehFrame));
            log_info("[EH] Collected .eh_frame at %p (from PT_GNU_EH_FRAME) for %s\n", (void *)ehFrame, m_Path.c_str());
        }
    }

    return true;
}

bool ElfLoader::LoadDependencies()
{
    log_info("Loading dependencies...");
    static bool librariesRegistered = false;
    if (!librariesRegistered)
    {
        librariesRegistered = true;

        SymbolResolver::GetInstance().RegisterLibrary("libopenal.so.0", "openal32.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libopenal.so.1", "openal32.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libCg.so", "Cg.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libCgGL.so", "CgGL.dll");

        SymbolResolver::GetInstance().RegisterLibrary("libz.so.1", "zlib1.dll");

        SymbolResolver::GetInstance().RegisterLibrary("libc.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libc.so.6", "INTERNAL");

        SymbolResolver::GetInstance().RegisterLibrary("libpthread.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libpthread.so.0", "INTERNAL");

        SymbolResolver::GetInstance().RegisterLibrary("libgcc.so", "libgcc_s_dw2-1.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libgcc_s.so.1", "libgcc_s_dw2-1.dll");

        SymbolResolver::GetInstance().RegisterLibrary("libkswapapi.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libsegaapi.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("ld-linux.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("ld-linux.so.2", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libasound.so.2", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXxf86vm.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libX11.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXext.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXmu.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXi.so.6", "INTERNAL");
        // A linked-in SDL 1.2 dlopens the X extensions; a dump's own copies
        // (Big Buck HD Wild v08.40's lib/) need libxcb, which is not there.
        SymbolResolver::GetInstance().RegisterLibrary("libXrender.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXrandr.so.2", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXcursor.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXfixes.so.3", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libGL.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libGLU.so.1", "INTERNAL");
        // The Raw Thrills g5 games': librt's clocks are the libc bridge's,
        // udev the cabinet hotplug stand-in (rtBridge.cpp).
        SymbolResolver::GetInstance().RegisterLibrary("librt.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libudev.so.1", "INTERNAL");

        // The Roxor games' (Tux Racer's) window, sound and scripts: the
        // Windows SDL (the compatibility layer over SDL2/SDL3, and the SDL
        // 1.2 mixer) as DLLs, Tcl and libpng as the game's own Linux
        // libraries (found in its lib/ directory), the X libraries' symbols
        // the game itself does not call (its SDL does, from the DLLs).
        SymbolResolver::GetInstance().RegisterLibrary("libSDL-1.2.so.0", "SDL.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libSDL_mixer-1.2.so.0", "SDL_mixer.dll");
        SymbolResolver::GetInstance().RegisterLibrary("libSM.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libICE.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libXt.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libglut.so.3", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libm.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libm.so.6", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libdl.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libdl.so.2", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libimf.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libirc.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libsvml.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libalpb.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libbdlog.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libsama.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libposixtime.so", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libposixtime.so.1", "INTERNAL");
        SymbolResolver::GetInstance().RegisterLibrary("libposixtime.so.2.4", "INTERNAL");
    }

    bool haveDynamic = false;
    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        section *pSection = m_Elfio->sections[i];
        if (pSection->get_type() == SHT_DYNAMIC)
        {
            haveDynamic = true;
            dynamic_section_accessor dynamic(*m_Elfio, pSection);
            Elf_Xword dyn_num = dynamic.get_entries_num();
            for (Elf_Xword j = 0; j < dyn_num; ++j)
            {
                Elf_Xword tag;
                Elf_Xword value;
                std::string str;
                dynamic.get_entry(j, tag, value, str);

                if (tag == DT_NEEDED)
                {
                    log_info("ELF needs library: %s", str.c_str());
                    SymbolResolver::GetInstance().LoadNeededLibrary(str);
                }
            }
        }
    }
    if (!haveDynamic)
        LoadImageDependencies();
    return true;
}

// The DT_NEEDED entries of an ELF without section headers (the Raw Thrills
// dumps: the envelope stripped them), read from the loaded PT_DYNAMIC as
// ld.so does.
void ElfLoader::LoadImageDependencies()
{
    if (m_Elfio->get_class() != ELFCLASS32)
        return;
    const Elf32_Dyn *dyn = (const Elf32_Dyn *)(uintptr_t)GetDynamicSectionAddress();
    if (!dyn || !ImageAddressValid((uint32_t)(uintptr_t)dyn))
        return;

    uint32_t strtab = 0, strsz = 0;
    for (const Elf32_Dyn *e = dyn; e - dyn < 256 && e->d_tag != DT_NULL; e++)
    {
        if (e->d_tag == DT_STRTAB)
            strtab = e->d_un.d_val;
        else if (e->d_tag == DT_STRSZ)
            strsz = e->d_un.d_val;
    }
    // A dump's table holds the relocated address; a file's, the unbiased one.
    if (strtab && !ImageAddressValid(strtab) && ImageAddressValid(strtab + m_LoadBias))
        strtab += m_LoadBias;
    if (!strtab || !ImageAddressValid(strtab))
    {
        log_warn("ELF has no readable dynamic string table: its DT_NEEDED libraries are not loaded");
        return;
    }
    for (const Elf32_Dyn *e = dyn; e - dyn < 256 && e->d_tag != DT_NULL; e++)
    {
        if (e->d_tag != DT_NEEDED || (strsz && e->d_un.d_val >= strsz))
            continue;
        const char *name = (const char *)(uintptr_t)(strtab + e->d_un.d_val);
        log_info("ELF needs library: %s", name);
        SymbolResolver::GetInstance().LoadNeededLibrary(name);
    }
}

#ifndef DT_RELRSZ
#define DT_RELRSZ 35
#endif
#ifndef DT_RELR
#define DT_RELR   36
#endif

#define R_386_32        1
#define R_386_PC32      2
#define R_386_COPY      5
#define R_386_GLOB_DAT  6
#define R_386_JMP_SLOT  7
#define R_386_RELATIVE  8

// The relocations of an ELF without section headers (the Raw Thrills dumps),
// from the loaded PT_DYNAMIC's DT_REL and DT_JMPREL tables, as ld.so reads
// them. A dump's slots already hold the cabinet's resolved values, so only
// the relocations that do not add to them are applied: the copies of the
// libraries' objects (R_386_COPY: libstdc++'s type_info vtables, without
// which no C++ exception is caught) and the GOT slots.
void ElfLoader::ProcessImageRelocations()
{
    if (m_Elfio->get_class() != ELFCLASS32)
        return;
    const Elf32_Dyn *dyn = (const Elf32_Dyn *)(uintptr_t)GetDynamicSectionAddress();
    if (!dyn || !ImageAddressValid((uint32_t)(uintptr_t)dyn))
        return;

    uint32_t rel = 0, relSize = 0, jmpRel = 0, jmpRelSize = 0, symtab = 0, strtab = 0;
    for (const Elf32_Dyn *e = dyn; e - dyn < 256 && e->d_tag != DT_NULL; e++)
    {
        switch (e->d_tag)
        {
        case DT_REL: rel = e->d_un.d_ptr; break;
        case DT_RELSZ: relSize = e->d_un.d_val; break;
        case DT_JMPREL: jmpRel = e->d_un.d_ptr; break;
        case DT_PLTRELSZ: jmpRelSize = e->d_un.d_val; break;
        case DT_SYMTAB: symtab = e->d_un.d_ptr; break;
        case DT_STRTAB: strtab = e->d_un.d_ptr; break;
        }
    }
    // A dump's table holds the relocated addresses; a file's, the unbiased ones.
    uint32_t *ptrs[] = {&rel, &jmpRel, &symtab, &strtab};
    for (uint32_t *p : ptrs)
        if (*p && !ImageAddressValid(*p) && ImageAddressValid(*p + m_LoadBias))
            *p += m_LoadBias;
    if (!symtab || !strtab || !ImageAddressValid(symtab) || !ImageAddressValid(strtab))
    {
        log_warn("ELF has no readable dynamic symbol table: its relocations are not applied");
        return;
    }

    const Elf32_Sym *syms = (const Elf32_Sym *)(uintptr_t)symtab;
    const char *strs = (const char *)(uintptr_t)strtab;
    size_t applied = 0, skipped = 0;
    struct
    {
        uint32_t table, size;
    } tables[] = {{rel, relSize}, {jmpRel, jmpRelSize}};
    for (const auto &t : tables)
    {
        if (!t.table || !ImageAddressValid(t.table))
            continue;
        const Elf32_Rel *r = (const Elf32_Rel *)(uintptr_t)t.table;
        for (uint32_t i = 0; i < t.size / sizeof(Elf32_Rel); i++)
        {
            uint32_t type = ELF32_R_TYPE(r[i].r_info), symIdx = ELF32_R_SYM(r[i].r_info);
            uint32_t *slot = (uint32_t *)(uintptr_t)(r[i].r_offset + m_LoadBias);
            if (!symIdx || !ImageAddressValid((uint32_t)(uintptr_t)slot) ||
                (type != R_386_COPY && type != R_386_GLOB_DAT && type != R_386_JMP_SLOT))
            {
                skipped++;
                continue;
            }
            std::string name = strs + syms[symIdx].st_name;
            if (type == R_386_COPY)
            {
                void *src = SymbolResolver::GetInstance().ResolveSymbolInSharedLibs(name);
                if (src && src != slot && syms[symIdx].st_size)
                {
                    memcpy(slot, src, syms[symIdx].st_size);
                    SymbolResolver::GetInstance().RegisterVTable(name, slot);
                    log_info("Applied R_386_COPY for %s (%u bytes) from %p to %p", name.c_str(), syms[symIdx].st_size, src, slot);
                    applied++;
                }
                else
                    log_warn("R_386_COPY for %s: no source in the shared libraries", name.c_str());
                continue;
            }
            std::string moduleName;
            void *value = SymbolResolver::GetInstance().ResolveSymbol(name, &moduleName);
            if (!value)
                continue;
            *slot = (uint32_t)(uintptr_t)value;
            if (type == R_386_JMP_SLOT && !m_IsSharedObject)
                SymbolResolver::GetInstance().RecordMainImport(name, slot);
            applied++;
        }
    }
    log_info("Applied %zu relocations from PT_DYNAMIC (%zu with addends left as dumped)", applied, skipped);
}

bool ElfLoader::ProcessRelocations()
{
    if (m_Relocated)
        return true;
    m_Relocated = true;

    log_info("Processing relocations...");

    Elf64_Addr relrAddr = 0;
    Elf_Xword relrSz = 0;
    std::unordered_set<uintptr_t> relrPatchedAddrs;

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        section *pSection = m_Elfio->sections[i];
        if (pSection->get_type() == SHT_DYNAMIC)
        {
            dynamic_section_accessor dynamic(*m_Elfio, pSection);
            for (Elf_Xword j = 0; j < dynamic.get_entries_num(); ++j)
            {
                Elf_Xword tag;
                Elf_Xword value;
                std::string str;
                dynamic.get_entry(j, tag, value, str);
                if (tag == DT_RELR)
                    relrAddr = value;
                if (tag == DT_RELRSZ)
                    relrSz = value;
            }
        }
    }

    if (relrAddr != 0 && relrSz > 0)
    {
        uint8_t *actualRelr = reinterpret_cast<uint8_t *>(relrAddr + m_LoadBias);
        uint32_t *relrData = reinterpret_cast<uint32_t *>(actualRelr);
        size_t count = relrSz / sizeof(uint32_t);

        uint32_t base = 0;
        for (size_t i = 0; i < count; ++i)
        {
            uint32_t entry = relrData[i];
            if ((entry & 1) == 0)
            {
                uint32_t *patchAddr = reinterpret_cast<uint32_t *>(entry + m_LoadBias);
                *patchAddr += m_LoadBias;
                relrPatchedAddrs.insert(reinterpret_cast<uintptr_t>(patchAddr));
                base = entry + sizeof(uint32_t);
            }
            else
            {
                uint32_t bitmap = entry >> 1;
                uint32_t offset = base;
                while (bitmap != 0)
                {
                    if (bitmap & 1)
                    {
                        uint32_t *patchAddr = reinterpret_cast<uint32_t *>(offset + m_LoadBias);
                        *patchAddr += m_LoadBias;
                        relrPatchedAddrs.insert(reinterpret_cast<uintptr_t>(patchAddr));
                    }
                    offset += sizeof(uint32_t);
                    bitmap >>= 1;
                }
                base += 31 * sizeof(uint32_t);
            }
        }
        log_info("Processed %zu bytes of DT_RELR packed relative relocations (%zu addresses)", (size_t)relrSz, relrPatchedAddrs.size());
    }

    bool haveRelSections = false;
    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
        if (m_Elfio->sections[i]->get_type() == SHT_REL || m_Elfio->sections[i]->get_type() == SHT_RELA)
            haveRelSections = true;
    if (!haveRelSections)
        ProcessImageRelocations();

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        section *pSection = m_Elfio->sections[i];
        if (pSection->get_type() == SHT_REL || pSection->get_type() == SHT_RELA)
        {
            log_debug("Found relocation section: %s", pSection->get_name().c_str());

            relocation_section_accessor rel(*m_Elfio, pSection);
            log_debug("  Entries: %d", rel.get_entries_num());
            for (Elf_Xword j = 0; j < rel.get_entries_num(); ++j)
            {
                Elf64_Addr offset;
                Elf_Word symbolIdx;
                Elf_Word type;
                Elf_Sxword addend;

                if (rel.get_entry(j, offset, symbolIdx, type, addend))
                {
                    std::string symbolName = "";
                    Elf_Xword symSize = 0;
                    section *symSec = m_Elfio->sections[pSection->get_link()];
                    if (symSec)
                    {
                        symbol_section_accessor symbols(*m_Elfio, symSec);
                        Elf64_Addr value;
                        unsigned char bind;
                        unsigned char symcat;
                        Elf_Half shndx;
                        unsigned char other;
                        symbols.get_symbol(symbolIdx, symbolName, value, symSize, bind, symcat, shndx, other);
                    }

                    uint8_t *targetAddr = reinterpret_cast<uint8_t *>(offset + m_LoadBias);
                    uint32_t *patchAddr = reinterpret_cast<uint32_t *>(targetAddr);
                    if (type == R_386_RELATIVE)
                    {
                        if (relrPatchedAddrs.count(reinterpret_cast<uintptr_t>(patchAddr)))
                        {
                            log_trace("Skipping R_386_RELATIVE at %p (already handled by DT_RELR)", patchAddr);
                            continue;
                        }
                        *patchAddr += m_LoadBias;
                        log_trace("Applied R_386_RELATIVE at %p (offset: 0x%llx), new val: 0x%08X", patchAddr, offset, *patchAddr);
                        continue;
                    }

                    // i386 TLS relocations: the values are DTV module
                    // indices / offsets within a TLS block, not addresses,
                    // so they must not go through the generic resolution
                    // below (glibc formulas, sysdeps/i386/dl-machine.h).
                    if (type == R_386_TLS_DTPMOD32 || type == R_386_TLS_DTPOFF32 || type == R_386_TLS_TPOFF || type == R_386_TLS_TPOFF32)
                    {
                        if (type == R_386_TLS_DTPMOD32)
                        {
                            // Anonymous (symbol index 0): the module is this
                            // object; a named symbol's module is where it is
                            // defined.
                            uint32_t modid = (symbolIdx == 0) ? m_TlsModid : Tls::FindSymbolModule(symbolName.c_str());
                            *patchAddr = modid;
                            log_info("TLS reloc R_386_TLS_DTPMOD32 for %s at %p (module %u)", symbolName.c_str(), patchAddr, modid);
                        }
                        else if (type == R_386_TLS_DTPOFF32)
                        {
                            // S + A: the symbol's offset within its module's
                            // TLS block, plus the addend.
                            uint32_t off = Tls::FindSymbolTlsOffset(symbolName.c_str()) + static_cast<uint32_t>(addend);
                            *patchAddr = off;
                            log_info("TLS reloc R_386_TLS_DTPOFF32 for %s at %p (offset 0x%x)", symbolName.c_str(), patchAddr, off);
                        }
                        else
                        {
                            // %fs-from-TCB offsets: best-effort, since Windows
                            // %fs is the TEB and never the Linux TCB.
                            uint32_t modid = Tls::FindSymbolModule(symbolName.c_str());
                            uint32_t symOff = Tls::FindSymbolTlsOffset(symbolName.c_str());
                            uint32_t modOff = Tls::GetModuleTlsOffset(modid);
                            *patchAddr += (type == R_386_TLS_TPOFF) ? (symOff - modOff) : (modOff - symOff);
                            log_info("TLS reloc R_386_TLS_TPOFF for %s at %p (best-effort, %fs is the TEB)", symbolName.c_str(), patchAddr);
                        }
                        continue;
                    }

                    if (!symbolName.empty())
                    {
                        std::string moduleName;
                        void *resolvedFunc = SymbolResolver::GetInstance().ResolveSymbol(symbolName, &moduleName);

                        if (resolvedFunc)
                        {
                            if (moduleName == "UNRESOLVED_STUB")
                            {
                                log_info("Symbol \"%s\" was not found, assigned crash stub at %p", symbolName.c_str(), resolvedFunc);
                            }
                            else if (moduleName == "WEAK_SYMBOL")
                            {
                                log_info("Symbol \"%s\" was resolved to NULL (Not Implemented/Required)", symbolName.c_str());
                            }
                            else
                            {
                                log_info("Symbol \"%s\" was resolved in \"%s\"", symbolName.c_str(), moduleName.c_str());
                            }
                            uint32_t symValue = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(resolvedFunc));

                            uint32_t inlineAddend = (pSection->get_type() == SHT_REL) ? *patchAddr : addend;
                            switch (type)
                            {
                                case R_386_32:
                                    *patchAddr = symValue + inlineAddend;
                                    log_trace("Applied R_386_32 for %s at %p (addend=%d -> val=0x%x)", symbolName.c_str(), patchAddr,
                                              inlineAddend, *patchAddr);
                                    break;
                                case R_386_PC32:
                                {
                                    *patchAddr = symValue + inlineAddend - static_cast<uint32_t>(reinterpret_cast<uintptr_t>(patchAddr));
                                    log_trace("Applied R_386_PC32 for %s at %p (offset: 0x%x)", symbolName.c_str(), patchAddr, *patchAddr);
                                    break;
                                }
                                case R_386_COPY:
                                {
                                    if (symSize > 0)
                                    {
                                        void *srcAddr = m_IsSharedObject
                                                            ? resolvedFunc
                                                            : SymbolResolver::GetInstance().ResolveSymbolInSharedLibs(symbolName);

                                        if (srcAddr && srcAddr != patchAddr)
                                        {
                                            memcpy(patchAddr, srcAddr, symSize);
                                            log_info("Applied R_386_COPY for %s (%llu bytes) from %p to %p", symbolName.c_str(), symSize,
                                                     srcAddr, patchAddr);
                                            SymbolResolver::GetInstance().RegisterVTable(symbolName, patchAddr);
                                        }
                                        else
                                        {
                                            log_info("R_386_COPY for %s: source not found in shared libs (srcAddr=%p)", symbolName.c_str(),
                                                     srcAddr);
                                        }
                                    }
                                    else
                                    {
                                        log_info("R_386_COPY for %s has 0 size", symbolName.c_str());
                                    }
                                    break;
                                }
                                case R_386_GLOB_DAT:
                                case R_386_JMP_SLOT:
                                    *patchAddr = symValue;
                                    log_trace("Applied R_386_GLOB_DAT / R_386_JMP_SLOT for %s at %p", symbolName.c_str(), patchAddr);
                                    if (type == R_386_JMP_SLOT && !m_IsSharedObject)
                                        SymbolResolver::GetInstance().RecordMainImport(symbolName, patchAddr);
                                    break;
                                default:
                                    log_info("Unhandled relocation type %lu for symbol %s", type, symbolName.c_str());
                                    break;
                            }
                        }
                        else
                        {
                            log_error("Failed to resolve symbol %s and failed to create stub", symbolName.c_str());
                        }
                    }
                    else
                    {
                        log_info("Unhandled anonymous relocation type %lu at %p", type, patchAddr);
                    }
                }
            }
        }
    }
    return true;
}

bool ElfLoader::ExportSymbols()
{
    log_info("Exporting dynamic symbols for resolution...");

    section *dynSymSec = nullptr;
    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        if (m_Elfio->sections[i]->get_type() == SHT_DYNSYM)
        {
            dynSymSec = m_Elfio->sections[i];
            break;
        }
    }

    if (!dynSymSec)
    {
        log_info("No SHT_DYNSYM section found - nothing to export");
        return true;
    }

    symbol_section_accessor symbols(*m_Elfio, dynSymSec);

    for (Elf_Xword k = 0; k < symbols.get_symbols_num(); ++k)
    {
        std::string symbolName = "";
        Elf64_Addr value;
        Elf_Xword size;
        unsigned char bind;
        unsigned char type;
        Elf_Half shndx;
        unsigned char other;

        if (symbols.get_symbol(k, symbolName, value, size, bind, type, shndx, other))
        {
            // TLS variables (STT_TLS): their st_value is the offset
            // within this module's TLS block, not an address, so they
            // must not be registered as native symbols. The module index
            // and the offset feed the GD/LD pairs (see tlsSupport).
            if (type == STT_TLS && shndx != 0 && !symbolName.empty())
                Tls::RegisterSymbol(symbolName.c_str(), m_TlsModid, static_cast<uint32_t>(value));

            if (value != 0 && shndx != 0 && (bind == STB_GLOBAL || bind == STB_WEAK || bind == 10) &&
                (type == STT_FUNC || type == STT_OBJECT || type == 10))
            {
                void *symbolAddr = reinterpret_cast<void *>(value + m_LoadBias);
                SymbolResolver::GetInstance().RegisterNativeSymbol(symbolName, symbolAddr);

                size_t atPos = symbolName.find('@');
                if (atPos != std::string::npos)
                {
                    std::string baseName = symbolName.substr(0, atPos);
                    SymbolResolver::GetInstance().RegisterNativeSymbol(baseName, symbolAddr);
                }
            }
        }
    }

    log_info("Exporting static symbols for findStaticFnAddr...");
    section *symSec = nullptr;
    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        if (m_Elfio->sections[i]->get_type() == SHT_SYMTAB)
        {
            symSec = m_Elfio->sections[i];
            break;
        }
    }

    if (symSec)
    {
        symbol_section_accessor staticSymbols(*m_Elfio, symSec);
        for (Elf_Xword k = 0; k < staticSymbols.get_symbols_num(); ++k)
        {
            std::string symbolName = "";
            Elf64_Addr value;
            Elf_Xword size;
            unsigned char bind;
            unsigned char type;
            Elf_Half shndx;
            unsigned char other;
            if (staticSymbols.get_symbol(k, symbolName, value, size, bind, type, shndx, other))
            {
                if (value != 0 && shndx != 0 && type == STT_FUNC)
                {
                    void *symbolAddr = reinterpret_cast<void *>(value + m_LoadBias);
                    SymbolResolver::GetInstance().RegisterNativeSymbol(symbolName, symbolAddr);
                }
            }
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// The loaded image's own dynamic symbol table.
//
// Dumps taken from running cabinets can have no section headers at all
// (the envelope stripped them), so the file's .dynsym section is
// unreadable; the only symbol table that remains is the one ld.so itself
// binds through: .dynamic's DT_SYMTAB / DT_STRTAB (Linux's dlsym finds the
// game's exported functions that way). The table's addresses are read from
// the loaded, relocated image, not from the file.
// ---------------------------------------------------------------------------

// A runtime address inside one of the loaded PT_LOAD segments' file-backed
// ranges: keeps a wild .dynamic pointer from being dereferenced.
bool ElfLoader::ImageAddressValid(uint32_t address) const
{
    for (const auto &seg : m_Elfio->segments)
    {
        if (seg->get_type() != PT_LOAD || seg->get_file_size() == 0)
            continue;
        uint32_t start = (uint32_t)(seg->get_virtual_address() + m_LoadBias);
        if (address >= start && address < start + (uint32_t)seg->get_file_size())
            return true;
    }
    return false;
}

void ElfLoader::ReadImageDynsym() const
{
    if (m_DynsymRead)
        return;
    m_DynsymRead = true;
    if (!m_Elfio || m_Elfio->get_class() != ELFCLASS32)
        return;

    const Elf32_Dyn *dyn = (const Elf32_Dyn *)(uintptr_t)GetDynamicSectionAddress();
    if (!dyn || !ImageAddressValid((uint32_t)(uintptr_t)dyn))
    {
        log_warn("symbol lookup: the image has no readable .dynamic table, name lookups will miss");
        return;
    }

    const uint32_t *hash = nullptr;
    for (const Elf32_Dyn *e = dyn; e - dyn < 128 && e->d_tag != DT_NULL; e++)
    {
        switch (e->d_tag)
        {
        case DT_SYMTAB:
            m_DynsymTable = e->d_un.d_val;
            break;
        case DT_STRTAB:
            m_DynsymStrings = e->d_un.d_val;
            break;
        case DT_STRSZ:
            m_DynsymStrsz = e->d_un.d_val;
            break;
        case DT_HASH:
            hash = (const uint32_t *)(uintptr_t)e->d_un.d_val;
            break;
        }
    }
    // A dump's .dynamic holds the relocated addresses (ld.so wrote them); a
    // library file's, the unbiased ones.
    if (m_DynsymTable && !ImageAddressValid(m_DynsymTable) && ImageAddressValid(m_DynsymTable + m_LoadBias))
        m_DynsymTable += m_LoadBias;
    if (m_DynsymStrings && !ImageAddressValid(m_DynsymStrings) && ImageAddressValid(m_DynsymStrings + m_LoadBias))
        m_DynsymStrings += m_LoadBias;
    if (hash && !ImageAddressValid((uint32_t)(uintptr_t)hash) && ImageAddressValid((uint32_t)(uintptr_t)hash + m_LoadBias))
        hash = (const uint32_t *)((uintptr_t)hash + m_LoadBias);
    if (!m_DynsymTable || !m_DynsymStrings || !ImageAddressValid(m_DynsymTable) || !ImageAddressValid(m_DynsymStrings))
    {
        log_warn("symbol lookup: the image's .dynamic points at no usable symbol table (symtab=%08x strtab=%08x)",
                 (unsigned)m_DynsymTable, (unsigned)m_DynsymStrings);
        m_DynsymTable = m_DynsymStrings = 0;
        return;
    }
    // The entry count: .hash's nchain (glibc's sysv layout). Without it the
    // walker is bounded by the string table's size instead.
    if (hash && ImageAddressValid((uint32_t)(uintptr_t)hash))
        m_DynsymCount = hash[1];
    log_info("symbol lookup: the image's dynamic symbol table has %u entries at %08x",
             (unsigned)(m_DynsymCount ? m_DynsymCount : 0), (unsigned)m_DynsymTable);
}

// One entry of the image's dynamic symbol table by name (a versioned name
// like "foo@GLIBC_2.0" matches the unversioned one, as dlsym does), or
// nullptr: the raw table, for FindExportedSymbol and GetSymbolSize.
static const Elf32_Sym *FindImageDynsym(uint32_t symtab, uint32_t strtab, uint32_t strsz, uint32_t count, const char *name)
{
    const Elf32_Sym *sym = (const Elf32_Sym *)(uintptr_t)symtab;
    const char *str = (const char *)(uintptr_t)strtab;
    size_t nameLen = strlen(name);

    if (!count)
        count = strsz + 1; // every entry's name takes at least one byte
    for (uint32_t i = 1; i < count; i++)
    {
        const Elf32_Sym *s = &sym[i];
        if (s->st_name == 0 && s->st_value == 0)
            break; // null-terminated table
        if (strsz && s->st_name >= strsz)
            break; // ran off the string table: the table is truncated
        const char *n = str + s->st_name;
        const char *at = strchr(n, '@');
        size_t len = at ? (size_t)(at - n) : strlen(n);
        if (len == nameLen && strncmp(n, name, nameLen) == 0)
            return s;
    }
    return nullptr;
}

void *ElfLoader::FindExportedSymbol(const std::string &name) const
{
    if (!m_Elfio)
        return nullptr;

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        if (m_Elfio->sections[i]->get_type() == SHT_DYNSYM)
        {
            symbol_section_accessor symbols(*m_Elfio, m_Elfio->sections[i]);
            for (Elf_Xword k = 0; k < symbols.get_symbols_num(); ++k)
            {
                std::string symName;
                Elf64_Addr value;
                Elf_Xword size;
                unsigned char bind, type;
                Elf_Half shndx;
                unsigned char other;

                if (symbols.get_symbol(k, symName, value, size, bind, type, shndx, other))
                {
                    if (symName == name && value != 0 && shndx != 0)
                    {
                        return reinterpret_cast<void *>(value + m_LoadBias);
                    }
                }
            }
            break;
        }
    }

    // No section headers (a cabinet dump), or the table the sections point
    // to is unreadable: the same symbols, from the loaded image's .dynamic.
    // A fixed-base dump's values are its runtime addresses; a shared
    // object's take its load bias.
    ReadImageDynsym();
    if (m_DynsymTable)
    {
        const Elf32_Sym *s = FindImageDynsym(m_DynsymTable, m_DynsymStrings, m_DynsymStrsz, m_DynsymCount, name.c_str());
        if (s && s->st_value != 0 && s->st_shndx != 0)
        {
            uint32_t value = s->st_value + (m_Elfio->get_type() == ET_DYN ? m_LoadBias : 0);
            log_debug("FindExportedSymbol: '%s' found in the image's .dynamic at %p", name.c_str(), (void *)(uintptr_t)value);
            return (void *)(uintptr_t)value;
        }
    }
    return nullptr;
}

uint32_t ElfLoader::GetSymbolSize(const std::string &name) const
{
    if (!m_Elfio)
        return 0;

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        if (m_Elfio->sections[i]->get_type() != SHT_DYNSYM)
            continue;
        symbol_section_accessor symbols(*m_Elfio, m_Elfio->sections[i]);
        for (Elf_Xword k = 0; k < symbols.get_symbols_num(); ++k)
        {
            std::string symName;
            Elf64_Addr value;
            Elf_Xword size;
            unsigned char bind, type;
            Elf_Half shndx;
            unsigned char other;

            if (symbols.get_symbol(k, symName, value, size, bind, type, shndx, other) && symName == name &&
                shndx != 0)
                return (uint32_t)size;
        }
        break;
    }

    ReadImageDynsym();
    if (m_DynsymTable)
    {
        const Elf32_Sym *s = FindImageDynsym(m_DynsymTable, m_DynsymStrings, m_DynsymStrsz, m_DynsymCount, name.c_str());
        if (s && s->st_shndx != 0)
            return s->st_size;
    }
    return 0;
}

bool ElfLoader::GetCodeSegment(uint32_t address, uint32_t *start, uint32_t *end) const
{
    if (!m_Elfio)
        return false;

    for (const auto &seg : m_Elfio->segments)
    {
        if (seg->get_type() != PT_LOAD || !(seg->get_flags() & PF_X))
            continue;
        uint32_t segStart = (uint32_t)(seg->get_virtual_address() + m_LoadBias);
        uint32_t segEnd = segStart + (uint32_t)seg->get_file_size();
        if (address >= segStart && address < segEnd)
        {
            *start = segStart;
            *end = segEnd;
            return true;
        }
    }
    return false;
}

bool ElfLoader::ForEachExportedSymbol(
    int (*callback)(const char *name, uint32_t value, uint32_t size, unsigned char type, unsigned shndx, void *arg), void *arg) const
{
    if (!m_Elfio)
        return false;

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        if (m_Elfio->sections[i]->get_type() != SHT_DYNSYM)
            continue;
        symbol_section_accessor symbols(*m_Elfio, m_Elfio->sections[i]);
        for (Elf_Xword k = 0; k < symbols.get_symbols_num(); ++k)
        {
            std::string symName;
            Elf64_Addr value;
            Elf_Xword size;
            unsigned char bind, type;
            Elf_Half shndx;
            unsigned char other;

            if (!symbols.get_symbol(k, symName, value, size, bind, type, shndx, other))
                continue;
            if (!callback(symName.c_str(), (uint32_t)(value + m_LoadBias), (uint32_t)size, type, shndx, arg))
                return false;
        }
        return true;
    }
    return true;
}

bool ElfLoader::GetWritableCodeSegment(uint32_t *start, uint32_t *end) const
{
    if (!m_Elfio)
        return false;

    for (const auto &seg : m_Elfio->segments)
    {
        if (seg->get_type() != PT_LOAD || (seg->get_flags() & (PF_W | PF_X)) != (PF_W | PF_X))
            continue;
        *start = (uint32_t)(seg->get_virtual_address() + m_LoadBias);
        *end = *start + (uint32_t)seg->get_memory_size();
        return true;
    }
    return false;
}

uint32_t ElfLoader::GetDynamicSectionAddress() const
{
    if (!m_Elfio)
        return 0;
    for (const auto &seg : m_Elfio->segments)
        if (seg->get_type() == PT_DYNAMIC)
            return (uint32_t)(seg->get_virtual_address() + m_LoadBias);
    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
        if (m_Elfio->sections[i]->get_type() == SHT_DYNAMIC)
            return (uint32_t)(m_Elfio->sections[i]->get_address() + m_LoadBias);
    return 0;
}

// The loaded game's symbols and code, for the C code that patches it (Raw
// Thrills).
extern "C" void *rtMainSymbol(const char *name)
{
    return g_ElfMain ? g_ElfMain->FindExportedSymbol(name) : nullptr;
}

extern "C" uint32_t rtMainSymbolSize(const char *name)
{
    return g_ElfMain ? g_ElfMain->GetSymbolSize(name) : 0;
}

extern "C" int rtMainCodeSegment(uint32_t address, uint32_t *start, uint32_t *end)
{
    return g_ElfMain && g_ElfMain->GetCodeSegment(address, start, end) ? 1 : 0;
}

extern "C" void *rtMainDynamic(void)
{
    uint32_t dyn = g_ElfMain ? g_ElfMain->GetDynamicSectionAddress() : 0;
    return (void *)(uintptr_t)dyn;
}

// The loaded game's dynamic symbols and writable code, for the C code that
// patches it (the Namco N2 hooks).
extern "C" int rtMainForeachSymbol(
    int (*callback)(const char *name, uint32_t value, uint32_t size, unsigned char type, unsigned shndx, void *arg), void *arg)
{
    return g_ElfMain && g_ElfMain->ForEachExportedSymbol(callback, arg) ? 1 : 0;
}

extern "C" int rtMainWritableCodeSegment(uint32_t *start, uint32_t *end)
{
    return g_ElfMain && g_ElfMain->GetWritableCodeSegment(start, end) ? 1 : 0;
}

bool ElfLoader::RunInit()
{
    // Once: RunAllInits runs again for every library the game dlopens.
    if (m_InitRun)
        return true;
    m_InitRun = true;
    log_info("Running initialization for shared object: %s", m_Path.c_str());

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        section *pSection = m_Elfio->sections[i];
        if (pSection->get_type() == SHT_DYNAMIC)
        {
            dynamic_section_accessor dynamic(*m_Elfio, pSection);
            Elf_Xword dyn_num = dynamic.get_entries_num();

            Elf64_Addr initAddr = 0;
            Elf64_Addr initArrayAddr = 0;
            Elf_Xword initArraySz = 0;

            for (Elf_Xword j = 0; j < dyn_num; ++j)
            {
                Elf_Xword tag;
                Elf_Xword value;
                std::string str;
                dynamic.get_entry(j, tag, value, str);

                if (tag == DT_INIT)
                {
                    initAddr = value;
                }
                else if (tag == DT_INIT_ARRAY)
                {
                    initArrayAddr = value;
                }
                else if (tag == DT_INIT_ARRAYSZ)
                {
                    initArraySz = value;
                }
            }

            if (initAddr != 0)
            {
                void *actualInit = reinterpret_cast<void *>(initAddr + m_LoadBias);
                log_debug("Calling DT_INIT at %p", actualInit);
                typedef void (*InitFunc)(void);
                InitFunc init = reinterpret_cast<InitFunc>(actualInit);
                init();
            }

            if (initArrayAddr != 0 && initArraySz > 0)
            {
                void *actualArray = reinterpret_cast<void *>(initArrayAddr + m_LoadBias);
                log_debug("Calling DT_INIT_ARRAY at %p (size %llu bytes)", actualArray, initArraySz);

                size_t numFuncs = initArraySz / sizeof(uint32_t);
                uint32_t *funcArray = reinterpret_cast<uint32_t *>(actualArray);
                for (size_t k = 0; k < numFuncs; ++k)
                {
                    log_debug("Calling DT_INIT_ARRAY function %zu at %p", k, funcArray[k]);
                    uint32_t funcPtrVal = funcArray[k];
                    if (funcPtrVal == 0 || funcPtrVal == 0xFFFFFFFF)
                        continue;

                    if (funcPtrVal < 0x10000)
                    {
                        log_info("DT_INIT_ARRAY[%zu] has suspiciously low address 0x%08X — skipping (possibly un-relocated)", k, funcPtrVal);
                        continue;
                    }

                    log_debug("Calling DT_INIT_ARRAY function %zu at 0x%08X", k, funcPtrVal);
                    typedef void (*InitArrayFunc)(void);
                    InitArrayFunc func = reinterpret_cast<InitArrayFunc>(reinterpret_cast<uintptr_t>(funcPtrVal));
                    func();
                }
            }
        }
    }

    return true;
}

bool ElfLoader::ResolveVTables()
{
    log_info("Resolving VTables...");

    for (Elf_Half i = 0; i < m_Elfio->sections.size(); ++i)
    {
        section *pSection = m_Elfio->sections[i];
        if (pSection->get_name() == ".rodata" || pSection->get_name() == ".data")
        {
            for (Elf_Half j = 0; j < m_Elfio->sections.size(); ++j)
            {
                if (m_Elfio->sections[j]->get_type() != SHT_SYMTAB && m_Elfio->sections[j]->get_type() != SHT_DYNSYM)
                    continue;

                section *symSec = m_Elfio->sections[j];
                symbol_section_accessor symbols(*m_Elfio, symSec);
                for (Elf_Xword k = 0; k < symbols.get_symbols_num(); ++k)
                {
                    std::string symbolName = "";
                    Elf64_Addr value;
                    Elf_Xword size;
                    unsigned char bind;
                    unsigned char type;
                    Elf_Half shndx;
                    unsigned char other;

                    if (symbols.get_symbol(k, symbolName, value, size, bind, type, shndx, other))
                    {
                        if (shndx == i && (symbolName.rfind("_ZTV", 0) == 0 || symbolName.rfind("_ZTT", 0) == 0))
                        {
                            log_debug("Found VTable/VTT symbol: %s at 0x%llx", symbolName.c_str(), value);

                            void *vtableAddr = reinterpret_cast<void *>(value + m_LoadBias);

                            SymbolResolver::GetInstance().RegisterVTable(symbolName, vtableAddr);
                        }
                    }
                }
            }
        }
    }

    return true;
}

// Where a main() entry point returns (see Execute): exit with its result.
extern "C" void ElfMainReturned(void);
extern "C" void ElfMainExit(int status)
{
    log_info("The game's main() returned %d", status);
    exit(status);
}
__asm__(".globl _ElfMainReturned\n"
        "_ElfMainReturned:\n\t"
        "and $-16, %esp\n\t"
        "sub $12, %esp\n\t"
        "push %eax\n\t"
        "call _ElfMainExit\n");

bool ElfLoader::Execute(int argc, char **argv, char **envp)
{
    if (m_IsSharedObject)
    {
        log_info("Shared Object loaded successfully. Skipping entrypoint execution.");
        return true;
    }

    log_info("Preparing to execute main ELF...");
    PatchStackGuardReads();
    Elf64_Addr entryAddr = m_Elfio->get_entry();
    if (entryAddr == 0)
    {
        log_error("No entry point found - cannot execute");
        return false;
    }

    log_info("Entry point original address: 0x%llx", entryAddr);

    void *actualEntry = reinterpret_cast<void *>(entryAddr + m_LoadBias);

    log_info("Executing Main ELF at mapped address: %p", actualEntry);
    ElfAuxvInfo auxvInfo = {};
    auxvInfo.AtPageSz = 4096;
    auxvInfo.AtEntry = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actualEntry));
    auxvInfo.AtPhent = 32;
    auxvInfo.AtPhnum = static_cast<uint32_t>(m_Elfio->segments.size());

    for (Elf_Half i = 0; i < m_Elfio->segments.size(); ++i)
    {
        if (m_Elfio->segments[i]->get_type() == PT_PHDR)
        {
            auxvInfo.AtPhdr = static_cast<uint32_t>(m_Elfio->segments[i]->get_virtual_address() + m_LoadBias);
            break;
        }
    }
    log_info("Auxv: AT_PHDR=0x%08X AT_PHENT=%u AT_PHNUM=%u AT_PAGESZ=%u AT_ENTRY=0x%08X",
             auxvInfo.AtPhdr, auxvInfo.AtPhent, auxvInfo.AtPhnum, auxvInfo.AtPageSz, auxvInfo.AtEntry);

    const uint32_t STACK_SIZE = s_MainStackSize;
    const uint32_t RESERVE_SIZE = 0x10000;

    uint32_t stackBase = 0;
    uint32_t stackLimit = 0;

    uint32_t esp = LinuxStack::Setup(STACK_SIZE, argc, argv, &stackBase, &stackLimit, RESERVE_SIZE, &auxvInfo);

    __writefsdword(0x04, stackBase);
    __writefsdword(0x08, stackLimit);

    log_info("Stack Alloc: %p - %p", (void *)stackLimit, (void *)stackBase);

    // This thread's Linux TCB/DTV (the game's GD/LD TLS goes through
    // __tls_get_addr, which needs it).
    Tls::EnsureThreadTls();

    typedef void (*EntryPointFunc)(void);
    EntryPointFunc entry = reinterpret_cast<EntryPointFunc>(actualEntry);
    // The Raw Thrills dumps' entry is the game's main() (the envelope's
    // _start is gone): not the i386 _start ("xor %ebp,%ebp"), it is called
    // as main(argc, argv, envp), returning to exit(). ld.so jumps there with
    // the process stack, where main reads argv[0]'s address as argc: a stack
    // address that Linux puts high, so negative, and no argument is parsed.
    const uint8_t *code = reinterpret_cast<const uint8_t *>(actualEntry);
    if (!(code[0] == 0x31 && code[1] == 0xed))
    {
        uint32_t *process = reinterpret_cast<uint32_t *>(esp);
        uint32_t mainArgc = process[0];
        uint32_t *frame = process - 4;
        frame[0] = (uint32_t)(uintptr_t)ElfMainReturned;
        frame[1] = mainArgc;
        frame[2] = (uint32_t)(uintptr_t)(process + 1);
        frame[3] = (uint32_t)(uintptr_t)(process + 1 + mainArgc + 1);
        esp = (uint32_t)(uintptr_t)frame;
        log_info("Entry point is a main(): called with argc %u", mainArgc);
    }
    log_info("Jumping to entry point %p", entry);
    log_info("Stack ESP: 0x%08X", esp);

    Sleep(100);

    __asm__ volatile("push $0x1F80\n\t"
                     "ldmxcsr (%%esp)\n\t"
                     "add $4, %%esp\n\t"
                     :
                     :
                     : "memory");

    __asm__ volatile("mov %0, %%esp\n\t"
                     "xor %%eax, %%eax\n\t"
                     "xor %%ebx, %%ebx\n\t"
                     "xor %%ecx, %%ecx\n\t"
                     "jmp *%1\n\t"
                     :
                     : "r"(esp), "r"(entry)
                     : "eax", "ebx", "ecx", "memory");

    __builtin_unreachable();
}

extern "C" void *GetStaticFunctionAddress(const char *name)
{
    std::string moduleName;
    void *result = SymbolResolver::GetInstance().ResolveSymbol(name, &moduleName);
    if (!result)
    {
        log_error("Failed to resolve symbol: %s\n", name);
        return NULL;
    }
    return result;
}

#endif