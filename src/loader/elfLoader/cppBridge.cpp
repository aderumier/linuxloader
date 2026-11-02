#if defined(_WIN32) || defined(__MINGW32__)
#include "cppBridge.hpp"
#include "symbolResolver.hpp"
#include "../log/log.h"
#include <cstdio>

#define MAP(name, func) SymbolResolver::GetInstance().RegisterVTable(name, reinterpret_cast<void *>(func))

namespace CppBridge
{

    const char *bridgeBadAllocWhat();
    const char *bridgeBadExceptionWhat();

    void initBridges()
    {
        log_info("Initializing C++ Bridges...");

        MAP("_ZdlPv", bridgeOperatorDelete);
        MAP("_Znwj", bridgeOperatorNew);
        MAP("_Znaj", bridgeOperatorNewArray);
        MAP("_ZdaPv", bridgeOperatorDelete);

        // std::exception::what() for the old ABI, where they are exported
        // from libstdc++ rather than inlined.
        MAP("_ZNKSt9bad_alloc4whatEv", bridgeBadAllocWhat);
        MAP("_ZNKSt13bad_exception4whatEv", bridgeBadExceptionWhat);
    }

    const char *bridgeBadAllocWhat()
    {
        return "std::bad_alloc";
    }

    const char *bridgeBadExceptionWhat()
    {
        return "std::bad_exception";
    }

    void *bridgeOperatorNew(size_t size)
    {
        log_trace("Intercepted operator new, size: %zu", size);
        return malloc(size);
    }

    void *bridgeOperatorNewArray(size_t size)
    {
        log_trace("Intercepted operator new[], size: %zu", size);
        return malloc(size);
    }

    void bridgeOperatorDelete(void *ptr)
    {
        log_trace("Intercepted operator delete, ptr: %p", ptr);
        free(ptr);
    }

} // namespace CppBridge

#endif