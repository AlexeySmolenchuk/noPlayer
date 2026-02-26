cmake_minimum_required(VERSION 3.16)

set(NOPLAYER_VCPKG_TOOLCHAIN "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "${NOPLAYER_VCPKG_TOOLCHAIN}")
    set(CMAKE_TOOLCHAIN_FILE "${NOPLAYER_VCPKG_TOOLCHAIN}" CACHE STRING "Vcpkg toolchain file")
endif()

if(WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET x64-windows-static CACHE STRING "Vcpkg target triplet")
endif()

option(NOPLAYER_SKIP_HOST_DEP_CHECKS "Skip Linux host dependency checks before vcpkg build." OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "${NOPLAYER_VCPKG_TOOLCHAIN}" AND NOT NOPLAYER_SKIP_HOST_DEP_CHECKS)
    if(CMAKE_GENERATOR STREQUAL "Ninja")
        find_program(NOPLAYER_NINJA_EXECUTABLE NAMES ninja ninja-build)
        if(NOT NOPLAYER_NINJA_EXECUTABLE)
            list(APPEND NOPLAYER_HOST_DEP_ERRORS
                "Ninja generator selected, but ninja is missing.")
        endif()
    endif()

    find_program(NOPLAYER_PKG_CONFIG_EXECUTABLE pkg-config)
    if(NOPLAYER_PKG_CONFIG_EXECUTABLE)
        foreach(NOPLAYER_MODULE IN ITEMS glu xrandr xi xinerama xcursor xxf86vm)
            execute_process(
                COMMAND "${NOPLAYER_PKG_CONFIG_EXECUTABLE}" --exists "${NOPLAYER_MODULE}"
                RESULT_VARIABLE NOPLAYER_MODULE_STATUS
                OUTPUT_QUIET
                ERROR_QUIET
            )
            if(NOT NOPLAYER_MODULE_STATUS EQUAL 0)
                list(APPEND NOPLAYER_MISSING_PKGCONFIG_MODULES "${NOPLAYER_MODULE}")
            endif()
        endforeach()
        if(NOPLAYER_MISSING_PKGCONFIG_MODULES)
            string(JOIN ", " NOPLAYER_MISSING_PKGCONFIG_MODULES_TEXT ${NOPLAYER_MISSING_PKGCONFIG_MODULES})
            list(APPEND NOPLAYER_HOST_DEP_ERRORS
                "Missing pkg-config modules: ${NOPLAYER_MISSING_PKGCONFIG_MODULES_TEXT}")
        endif()
    else()
        list(APPEND NOPLAYER_HOST_DEP_ERRORS
            "pkg-config is missing.")
    endif()

    find_program(NOPLAYER_PERL_EXECUTABLE perl)
    if(NOPLAYER_PERL_EXECUTABLE)
        execute_process(
            COMMAND "${NOPLAYER_PERL_EXECUTABLE}" -MIPC::Cmd -MFindBin -e1
            RESULT_VARIABLE NOPLAYER_PERL_MODULE_STATUS
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(NOT NOPLAYER_PERL_MODULE_STATUS EQUAL 0)
            list(APPEND NOPLAYER_HOST_DEP_ERRORS
                "Perl modules IPC::Cmd and FindBin are missing.")
        endif()
    else()
        list(APPEND NOPLAYER_HOST_DEP_ERRORS
            "Perl is missing.")
    endif()

    if(NOPLAYER_HOST_DEP_ERRORS)
        string(JOIN "\n  - " NOPLAYER_HOST_DEP_ERRORS_TEXT ${NOPLAYER_HOST_DEP_ERRORS})
        message(FATAL_ERROR
            "Missing Linux host dependencies required by vcpkg:\n"
            "  - ${NOPLAYER_HOST_DEP_ERRORS_TEXT}\n\n"
            "Fedora:\n"
            "  sudo dnf install -y ninja-build pkgconf-pkg-config "
            "mesa-libGL-devel mesa-libGLU-devel "
            "libX11-devel libXrandr-devel libXi-devel libXinerama-devel libXcursor-devel libXxf86vm-devel "
            "perl perl-IPC-Cmd\n\n"
            "Debian/Ubuntu:\n"
            "  sudo apt-get install -y ninja-build pkg-config "
            "libgl-dev libglu1-mesa-dev libxrandr-dev libxi-dev libxinerama-dev libxcursor-dev libxxf86vm-dev "
            "perl perl-modules"
        )
    endif()
endif()

project(noPlayer VERSION 0.0.0 LANGUAGES CXX)

find_package(GLEW CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)

find_package(OpenColorIO CONFIG REQUIRED)
find_package(OpenImageIO CONFIG REQUIRED)

add_subdirectory(src)
