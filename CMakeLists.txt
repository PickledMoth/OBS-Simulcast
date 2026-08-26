cmake_minimum_required(VERSION 3.16...3.26)

project(OBS-Simulcast VERSION 0.1.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# --- Dependencies -----------------------------------------------------------
# Expects a locally built/installed obs-studio dev tree (libobs + obs-frontend-api).
# Point CMake at it with -DLIBOBS_DIR / -Dobs-frontend-api_DIR as needed, e.g.:
#   cmake -B build -DLIBOBS_DIR="C:/obs-studio/build/libobs"
find_package(LibObs REQUIRED)
find_package(obs-frontend-api REQUIRED)

# obsconfig.h is generated into the obs-studio *build* tree (config/), not
# shipped in the source tree, so libobs consumers must be pointed at it
# explicitly. OBS_BUILD_DIR should point at the obs-studio build directory
# (e.g. .../obs-studio/build_x64) that produced the libs found above.
if(NOT OBS_BUILD_DIR)
    get_filename_component(OBS_BUILD_DIR "${LIBOBS_LIB}/../../.." ABSOLUTE)
endif()
find_path(OBS_CONFIG_INCLUDE_DIR NAMES obsconfig.h HINTS "${OBS_BUILD_DIR}/config")

find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)
set(QT_VERSION 6)

# --- Sources -----------------------------------------------------------------
set(OBS-Simulcast_SOURCES
    src/plugin-main.cpp
    src/multistream-manager.cpp
    src/output-target.cpp
    src/vertical-canvas.cpp
    src/platform-presets.cpp
    src/settings-store.cpp
    src/secure-string.cpp
    src/chat-url.cpp
    src/twitch-chat-client.cpp
    src/youtube-chat-client.cpp
    src/password-crypto.cpp
    ui/multistream-dock.cpp
    ui/encoder-settings-dialog.cpp
    ui/chat-link-dialog.cpp
    ui/chat-view-dock.cpp
    ui/platform-icons.cpp
    ui/chat-overlay-server.cpp
    ui/remote-image-text-browser.cpp
    ui/floating-chat-overlay.cpp
    ui/chat-diagnostics-dialog.cpp
    ui/first-run-setup-dialog.cpp
)

set(OBS-Simulcast_HEADERS
    src/multistream-manager.h
    src/output-target.h
    src/vertical-canvas.h
    src/platform-presets.h
    src/settings-store.h
    src/secure-string.h
    src/chat-url.h
    src/twitch-chat-client.h
    src/youtube-chat-client.h
    src/password-crypto.h
    ui/multistream-dock.h
    ui/encoder-settings-dialog.h
    ui/chat-link-dialog.h
    ui/chat-view-dock.h
    ui/platform-icons.h
    ui/chat-overlay-server.h
    ui/remote-image-text-browser.h
    ui/floating-chat-overlay.h
    ui/chat-diagnostics-dialog.h
    ui/first-run-setup-dialog.h
)

add_library(OBS-Simulcast MODULE
    ${OBS-Simulcast_SOURCES}
    ${OBS-Simulcast_HEADERS}
)

target_include_directories(OBS-Simulcast PRIVATE
    src
    ui
    ${CMAKE_CURRENT_BINARY_DIR}
    ${OBS_CONFIG_INCLUDE_DIR}
)

target_link_libraries(OBS-Simulcast PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt::Widgets
    Qt::Core
    Qt::Gui
    Qt::Network
)

if(WIN32)
    target_link_libraries(OBS-Simulcast PRIVATE Crypt32 Bcrypt)
endif()

target_compile_definitions(OBS-Simulcast PRIVATE
    PLUGIN_NAME="OBS-Simulcast"
    PLUGIN_VERSION="${PROJECT_VERSION}"
)

set_target_properties(OBS-Simulcast PROPERTIES
    PREFIX ""
)

# --- Install / package ---------------------------------------------------
if(WIN32)
    install(TARGETS OBS-Simulcast
        LIBRARY DESTINATION "obs-plugins/64bit"
        RUNTIME DESTINATION "obs-plugins/64bit")
elseif(APPLE)
    install(TARGETS OBS-Simulcast
        LIBRARY DESTINATION "PlugIns")
else()
    install(TARGETS OBS-Simulcast
        LIBRARY DESTINATION "lib/obs-plugins")
endif()

install(DIRECTORY data/ DESTINATION "data/obs-plugins/OBS-Simulcast")

include(cmake/Packaging.cmake)
