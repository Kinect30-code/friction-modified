# AEP import module for friction-modified
#
# Two libraries:
#  - friction_aep_parser : glaxnimate-derived AEP binary parser (C++17).
#    Pure format parsing, no friction dependencies.
#  - friction_aep_mapper : maps parsed AEP project onto friction's
#    BoundingBox/Canvas model. Compiled as C++17 as well; links against
#    friction core via its public headers only, so the core stays C++14.
#
# The module is opt-in: only compiled when BUILD_AEP_IMPORT is ON and the
# PluginManager feature aepImport is enabled at runtime.

option(BUILD_AEP_IMPORT "Build AEP import module" OFF)

if(NOT BUILD_AEP_IMPORT)
    return()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/../../cmake")
include(friction-common)

add_library(friction_aep_parser STATIC
    aepparser.cpp
    glax/string_decoder.cpp
    glax/gradient_xml.cpp
)

target_include_directories(friction_aep_parser PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/glax
)

# The parser uses C++17 features (std::variant / std::optional).
set_target_properties(friction_aep_parser PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(friction_aep_parser PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Xml
)

add_library(friction_aep_mapper STATIC
    aepmapper.cpp
)

target_include_directories(friction_aep_mapper PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/glax
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/skia
)

set_target_properties(friction_aep_mapper PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)

target_link_libraries(friction_aep_mapper PUBLIC
    friction_aep_parser
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::Qml
    Qt5::Xml
)

# Linked into the core library by the top-level CMakeLists when enabled.
