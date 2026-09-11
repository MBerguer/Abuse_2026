cmake_minimum_required(VERSION 3.20)

project(abuse 
    VERSION 2025.1.0.0
    LANGUAGES C CXX)

# Add GNUInstallDirs for standard installation directories
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Detect the platform, as there's quite a bit of platform variation

if (WIN32)
    # TODO: Think about Win64
    enable_language(RC)
endif()

if (VCPKG_TARGET_TRIPLET)
    # If using vcpkg, don't specify versions, they're in the manifest
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_mixer CONFIG REQUIRED)
else()
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
    find_package(SDL2 2.0.0 REQUIRED)
    find_package(SDL2_mixer 2.0.0 REQUIRED)
    # For compatibility with vcpkg, configure SDL2_mixer as an imported shared library
    add_library(SDL2_mixer::SDL2_mixer SHARED IMPORTED)
    set_property(TARGET SDL2_mixer::SDL2_mixer PROPERTY IMPORTED_LOCATION "${SDL2_MIXER_LIBRARY}")
    set_property(TARGET SDL2_mixer::SDL2_mixer PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${SDL2_MIXER_INCLUDE_DIRS}")
endif()

find_package(OpenGL REQUIRED)
add_compile_definitions(GL_SILENCE_DEPRECATION)

include(CheckIncludeFiles)

check_include_files(unistd.h HAVE_UNISTD_H)
check_include_files("sys/time.h" HAVE_SYS_TIME_H)

option(HAVE_NETWORK "Enable networking support" TRUE)

if(TCPIP_DEBUG)
    add_compile_definitions(TCPIP_DEBUG)
endif()

if(NO_CHECK)
    add_compile_definitions(NO_CHECK)
endif()
        
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in ${CMAKE_CURRENT_BINARY_DIR}/config.h)

if(WIN32)
    # For Windows, just use whatever CMake wants
    set(ASSETDIR ".")
elseif(APPLE)
    # Under macOS, redirect to an app bundle
    set(ASSETDIR "abuse.app/Contents/Resources/data")
else()    
    # Under Linux, use standard installation paths
    if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        # Standard system installation
        set(ASSETDIR "${CMAKE_INSTALL_FULL_DATADIR}/games/${PROJECT_NAME}/data")        
    else()
        # Local installation
        set(ASSETDIR "${CMAKE_INSTALL_PREFIX}/data")
    endif()
    # Only define this for ports that actually use it
    add_compile_definitions(ASSETDIR="${ASSETDIR}")
    
    message("CMAKE_INSTALL_PREFIX is set to: ${CMAKE_INSTALL_PREFIX}")
    message("ASSETDIR is set to: ${ASSETDIR}")
endif()

add_compile_definitions(
    PACKAGE_NAME="abuse"
    PACKAGE_VERSION="${abuse_VERSION}"
    HAVE_CONFIG_H
)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Packaging

set(CPACK_PACKAGE_NAME "Abuse")
set(CPACK_PACKAGE_VENDOR "Crack-Dot-Com")
set(CPACK_PACKAGE_VERSION_MAJOR ${abuse_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${abuse_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${abuse_VERSION_PATCH})
#set(CPACK_PACKAGE_DESCRIPTION_FILE "README.md")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Abuse game")
#set(CPACK_PACKAGE_ICON "doc/abuse.bmp")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGE_EXECUTABLES "abuse;Abuse")
if (WIN32)
    set(CPACK_GENERATOR "WIX;ZIP")
    # This doesn't make sense for a Windows archive
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY off)
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop;TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX .)
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
else()
    set(CPACK_GENERATOR "TGZ")
endif()
# WIX-specific
set(CPACK_WIX_UPGRADE_GUID "3A89DEB1-B520-4DF5-B44D-96A0EBB27DC0")
set(CPACK_WIX_LICENSE_RTF "${CMAKE_CURRENT_SOURCE_DIR}/COPYING.rtf")
set(CPACK_WIX_PRODUCT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/doc/icon.ico")
set(CPACK_WIX_PROGRAM_MENU_FOLDER "Abuse")

include(CPack)

# Modify install location for the executable on Unix systems
if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_BINDIR "games")
endif()

add_subdirectory(src)
add_subdirectory(data)