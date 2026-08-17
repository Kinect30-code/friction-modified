# PSD import module for friction-modified
#
# Compiled as C++14 (fits friction core) and linked into the app when
# BUILD_PSD_IMPORT is ON. The reader is a self-contained PSD parser; the
# mapper talks to friction core via public headers only.

option(BUILD_PSD_IMPORT "Build PSD import module" OFF)

if(NOT BUILD_PSD_IMPORT)
    return()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(friction-common)

add_library(friction_psd_import STATIC
    psdreader.cpp
    psdmapper.cpp
)

target_include_directories(friction_psd_import PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/skia
)

target_link_libraries(friction_psd_import PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Qml
    Qt5::Xml
)
