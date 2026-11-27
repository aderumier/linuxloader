cmake_minimum_required(VERSION 3.10)
project(linuxloader LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/log)
include_directories(${PROJECT_SOURCE_DIR}/src/elfio)
include_directories(${PROJECT_SOURCE_DIR}/src/glad/include)
include_directories(${PROJECT_SOURCE_DIR}/src/minhook/include)
include_directories(${PROJECT_SOURCE_DIR}/src/SDL3)

# ------------------------------------------------------------------------------
# linuxloader64.exe: the 64-bit Linux games (g7) on Windows, its own build
# (cmake -DCMAKE_TOOLCHAIN_FILE=mingw64.cmake), none of the 32-bit loader.
# ------------------------------------------------------------------------------

if((WIN32 OR MINGW OR CMAKE_C_COMPILER MATCHES "mingw") AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(STATUS "Configuring linuxloader64.exe (Windows, x86-64)")
    file(GLOB LOADER64_SRC
        src/loader/win64/*.c
        src/loader/config/iniParser.c
        src/loader/rawthrills/g7/g7Games.c
    )
    add_executable(linuxloader64 ${LOADER64_SRC}
        # Minecraft's card binder, drawn with Dear ImGui (MIT) over the frame
        src/loader/rawthrills/g7/g7McdBinder.cpp
        src/glad/src/gl.c
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_impl_opengl3.cpp)
    set_property(TARGET linuxloader64 PROPERTY C_STANDARD 11)
    target_compile_definitions(linuxloader64 PRIVATE __USE_MINGW_ANSI_STDIO=1 _WIN32_WINNT=0x0A00 WINVER=0x0A00
        IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS OVERLAY_FONT_PATH=${PROJECT_SOURCE_DIR}/src/fonts/cozette.ttf)
    target_include_directories(linuxloader64 PRIVATE ${PROJECT_SOURCE_DIR}/src/glad/include
        ${PROJECT_SOURCE_DIR}/src/imgui ${PROJECT_SOURCE_DIR}/src/stb)
    target_compile_options(linuxloader64 PRIVATE -O2 -fno-strict-aliasing -Wall -Wno-unused-parameter -Wno-unused-function
        -Wno-format-truncation)
    target_compile_options(linuxloader64 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    # The main thread only waits: a small stack, out of the way of the
    # addresses the game's executable takes (see main64.c). The C++ runtime
    # and winpthreads (the binder's) linked in: no MinGW DLL beside the exe.
    target_link_options(linuxloader64 PRIVATE -s -Wl,--stack,1048576 -static)
    target_link_libraries(linuxloader64 bcrypt advapi32 gdi32 opengl32 winmm)

    # Marvel Contest of Champions on Unity's Windows player (rawthrills/mcoc/win):
    # mcoc2.exe, the launcher in place of Unity's WindowsPlayer.exe, and
    # mcocwin.dll, the cabinet's plugins (the Linux ones) and the hooks.
    add_library(mcocwin SHARED
        src/loader/rawthrills/mcoc/win/mcocWin.c
        src/loader/rawthrills/mcoc/win/mcocWinInput.c
        src/loader/rawthrills/mcoc/win/mcocWinStubs.c
        src/loader/rawthrills/mcoc/mcocGame.c
        src/loader/rawthrills/mcoc/mcocPlugins.c
        src/loader/rawthrills/mcoc/mcocBinder.cpp
        src/loader/rawthrills/unity/unityRio2.c
        src/loader/rawthrills/unity/unityMono.c
        src/loader/config/iniParser.c
        src/glad/src/gl.c
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_impl_opengl3.cpp
        src/minhook/src/buffer.c
        src/minhook/src/hook.c
        src/minhook/src/trampoline.c
        src/minhook/src/hde/hde64.c)
    set_target_properties(mcocwin PROPERTIES PREFIX "" OUTPUT_NAME "mcocwin")
    target_compile_definitions(mcocwin PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00 IMGUI_DISABLE_DEMO_WINDOWS
        IMGUI_DISABLE_DEBUG_TOOLS OVERLAY_FONT_PATH=${PROJECT_SOURCE_DIR}/src/fonts/cozette.ttf)
    target_include_directories(mcocwin PRIVATE ${PROJECT_SOURCE_DIR}/src/glad/include ${PROJECT_SOURCE_DIR}/src/imgui
        ${PROJECT_SOURCE_DIR}/src/stb ${PROJECT_SOURCE_DIR}/src/minhook/include)
    target_compile_options(mcocwin PRIVATE -O2 -Wall -Wno-unused-parameter -Wno-unused-function -Wno-format-truncation)
    target_compile_options(mcocwin PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    # Every plugin entry point exported (Mono looks them up by name), the C++
    # runtime and winpthreads linked in.
    target_link_options(mcocwin PRIVATE -s -static -Wl,--export-all-symbols)
    # SDL 3.4.20's gamepads (libs/win64/libSDL3.a, built from its release with
    # mingw64.cmake: static, no audio, render, GPU nor camera).
    target_link_libraries(mcocwin ${PROJECT_SOURCE_DIR}/libs/win64/libSDL3.a opengl32 gdi32 user32 shell32 ole32
        oleaut32 uuid ws2_32 setupapi hid imm32 version winmm cfgmgr32 advapi32)

    add_executable(mcoc2 WIN32 src/loader/rawthrills/mcoc/win/mcocLauncher.c)
    target_compile_options(mcoc2 PRIVATE -O2 -Wall -municode)
    target_link_options(mcoc2 PRIVATE -s -static -municode)
    return()
endif()

# ------------------------------------------------------------------------------
# Common Static Libraries (Built for whichever platform CMake is targeting)
# ------------------------------------------------------------------------------

# 1. MinHook (Static)
file(GLOB_RECURSE MINHOOK_SRC src/minhook/src/*.c)
set(MINHOOK_C_FLAGS -fPIC -m32)
add_library(minhook STATIC ${MINHOOK_SRC})
target_include_directories(minhook PUBLIC src/minhook/include)
target_compile_options(minhook PRIVATE ${MINHOOK_C_FLAGS})

# 2. LibXdiff (Static)
set(LIBXDIFF_C_FLAGS -DHAVE_CONFIG_H -fPIC -m32)
file(GLOB_RECURSE XDIFF_SRC src/libxdiff/xdiff/*.c)
add_library(xdiff STATIC ${XDIFF_SRC})
target_include_directories(xdiff PUBLIC src/libxdiff/xdiff)
target_compile_options(xdiff PRIVATE ${LIBXDIFF_C_FLAGS})

# ------------------------------------------------------------------------------
# Platform-Specific Projects
# ------------------------------------------------------------------------------

if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND NOT MINGW AND NOT CMAKE_C_COMPILER MATCHES "mingw")
    message(STATUS "Configuring Project 1: Linux Build")

    # The linuxloader executable
    set(LINUXLOADER_EXE_C_FLAGS -m32 -DCOMPILING_LINUXLOADER_ELF)
    add_executable(linuxloader_exe
        src/loader/linuxExe.c
        src/loader/loader64/game64.c
        src/loader/mainShared.c
        src/loader/config/iniParser.c
        src/loader/input/sdlInput.c
        src/loader/input/wheelSetup.c
        src/loader/input/controlIniGen.c
        src/loader/log/log.c
        src/loader/hardware/lindbergh/output.c
        src/loader/hardware/lindbergh/jvs.c
        src/loader/config/config.c
        src/loader/input/evdevInput.c
        src/loader/config/gameData.c
        src/loader/config/configIni.c
        src/loader/rawthrills/rtGames.c
        src/loader/rawthrills/rtLaunch.c
        src/loader/namco/namcoEs1Games.c
        src/loader/namco/namcoEs1Launch.c
        src/loader/namco/namcoN2Games.c
        src/loader/namco/namcoN2Launch.c
        src/loader/teamplay/teamplayGames.c
        src/loader/globalvr/gvrGames.c
        src/loader/roxor/roxorGames.c
        src/loader/cecropia/cecropiaGames.c
        src/loader/igs/igsGames.c
        src/loader/cosmodog/cosmodogGames.c
        src/loader/paokai/paokaiGames.c
        src/loader/byoyoyo/byoyoyoGames.c
        src/loader/itech/itechGames.c
        src/loader/gaelco/gaelcoGames.c
        src/loader/hostLinker.c)

    set_target_properties(linuxloader_exe PROPERTIES LINK_FLAGS "-m32")
    target_link_libraries(linuxloader_exe PRIVATE m SDL3)
    set_target_properties(linuxloader_exe PROPERTIES OUTPUT_NAME "linuxloader")
    target_compile_options(linuxloader_exe PRIVATE ${LINUXLOADER_EXE_C_FLAGS})

    # FreeBSD games (Temple Run): their ELF mapped and run on the host's
    # libraries (src/freebsd/fbsdRunner.c). Position-independent: the game
    # takes the executables' usual address.
    file(GLOB LINUXLOADER_FREEBSD_SRC src/freebsd/*.c)
    add_executable(linuxloader_freebsd ${LINUXLOADER_FREEBSD_SRC})
    target_compile_options(linuxloader_freebsd PRIVATE -m32 -fPIE -Wall -D_GNU_SOURCE)
    set_target_properties(linuxloader_freebsd PROPERTIES LINK_FLAGS "-m32 -pie")
    target_link_libraries(linuxloader_freebsd PRIVATE dl pthread)

    # Common options for shared libraries
    set(LIBS_C_FLAGS -m32 -mincoming-stack-boundary=2 -fPIC -shared)
    set(SHARED_LINKER_FLAGS -m32 -Wl,-z,defs -rdynamic)

    # libkswapapi.so
    add_library(kswapapi SHARED src/loader/extraLibs/lindbergh/libkswapapi/libkswapapi.c)
    target_link_libraries(kswapapi ${SHARED_LINKER_FLAGS})
    target_compile_options(kswapapi PRIVATE ${LIBS_C_FLAGS})

    # libposixtime.so
    add_library(posixtime SHARED src/loader/extraLibs/lindbergh/libposixtime/libposixtime.c)
    target_link_libraries(posixtime ${SHARED_LINKER_FLAGS})
    target_compile_options(posixtime PRIVATE ${LIBS_C_FLAGS})
    add_custom_command(TARGET posixtime POST_BUILD COMMAND ln -s -f libposixtime.so ${CMAKE_BINARY_DIR}/libposixtime.so.1 COMMAND ln -s -f libposixtime.so ${CMAKE_BINARY_DIR}/libposixtime.so.2.4)

    # libGLEW.so.1.3 (Global VR games)
    add_library(glew_gvr SHARED src/loader/extraLibs/globalvr/libglew/libglew.c)
    set_target_properties(glew_gvr PROPERTIES OUTPUT_NAME "GLEW")
    target_link_libraries(glew_gvr ${SHARED_LINKER_FLAGS} dl)
    target_compile_options(glew_gvr PRIVATE ${LIBS_C_FLAGS} -D_GNU_SOURCE)
    add_custom_command(TARGET glew_gvr POST_BUILD COMMAND ln -s -f libGLEW.so ${CMAKE_BINARY_DIR}/libGLEW.so.1.3)

    # libarcaderegistry.so.1 (Namco ES1: Gundam Senjo no Kizuna)
    add_library(arcaderegistry SHARED src/loader/extraLibs/namco/libarcaderegistry/libarcaderegistry.c)
    target_link_libraries(arcaderegistry ${SHARED_LINKER_FLAGS} pthread)
    target_compile_options(arcaderegistry PRIVATE ${LIBS_C_FLAGS})
    add_custom_command(TARGET arcaderegistry POST_BUILD COMMAND ln -s -f libarcaderegistry.so ${CMAKE_BINARY_DIR}/libarcaderegistry.so.1)

    # libsegaapi.so
    add_library(segaapi SHARED src/loader/audio/lindbergh/libsegaapi/libsegaapi.c)
    target_include_directories(segaapi PRIVATE ${PROJECT_SOURCE_DIR}/src/FAudio)
    set(LIBSEGAAPI_C_FLAGS -m32 -mincoming-stack-boundary=2 -fPIC -shared)
    set(LIBSEGAAPI_LINKER_FLAGS -O0 -g -m32 -fPIC -shared)
    target_link_libraries(segaapi ${LIBSEGAAPI_LINKER_FLAGS})
    target_link_libraries(segaapi ${PROJECT_SOURCE_DIR}/libs/linux_x86/libFAudio.a)
    target_compile_options(segaapi PRIVATE ${LIBSEGAAPI_C_FLAGS})

    # linuxloader.so
    set(LINUX_LOADER_SO_LINKER_FLAGS -m32 -Wl,-z,defs -rdynamic -static-libstdc++ -static-libgcc -lc -ldl -lGL -lglut -lX11 -lXcursor -lSDL3 -lSDL3_ttf -lSDL3_image -ludev -lm -lpthread -shared -lstdc++ -lgcc -nostdlib)
    set(LINUX_LOADER_SO_C_FLAGS -g -fPIC -m32 -mincoming-stack-boundary=2 -Wall -Wno-unused-variable -Wno-unused-function -D_GNU_SOURCE -Wno-format-truncation -Wno-strict-aliasing -Wno-discarded-qualifiers -Wno-stringop-truncation -Wno-format-overflow)
    file(GLOB LINUX_LOADER_OBJS
        src/loader/init.c
        src/loader/mainL.c
        src/loader/mainShared.c
        src/loader/hostLinker.c
        src/loader/config/*.c
        src/loader/graphics/*.c
        src/loader/hardware/lindbergh/*.c
        src/loader/input/*.c
        src/loader/log/*.c
        src/loader/patching/*.c
        src/loader/redirections/*.c
        src/loader/common/*.c
        src/loader/rawthrills/*.c
        src/loader/namco/*.c
        src/loader/teamplay/*.c
        src/loader/globalvr/*.c
        src/loader/roxor/*.c
        src/loader/cecropia/*.c
        src/loader/igs/*.c
        src/loader/igs/*.cpp
        src/loader/cosmodog/*.c
        src/loader/paokai/*.c
        src/loader/byoyoyo/*.c
        src/loader/itech/*.c
        src/loader/gaelco/*.c
        src/loader/graphics/shaderWork/*.c
        src/glad/src/gl.c)
    # Dear ImGui (MIT), for overlays drawn over a game (Hero of Robots' card binder)
    list(APPEND LINUX_LOADER_OBJS
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_impl_opengl3.cpp)

    add_library(linuxloader SHARED ${LINUX_LOADER_OBJS})
    set_target_properties(linuxloader PROPERTIES PREFIX "")
    target_link_libraries(linuxloader ${LINUX_LOADER_SO_LINKER_FLAGS} xdiff minhook dl)
    target_compile_options(linuxloader PRIVATE "$<$<COMPILE_LANGUAGE:C>:${LINUX_LOADER_SO_C_FLAGS}>")
    target_compile_options(linuxloader PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:-g;-fPIC;-m32;-mincoming-stack-boundary=2;-Wall;-D_GNU_SOURCE;-fno-exceptions;-fno-rtti>")
    target_compile_definitions(linuxloader PRIVATE IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
        OVERLAY_FONT_PATH=${PROJECT_SOURCE_DIR}/src/fonts/cozette.ttf)
    target_include_directories(linuxloader PRIVATE ${PROJECT_SOURCE_DIR}/src/imgui)
    # errno@GLIBC_2.0 for the Gaelco games, no other library's errno.
    target_link_options(linuxloader PRIVATE -Wl,--version-script=${PROJECT_SOURCE_DIR}/src/loader/gaelco/gaelcoErrno.ver)
    set_property(TARGET linuxloader APPEND PROPERTY LINK_DEPENDS ${PROJECT_SOURCE_DIR}/src/loader/gaelco/gaelcoErrno.ver)

    # The 64-bit games (the Raw Thrills g7 dumps, the Unity games), which
    # linuxloader hands to linuxloader64: it starts them with
    # linuxloader64.so, their library, preloaded.  The library shares the
    # loader's ini parser and gun border, and reads the same linuxloader.ini.
    # g7_rt.so and halo_rt.so, its names when it was the g7 games' alone,
    # link to it.
    add_executable(linuxloader64
        src/loader/loader64/linuxloader64.c
        src/loader/loader64/game64.c
        src/loader/config/iniParser.c)
    target_compile_options(linuxloader64 PRIVATE -m64 -Wall -Wextra -Wno-format-truncation)
    target_link_options(linuxloader64 PRIVATE -m64)

    file(GLOB LOADER64_SO_SRC
        src/loader/loader64/rt64.c
        src/loader/rawthrills/g7/*.c
        src/loader/rawthrills/g7/*.cpp
        # the Raw Thrills Unity base, then its games
        src/loader/rawthrills/unity/*.c
        src/loader/rawthrills/nerf/*.c
        src/loader/rawthrills/sb3/*.c
        src/loader/rawthrills/tmnt/*.c
        src/loader/rawthrills/mcoc/*.c
        src/loader/rawthrills/mcoc/*.cpp)
    add_library(loader64 SHARED
        ${LOADER64_SO_SRC}
        src/loader/config/iniParser.c
        src/loader/input/evdevFfb.c
        src/loader/graphics/border.c
        src/loader/graphics/borderFrame.c
        src/loader/graphics/frameScale.c
        src/glad/src/gl.c
        # Dear ImGui (MIT), for overlays drawn over a game (Minecraft's card binder)
        src/imgui/imgui.cpp
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_tables.cpp
        src/imgui/imgui_widgets.cpp
        src/imgui/imgui_impl_opengl3.cpp)
    set_target_properties(loader64 PROPERTIES PREFIX "" OUTPUT_NAME "linuxloader64")
    target_compile_options(loader64 PRIVATE -m64 -fPIC -Wall -Wextra)
    target_compile_options(loader64 PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions -fno-rtti>)
    target_compile_definitions(loader64 PRIVATE IMGUI_DISABLE_DEMO_WINDOWS IMGUI_DISABLE_DEBUG_TOOLS
        OVERLAY_FONT_PATH=${PROJECT_SOURCE_DIR}/src/fonts/cozette.ttf)
    target_include_directories(loader64 PRIVATE ${PROJECT_SOURCE_DIR}/src/imgui ${PROJECT_SOURCE_DIR}/src/stb)
    target_link_options(loader64 PRIVATE -m64)
    target_link_libraries(loader64 PRIVATE dl pthread m)
    add_custom_command(TARGET loader64 POST_BUILD
        COMMAND ln -s -f linuxloader64.so ${CMAKE_BINARY_DIR}/g7_rt.so
        COMMAND ln -s -f linuxloader64.so ${CMAKE_BINARY_DIR}/halo_rt.so)

elseif(WIN32 OR MINGW OR CMAKE_C_COMPILER MATCHES "mingw")

    enable_language(RC)

    include_directories(${PROJECT_SOURCE_DIR}/src/elfio)
    include_directories(${PROJECT_SOURCE_DIR}/src/SDL3)
    include_directories(${PROJECT_SOURCE_DIR}/src/FAudio)

    # Gather all source files
    file(GLOB SRC_FILES
        src/loader/init.c
        src/loader/mainW.cpp
        src/loader/mainShared.c
        src/loader/audio/lindbergh/libsegaapi/libsegaapi.c
        src/loader/audio/lindbergh/alsa2sdl/alsa2sdl.c
        src/loader/config/*.c
        src/loader/graphics/*.c
        src/loader/hardware/lindbergh/*.c
        src/loader/input/*.c
        src/loader/log/*.c
        src/loader/patching/*.c
        src/loader/redirections/*.c
        src/loader/teamplay/*.c
        src/loader/roxor/*.c
        src/loader/rawthrills/*.c
        src/loader/namco/*.c
        src/loader/gaelco/*.c
        src/loader/common/gamePath.c
        src/loader/common/alignedCall.c
        src/loader/common/desktopInput.c
        src/loader/common/importHook.c
        src/loader/common/ossDsp.c
        src/loader/common/nvGl.c
        src/loader/common/nvProgram.c
        src/loader/common/webcamWin.c
        src/loader/graphics/shaderWork/*.c
        src/loader/elfLoader/*.cpp
        src/loader/elfLoader/pthread/*.cpp
        src/glad/src/gl.c
        src/loader/resources/icon.rc
    )


    # WMMT4 and WMMT5DX+ (their in-loader server, POSIX sockets) and Gundam (WIP on Linux)
    # are not ported yet.
    list(FILTER SRC_FILES EXCLUDE REGEX "namcoEs1Wmmt4|namcoEs1Wmmt5|namcoEs1Gundam")
    # glibc's errno for the Gaelco executables: the resolver answers it here.
    list(FILTER SRC_FILES EXCLUDE REGEX "gaelcoErrno")

    # Executable target
    add_executable(linuxloader ${SRC_FILES})

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        _WIN32_WINNT=0x0600
        WINVER=0x0600
    )

    # Guest code is i386 SysV and needs a 16-byte aligned ESP at every call;
    # MinGW defaults to 4, so both flags are required to realign native->guest calls.
    target_compile_options(${PROJECT_NAME} PRIVATE
        -mincoming-stack-boundary=2
        -mpreferred-stack-boundary=4
        -Wall
        -Wextra
        -fpermissive
        -Wno-unused-parameter
        -Wunused-variable
        -Wno-unused-but-set-variable
        -Wimplicit-fallthrough=0
        -Wstrict-aliasing=0
    )

    # 4 MB reserved AND committed: the guest emits no stack probes, so a large
    # frame skips the guard page. -Wl, splits on commas and drops the commit value.
    target_link_options(${PROJECT_NAME} PRIVATE
        -s
        "-Xlinker" "--stack" "-Xlinker" "4194304,4194304"
        -Wl,--large-address-aware
    )

    target_link_directories(${PROJECT_NAME} PRIVATE "libs/win32")

    # Link dependencies
    target_link_directories(${PROJECT_NAME} PRIVATE "libs/win32")
    target_link_libraries(${PROJECT_NAME}
        minhook xdiff -Wl,-Bstatic SDL3 SDL3_ttf SDL3_image FAudio freetype harfbuzz winpthread stdc++ gcc gcc_eh -Wl,-Bdynamic gdi32 winmm ole32 ws2_32 psapi usp10 rpcrt4 setupapi version imm32 hid delayimp iphlpapi
    )

else()
    message(WARNING "Unknown platform, doing nothing. Please use Linux or MinGW.")
endif()
