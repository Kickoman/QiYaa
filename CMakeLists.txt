cmake_minimum_required(VERSION 3.21)

# The version the packages are named after and the app shows. A release build in CI sets it from
# its tag vX.Y.Z; the default here is for builds from a checkout.
set(QIYAA_VERSION "0.8.0" CACHE STRING "The app's version, X.Y.Z (CI takes it from the tag)")
if(NOT QIYAA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "QIYAA_VERSION must be X.Y.Z, got '${QIYAA_VERSION}'")
endif()

project(QiYaa
    VERSION ${QIYAA_VERSION}
    DESCRIPTION "Winamp-style Yandex Music player (C++/Qt)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QIYAA_BUILD_TESTS "Build unit tests" ON)

# For packaging scripts (CI names the installers after it).
file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${PROJECT_VERSION}")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network LinguistTools)
qt_standard_project_setup()

add_library(qiyaa_miniz STATIC
    contrib/miniz/miniz.c
    contrib/miniz/miniz_tdef.c
    contrib/miniz/miniz_tinfl.c
    contrib/miniz/miniz_zip.c)
target_include_directories(qiyaa_miniz PUBLIC contrib/miniz)
target_compile_definitions(qiyaa_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_TIME)
set_target_properties(qiyaa_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(qiyaa_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(qiyaa_miniaudio PUBLIC contrib/miniaudio)
# Only what we need: playback + mp3/flac/wav decoders. No encoders, no generation.
target_compile_definitions(qiyaa_miniaudio PUBLIC
    MA_NO_ENCODING MA_NO_GENERATION MA_NO_VORBIS)
find_package(Threads REQUIRED)
target_link_libraries(qiyaa_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(APPLE)
    # Link CoreAudio directly instead of dlopen() so the app can be notarized.
    target_compile_definitions(qiyaa_miniaudio PUBLIC MA_NO_RUNTIME_LINKING)
    target_link_libraries(qiyaa_miniaudio PUBLIC
        "-framework CoreFoundation" "-framework CoreAudio" "-framework AudioToolbox")
elseif(UNIX)
    target_link_libraries(qiyaa_miniaudio PUBLIC m)
endif()

# The MP4 demuxer and the AAC decoder behind the economy quality (spec/player/quality.md).
add_library(qiyaa_minimp4 STATIC src/audio/minimp4_impl.c)
target_include_directories(qiyaa_minimp4 PUBLIC contrib/minimp4)
set_target_properties(qiyaa_minimp4 PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(qiyaa_minimp4 PRIVATE /w)
else()
    target_compile_options(qiyaa_minimp4 PRIVATE -w)
endif()
add_subdirectory(contrib/fdk-aac)

# The Ogg Vorbis decoder behind Spotify's files and local .ogg files (spec/player/spotify.md, LOCAL-09).
add_library(qiyaa_stb_vorbis STATIC src/audio/stb_vorbis_impl.c)
target_include_directories(qiyaa_stb_vorbis PUBLIC contrib/stb_vorbis)
target_compile_definitions(qiyaa_stb_vorbis PUBLIC STB_VORBIS_NO_PULLDATA_API)
set_target_properties(qiyaa_stb_vorbis PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(qiyaa_stb_vorbis PRIVATE /w)
else()
    target_compile_options(qiyaa_stb_vorbis PRIVATE -w)
endif()

# The crypto behind the Spotify protocol (src/spotify/protocol): a subset of BearSSL 7bea48e.
add_library(qiyaa_bearssl STATIC
    contrib/bearssl/src/codec/ccopy.c
    contrib/bearssl/src/codec/dec32be.c
    contrib/bearssl/src/codec/enc32be.c
    contrib/bearssl/src/hash/sha1.c
    contrib/bearssl/src/int/i31_add.c
    contrib/bearssl/src/int/i31_bitlen.c
    contrib/bearssl/src/int/i31_decmod.c
    contrib/bearssl/src/int/i31_decode.c
    contrib/bearssl/src/int/i31_encode.c
    contrib/bearssl/src/int/i31_fmont.c
    contrib/bearssl/src/int/i31_modpow2.c
    contrib/bearssl/src/int/i31_montmul.c
    contrib/bearssl/src/int/i31_muladd.c
    contrib/bearssl/src/int/i31_ninv31.c
    contrib/bearssl/src/int/i31_rshift.c
    contrib/bearssl/src/int/i31_sub.c
    contrib/bearssl/src/int/i31_tmont.c
    contrib/bearssl/src/int/i32_div32.c
    contrib/bearssl/src/mac/hmac.c
    contrib/bearssl/src/rsa/rsa_i31_pkcs1_vrfy.c
    contrib/bearssl/src/rsa/rsa_i31_pub.c
    contrib/bearssl/src/rsa/rsa_pkcs1_sig_unpad.c
    contrib/bearssl/src/symcipher/aes_ct.c
    contrib/bearssl/src/symcipher/aes_ct_ctr.c
    contrib/bearssl/src/symcipher/aes_ct_enc.c)
target_include_directories(qiyaa_bearssl PUBLIC contrib/bearssl/inc PRIVATE contrib/bearssl/src)
set_target_properties(qiyaa_bearssl PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    target_compile_options(qiyaa_bearssl PRIVATE /w)
else()
    target_compile_options(qiyaa_bearssl PRIVATE -w)
endif()

# One static library per module: a folder of src/ with sources in it (src/sources/yandex is qiyaa_sources_yandex).
# Dependencies point one way:
#   audio, yandex, skins  ->  vis, core  ->  sources/yandex, sources/local, lrclib  ->  jam  ->  ui, integrations  ->  app  ->  qiyaa (main)
function(qiyaa_target_defaults target)
    target_compile_definitions(${target} PUBLIC QT_NO_KEYWORDS)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
    endif()
endfunction()

function(qiyaa_add_module name)
    add_library(qiyaa_${name} STATIC ${ARGN})
    target_include_directories(qiyaa_${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    qiyaa_target_defaults(qiyaa_${name})
endfunction()

qiyaa_add_module(audio
    src/audio/audio_engine.cpp  src/audio/audio_engine.h
    src/audio/blocks.h
    src/audio/byte_source.h
    src/audio/decoder.cpp       src/audio/decoder.h
    src/audio/error.h
    src/audio/eq_presets.cpp    src/audio/eq_presets.h
    src/audio/equalizer.cpp     src/audio/equalizer.h
    src/audio/file_byte_source.cpp src/audio/file_byte_source.h
    src/audio/mp4_aac_backend.cpp src/audio/mp4_aac_backend.h
    src/audio/ogg_vorbis_backend.cpp src/audio/ogg_vorbis_backend.h
    src/audio/stream_buffer.cpp src/audio/stream_buffer.h
    src/audio/vis_tap.cpp       src/audio/vis_tap.h)
target_link_libraries(qiyaa_audio PUBLIC Qt6::Core
    PRIVATE qiyaa_miniaudio qiyaa_minimp4 qiyaa_fdk_aac qiyaa_stb_vorbis)

qiyaa_add_module(spotify_protocol
    src/spotify/protocol/access_point.cpp src/spotify/protocol/access_point.h
    src/spotify/protocol/audio_file.cpp   src/spotify/protocol/audio_file.h
    src/spotify/protocol/bytes.cpp        src/spotify/protocol/bytes.h
    src/spotify/protocol/crypto.cpp       src/spotify/protocol/crypto.h
    src/spotify/protocol/ids.cpp          src/spotify/protocol/ids.h
    src/spotify/protocol/login.cpp        src/spotify/protocol/login.h
    src/spotify/protocol/metadata.cpp     src/spotify/protocol/metadata.h
    src/spotify/protocol/modpow.c         src/spotify/protocol/modpow.h
    src/spotify/protocol/protobuf.cpp     src/spotify/protocol/protobuf.h
    src/spotify/protocol/shannon.cpp      src/spotify/protocol/shannon.h)
target_include_directories(qiyaa_spotify_protocol PRIVATE contrib/bearssl/src)
target_link_libraries(qiyaa_spotify_protocol PRIVATE qiyaa_bearssl)

qiyaa_add_module(spotify_client
    src/spotify/client/credentials_file.cpp src/spotify/client/credentials_file.h
    src/spotify/client/endpoints.h
    src/spotify/client/file_download.cpp    src/spotify/client/file_download.h
    src/spotify/client/identity.cpp         src/spotify/client/identity.h
    src/spotify/client/login.cpp            src/spotify/client/login.h
    src/spotify/client/session.cpp          src/spotify/client/session.h
    src/spotify/client/web.cpp              src/spotify/client/web.h)
target_link_libraries(qiyaa_spotify_client PUBLIC qiyaa_spotify_protocol Qt6::Core Qt6::Network)

qiyaa_add_module(yandex
    src/yandex/api_client.cpp   src/yandex/api_client.h
    src/yandex/download.cpp     src/yandex/download.h
    src/yandex/file_info.cpp    src/yandex/file_info.h
    src/yandex/library.cpp      src/yandex/library.h
    src/yandex/oauth.cpp        src/yandex/oauth.h
    src/yandex/token.cpp        src/yandex/token.h
    src/yandex/track_url.cpp    src/yandex/track_url.h)
target_link_libraries(qiyaa_yandex PUBLIC Qt6::Core Qt6::Network)

qiyaa_add_module(skins
    src/skins/error.h
    src/skins/region.cpp        src/skins/region.h
    src/skins/skin.cpp          src/skins/skin.h
    src/skins/sprites.h)
target_link_libraries(qiyaa_skins PUBLIC Qt6::Gui PRIVATE qiyaa_miniz)
qt_add_resources(qiyaa_skins "skins"
    PREFIX "/skins"
    BASE resources/skins
    FILES
        resources/skins/base-2.91.wsz
        resources/skins/Green-Dimension-V2.wsz
        resources/skins/MacOSXAqua1-5.wsz
        resources/skins/Skinner_Atlas.wsz
        resources/skins/TopazAmp1-2.wsz
        resources/skins/Vizor1-01.wsz
        resources/skins/XMMS-Turquoise.wsz
        resources/skins/ZaxonRemake1-0.wsz)

qiyaa_add_module(vis
    src/vis/milkdrop_presets.cpp src/vis/milkdrop_presets.h
    src/vis/milkdrop_failure.h
    src/vis/visualizer.h        src/vis/visualizer.cpp)
target_link_libraries(qiyaa_vis PUBLIC Qt6::Gui PRIVATE qiyaa_audio qiyaa_skins)

qiyaa_add_module(core
    src/core/cover_cache.cpp    src/core/cover_cache.h
    src/core/failure_policy.cpp src/core/failure_policy.h
    src/core/jam_mode.cpp       src/core/jam_mode.h
    src/core/lyrics.cpp         src/core/lyrics.h
    src/core/lyrics_finder.cpp  src/core/lyrics_finder.h
    src/core/player.cpp         src/core/player.h
    src/core/queue.cpp          src/core/queue.h
    src/core/queue_source.h
    src/core/source_notice.h
    src/core/track.cpp          src/core/track.h
    src/core/track_navigator.cpp src/core/track_navigator.h
    src/core/track_source.cpp   src/core/track_source.h)
target_link_libraries(qiyaa_core PUBLIC qiyaa_audio Qt6::Gui Qt6::Network)

qiyaa_add_module(sources_yandex
    src/sources/yandex/catalog.cpp  src/sources/yandex/catalog.h
    src/sources/yandex/history_wave.cpp src/sources/yandex/history_wave.h
    src/sources/yandex/source.cpp   src/sources/yandex/source.h
    src/sources/yandex/tracks.cpp   src/sources/yandex/tracks.h
    src/sources/yandex/vibe.cpp     src/sources/yandex/vibe.h
    src/sources/yandex/wave_origins.cpp src/sources/yandex/wave_origins.h)
target_link_libraries(qiyaa_sources_yandex PUBLIC qiyaa_core qiyaa_yandex Qt6::Core)

# TagLib reads the tags and covers of local files. It is our own build on every platform, linked
# statically under its MPL-1.1 (THIRD_PARTY.md), so that its limits are the same everywhere: no zlib
# (a compressed ID3v2 frame is never inflated) and only the formats the engine plays. It installs
# nothing of its own.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(taglib
    URL https://github.com/taglib/taglib/releases/download/v2.3.2/taglib-2.3.2.tar.gz
    URL_HASH SHA256=3ca2d8afaa7f1cf7f6ed10e511ebc368bfacd6dcaa3dbfa690b89e502e8963dc)
set(BUILD_SHARED_LIBS OFF)
set(BUILD_TESTING OFF)
set(BUILD_EXAMPLES OFF)
set(BUILD_BINDINGS OFF)
set(WITH_ZLIB OFF)
foreach(format APE ASF DSF MATROSKA MOD SHORTEN TRUEAUDIO)
    set(WITH_${format} OFF)
endforeach()
FetchContent_MakeAvailable(taglib)
foreach(variable BUILD_SHARED_LIBS BUILD_TESTING BUILD_EXAMPLES BUILD_BINDINGS WITH_ZLIB)
    unset(${variable})
endforeach()
foreach(format APE ASF DSF MATROSKA MOD SHORTEN TRUEAUDIO)
    unset(WITH_${format})
endforeach()
set_property(DIRECTORY ${taglib_SOURCE_DIR} PROPERTY EXCLUDE_FROM_ALL TRUE)
configure_file(${taglib_SOURCE_DIR}/COPYING.MPL ${CMAKE_BINARY_DIR}/taglib-LICENSE.txt COPYONLY)
get_target_property(_qiyaa_taglib_includes tag INCLUDE_DIRECTORIES)
target_include_directories(tag SYSTEM INTERFACE
    "$<BUILD_INTERFACE:${taglib_SOURCE_DIR}/taglib;${_qiyaa_taglib_includes}>")
add_library(TagLib::tag ALIAS tag)

qiyaa_add_module(sources_local
    src/sources/local/catalog.cpp   src/sources/local/catalog.h
    src/sources/local/lyrics.cpp    src/sources/local/lyrics.h
    src/sources/local/scan.cpp      src/sources/local/scan.h
    src/sources/local/source.cpp    src/sources/local/source.h
    src/sources/local/tags.cpp      src/sources/local/tags.h
    src/sources/local/tracks.cpp    src/sources/local/tracks.h)
target_link_libraries(qiyaa_sources_local PUBLIC qiyaa_core Qt6::Core PRIVATE TagLib::tag)

qiyaa_add_module(lrclib
    src/lrclib/search.cpp       src/lrclib/search.h
    src/lrclib/service.cpp      src/lrclib/service.h)
target_link_libraries(qiyaa_lrclib PUBLIC qiyaa_core Qt6::Core Qt6::Network)

# Jam: a shared queue the app hosts for guests (spec/jam). Needs Qt WebSockets; without it the
# app builds without the jam, like without Milkdrop.
option(QIYAA_WITH_JAM "Jam: host a shared queue for guests (needs Qt WebSockets)" ON)
option(QIYAA_REQUIRE_JAM "Stop with an error instead of building without the jam" OFF)
set(QIYAA_JAM_URL "https://qiyaa.kanstancin.net" CACHE STRING "The jam server the app offers by default")
set(QIYAA_HAVE_JAM OFF)
if(QIYAA_WITH_JAM)
    find_package(Qt6 6.4 QUIET COMPONENTS WebSockets)
    if(TARGET Qt6::WebSockets)
        qiyaa_add_module(jam
            src/jam/client.cpp          src/jam/client.h
            src/jam/codec.cpp           src/jam/codec.h
            src/jam/host_session.cpp    src/jam/host_session.h
            src/jam/protocol.h
            src/jam/session_store.cpp   src/jam/session_store.h)
        target_link_libraries(qiyaa_jam PUBLIC qiyaa_core qiyaa_yandex Qt6::Core Qt6::WebSockets PRIVATE qiyaa_sources_yandex)
        target_compile_definitions(qiyaa_jam PUBLIC QIYAA_HAVE_JAM)
        set(QIYAA_HAVE_JAM ON)
        message(STATUS "Jam: enabled (default server ${QIYAA_JAM_URL})")
    else()
        message(STATUS "Jam: disabled (Qt6::WebSockets not found)")
    endif()
endif()
if(QIYAA_REQUIRE_JAM AND NOT QIYAA_HAVE_JAM)
    message(FATAL_ERROR "The jam was required but can't be built (see above)")
endif()

qiyaa_add_module(ui
    src/ui/equalizer_window.cpp src/ui/equalizer_window.h
    src/ui/gen_window.cpp       src/ui/gen_window.h
    src/ui/input_dialogs.cpp    src/ui/input_dialogs.h
    src/ui/library_menu.cpp     src/ui/library_menu.h
    src/ui/lyrics_window.cpp    src/ui/lyrics_window.h
    src/ui/login_dialog.cpp     src/ui/login_dialog.h
    src/ui/main_window.cpp      src/ui/main_window.h
    src/ui/now_playing_window.cpp src/ui/now_playing_window.h
    src/ui/player_notices.cpp   src/ui/player_notices.h
    src/ui/playlist_window.cpp  src/ui/playlist_window.h
    src/ui/skinned_window.cpp   src/ui/skinned_window.h
    src/ui/source_notices.cpp   src/ui/source_notices.h
    src/ui/snap.cpp             src/ui/snap.h
    src/ui/spotify_login_dialog.cpp src/ui/spotify_login_dialog.h)
target_link_libraries(qiyaa_ui
    PUBLIC qiyaa_audio qiyaa_vis Qt6::Widgets
    PRIVATE qiyaa_core qiyaa_skins qiyaa_sources_yandex qiyaa_spotify_client qiyaa_yandex)
if(QIYAA_HAVE_JAM)
    # The jam window and its QR code (contrib/qrcodegen, MIT).
    add_library(qiyaa_qrcodegen STATIC contrib/qrcodegen/qrcodegen.cpp)
    target_include_directories(qiyaa_qrcodegen PUBLIC contrib/qrcodegen)
    set_target_properties(qiyaa_qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_sources(qiyaa_ui PRIVATE
        src/ui/jam_server_dialog.cpp src/ui/jam_server_dialog.h
        src/ui/jam_window.cpp        src/ui/jam_window.h)
    target_link_libraries(qiyaa_ui PUBLIC qiyaa_jam PRIVATE qiyaa_qrcodegen)
endif()

qiyaa_add_module(integrations
    src/integrations/media_controls.cpp src/integrations/media_controls.h)
target_link_libraries(qiyaa_integrations PUBLIC Qt6::Gui PRIVATE qiyaa_core)

# System media integration: MPRIS, macOS MediaPlayer, or Windows SMTC.
if(APPLE)
    enable_language(OBJCXX)
    set_target_properties(qiyaa_integrations PROPERTIES
        OBJCXX_STANDARD 20 OBJCXX_STANDARD_REQUIRED ON OBJCXX_EXTENSIONS OFF)
    target_sources(qiyaa_integrations PRIVATE
        src/integrations/mac_media_controls.mm src/integrations/mac_media_controls.h)
    set_source_files_properties(src/integrations/mac_media_controls.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(qiyaa_integrations PRIVATE "-framework Foundation" "-framework MediaPlayer")
    target_compile_definitions(qiyaa_integrations PUBLIC QIYAA_HAVE_MAC_MEDIA_CONTROLS)
    message(STATUS "macOS media controls: enabled")
endif()
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_sources(qiyaa_integrations PRIVATE
            src/integrations/mpris.cpp src/integrations/mpris.h)
        target_link_libraries(qiyaa_integrations PUBLIC Qt6::DBus)
        target_compile_definitions(qiyaa_integrations PUBLIC QIYAA_HAVE_MPRIS)
        set(QIYAA_HAVE_MPRIS ON)
        message(STATUS "MPRIS: enabled")
    else()
        message(STATUS "MPRIS: disabled (Qt6::DBus not found)")
    endif()
endif()
if(WIN32 AND MSVC)
    option(QIYAA_WITH_SMTC "Windows media controls (needs C++/WinRT from the Windows SDK)" ON)
    if(QIYAA_WITH_SMTC)
        include(CheckIncludeFileCXX)
        set(CMAKE_REQUIRED_FLAGS "/std:c++20 /EHsc")
        check_include_file_cxx("winrt/Windows.Media.h" QIYAA_HAVE_CPPWINRT)
        unset(CMAKE_REQUIRED_FLAGS)
        if(QIYAA_HAVE_CPPWINRT)
            target_sources(qiyaa_integrations PRIVATE
                src/integrations/smtc.cpp src/integrations/smtc.h)
            target_link_libraries(qiyaa_integrations PRIVATE windowsapp Qt6::Widgets)
            target_compile_definitions(qiyaa_integrations PUBLIC QIYAA_HAVE_SMTC)
            message(STATUS "SMTC: enabled")
        else()
            message(STATUS "SMTC: disabled (C++/WinRT headers not found)")
        endif()
    endif()
endif()

# Milkdrop: projectM (LGPL-2.1, linked dynamically) + Qt OpenGL. Uses an installed
# projectM 4 (Homebrew, vcpkg, distro) or downloads and builds it.
option(QIYAA_WITH_MILKDROP "Milkdrop visualization window (projectM, needs OpenGL 3.3)" ON)
option(QIYAA_FETCH_PROJECTM "Download and build projectM if it isn't installed" ON)
option(QIYAA_REQUIRE_MILKDROP "Stop with an error instead of building without Milkdrop" OFF)
set(QIYAA_HAVE_MILKDROP OFF)
if(QIYAA_WITH_MILKDROP)
    find_package(Qt6 6.4 QUIET COMPONENTS OpenGL)
    find_package(projectM4 4.1 QUIET)
    if(NOT TARGET libprojectM::projectM AND QIYAA_FETCH_PROJECTM AND TARGET Qt6::OpenGL)
        include(FetchContent)
        set(ENABLE_PLAYLIST OFF CACHE BOOL "" FORCE)  # QiYaa keeps its own preset list
        set(ENABLE_SDL_UI OFF CACHE BOOL "" FORCE)
        set(ENABLE_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)  # it would also rename our Debug libraries
        FetchContent_Declare(projectm
            GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
            GIT_TAG v4.1.7
            GIT_SHALLOW TRUE)
        # projectM takes its library version from the top-level project (ours).
        set(_qiyaa_version ${CMAKE_PROJECT_VERSION})
        set(CMAKE_PROJECT_VERSION 4.1.7)
        FetchContent_MakeAvailable(projectm)
        set(CMAKE_PROJECT_VERSION ${_qiyaa_version})
        unset(BUILD_SHARED_LIBS CACHE)  # projectM's option; keep it from applying to anything else
        set(QIYAA_PROJECTM_FETCHED ON)
        configure_file(${projectm_SOURCE_DIR}/LICENSE.txt ${CMAKE_BINARY_DIR}/projectM-LICENSE.txt COPYONLY)
    endif()
    if(TARGET Qt6::OpenGL AND TARGET libprojectM::projectM)
        target_sources(qiyaa_vis PRIVATE src/vis/milkdrop_view.cpp src/vis/milkdrop_view.h)
        target_sources(qiyaa_ui PRIVATE src/ui/milkdrop_window.cpp src/ui/milkdrop_window.h)
        target_link_libraries(qiyaa_vis PUBLIC Qt6::OpenGL PRIVATE libprojectM::projectM)
        target_compile_definitions(qiyaa_vis PUBLIC QIYAA_HAVE_MILKDROP)
        file(GLOB QIYAA_MILK_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/milkdrop/*.milk)
        qt_add_resources(qiyaa_vis "milkdrop" PREFIX "/milkdrop" BASE resources/milkdrop FILES ${QIYAA_MILK_PRESETS})
        set(QIYAA_HAVE_MILKDROP ON)
        list(LENGTH QIYAA_MILK_PRESETS _n)
        message(STATUS "Milkdrop: enabled (${_n} built-in presets)")
    elseif(NOT TARGET Qt6::OpenGL)
        message(STATUS "Milkdrop: disabled (Qt6::OpenGL not found)")
    else()
        message(STATUS "Milkdrop: disabled (projectM 4 not found; QIYAA_FETCH_PROJECTM=ON downloads it)")
    endif()
endif()
if(QIYAA_REQUIRE_MILKDROP AND NOT QIYAA_HAVE_MILKDROP)
    message(FATAL_ERROR "Milkdrop was required but can't be built (see above)")
endif()

# Telemetry: anonymous usage statistics and crash reports (spec/telemetry), only in builds made
# with it; the releases have it. The app sends them to QIYAA_TELEMETRY_URL.
option(QIYAA_WITH_TELEMETRY "Usage statistics and crash reports (spec/telemetry)" OFF)
set(QIYAA_TELEMETRY_URL "${QIYAA_JAM_URL}/api/telemetry" CACHE STRING "Where telemetry goes")
set(QIYAA_PACKAGE "source" CACHE STRING "How this build is shipped: deb, appimage, windows, macos, source")
set(QIYAA_HAVE_TELEMETRY OFF)
if(QIYAA_WITH_TELEMETRY)
    qiyaa_add_module(telemetry
        src/telemetry/crash_handler.cpp src/telemetry/crash_handler.h
        src/telemetry/machine_id.cpp    src/telemetry/machine_id.h
        src/telemetry/reporter.cpp      src/telemetry/reporter.h
        src/telemetry/system_info.cpp   src/telemetry/system_info.h)
    target_link_libraries(qiyaa_telemetry PUBLIC Qt6::Core Qt6::Network PRIVATE Qt6::Gui)
    target_compile_definitions(qiyaa_telemetry PUBLIC QIYAA_HAVE_TELEMETRY
        PRIVATE QIYAA_PACKAGE="${QIYAA_PACKAGE}")
    if(WIN32)
        target_link_libraries(qiyaa_telemetry PRIVATE dbghelp)
    else()
        target_link_libraries(qiyaa_telemetry PRIVATE ${CMAKE_DL_LIBS})
    endif()
    set(QIYAA_HAVE_TELEMETRY ON)
    message(STATUS "Telemetry: enabled (${QIYAA_TELEMETRY_URL}, package ${QIYAA_PACKAGE})")
else()
    message(STATUS "Telemetry: disabled (QIYAA_WITH_TELEMETRY=OFF)")
endif()

qiyaa_add_module(app
    src/app/account.cpp         src/app/account.h
    src/app/application.cpp     src/app/application.h
    src/app/jam_bridge.h
    src/app/main_menu.cpp       src/app/main_menu.h
    src/app/network_watch.cpp   src/app/network_watch.h
    src/app/offline_sources.cpp src/app/offline_sources.h
    src/app/paths.cpp           src/app/paths.h
    src/app/settings.cpp        src/app/settings.h
    src/app/spotify_account.cpp src/app/spotify_account.h
    src/app/telemetry_bridge.h
    src/app/translations.cpp    src/app/translations.h
    src/app/window_set.cpp      src/app/window_set.h)
target_link_libraries(qiyaa_app
    PUBLIC qiyaa_audio qiyaa_core qiyaa_lrclib qiyaa_skins qiyaa_sources_local qiyaa_sources_yandex qiyaa_spotify_client qiyaa_yandex
    PRIVATE qiyaa_integrations qiyaa_ui qiyaa_vis)
# App::Settings holds the default of jam/server, also in a build without the jam.
target_compile_definitions(qiyaa_app PRIVATE QIYAA_JAM_URL="${QIYAA_JAM_URL}")
# The jam's part of the app: jam_bridge.cpp in a build with the jam, else jam_off.cpp, which does nothing.
if(QIYAA_HAVE_JAM)
    target_sources(qiyaa_app PRIVATE src/app/jam_bridge.cpp)
    target_link_libraries(qiyaa_app PRIVATE qiyaa_jam)
else()
    target_sources(qiyaa_app PRIVATE src/app/jam_off.cpp)
endif()
# Telemetry: telemetry_bridge.cpp in a telemetry build, else telemetry_off.cpp, which does nothing.
if(QIYAA_HAVE_TELEMETRY)
    target_sources(qiyaa_app PRIVATE src/app/telemetry_bridge.cpp)
    target_link_libraries(qiyaa_app PRIVATE qiyaa_telemetry)
else()
    target_sources(qiyaa_app PRIVATE src/app/telemetry_off.cpp)
endif()
# The interface in Belarusian, Russian and English (the source language; its file holds the
# plural forms). translations/README.md says how to update the .ts files.
qt_add_translations(qiyaa_app
    TS_FILES translations/qiyaa_be.ts translations/qiyaa_en.ts translations/qiyaa_ru.ts
    RESOURCE_PREFIX /i18n)
qt_add_resources(qiyaa_app "icons"
    PREFIX "/icons"
    BASE resources/icons
    FILES
        resources/icons/qiyaa-16.png
        resources/icons/qiyaa-24.png
        resources/icons/qiyaa-32.png
        resources/icons/qiyaa-48.png
        resources/icons/qiyaa-64.png
        resources/icons/qiyaa-128.png
        resources/icons/qiyaa-256.png)

set(QIYAA_APP_SOURCES src/app/main.cpp)
if(WIN32)
    # Icon and version info for Explorer.
    configure_file(packaging/windows/qiyaa.rc.in ${CMAKE_CURRENT_BINARY_DIR}/qiyaa.rc @ONLY)
    list(APPEND QIYAA_APP_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/qiyaa.rc)
elseif(APPLE)
    set_source_files_properties(resources/icons/qiyaa.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND QIYAA_APP_SOURCES resources/icons/qiyaa.icns)
endif()
qt_add_executable(qiyaa WIN32 MACOSX_BUNDLE ${QIYAA_APP_SOURCES})
target_link_libraries(qiyaa PRIVATE qiyaa_app qiyaa_ui)
qiyaa_target_defaults(qiyaa)
target_compile_definitions(qiyaa PRIVATE QIYAA_VERSION="${PROJECT_VERSION}")
if(QIYAA_HAVE_TELEMETRY)
    target_link_libraries(qiyaa PRIVATE qiyaa_telemetry)
    target_compile_definitions(qiyaa PRIVATE QIYAA_TELEMETRY_URL="${QIYAA_TELEMETRY_URL}")
endif()
set_target_properties(qiyaa PROPERTIES
    OUTPUT_NAME QiYaa
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.kickoman.qiyaa
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE qiyaa.icns)

if(UNIX AND NOT APPLE)
    # FHS layout: used for the AppImage (cmake --install ... --prefix AppDir/usr) and distro packages.
    include(GNUInstallDirs)
    install(TARGETS qiyaa RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(QIYAA_PROJECTM_FETCHED)
        # A downloaded projectM ships with the app, by default in a private
        # directory so it can't clash with a distribution's own projectM package.
        if(NOT DEFINED QIYAA_BUNDLED_LIBDIR)
            set(QIYAA_BUNDLED_LIBDIR ${CMAKE_INSTALL_LIBDIR}/qiyaa)
        endif()
        install(TARGETS projectM LIBRARY DESTINATION ${QIYAA_BUNDLED_LIBDIR} NAMELINK_SKIP)
        install(FILES ${projectm_SOURCE_DIR}/LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qiyaa
                RENAME projectM-LICENSE.txt)
        set_target_properties(qiyaa PROPERTIES INSTALL_RPATH "$ORIGIN/../${QIYAA_BUNDLED_LIBDIR}")
    endif()

    # .deb package (cpack -G DEB), built against the distribution's Qt: for
    # Ubuntu 24.04+ / Debian 13+. Dependencies come from dpkg-shlibdeps, plus
    # what Qt and miniaudio load at run time.
    set(CPACK_PACKAGE_NAME qiyaa)
    set(CPACK_PACKAGE_VENDOR "QiYaa contributors")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Winamp-style player for Yandex Music")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Kickoman/QiYaa")
    set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
    set(QIYAA_DEB_MAINTAINER "Kickoman <Kickoman@users.noreply.github.com>" CACHE STRING "Maintainer field of the .deb")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER ${QIYAA_DEB_MAINTAINER})
    set(CPACK_DEBIAN_PACKAGE_SECTION sound)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)  # qiyaa_<version>_<arch>.deb
    # The first line of the control file's Description comes from the summary above.
    set(CPACK_DEBIAN_PACKAGE_DESCRIPTION "QiYaa plays Yandex Music (My Wave, likes, playlists, search) in the classic
Winamp 2 look: .wsz skins, docking equalizer and playlist windows, spectrum
and Milkdrop visualizations, media keys (MPRIS).")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    if(QIYAA_PROJECTM_FETCHED)
        # Our private projectM: dpkg-shlibdeps must not look for a package providing it.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS_PRIVATE_DIRS ${projectm_BINARY_DIR}/src/libprojectM)
    endif()
    # Loaded at run time, invisible to dpkg-shlibdeps: the X11 platform plugin
    # (the app runs through XWayland), the sound server's client library
    # (miniaudio opens it itself) and, for QIYAA_NATIVE_WAYLAND=1, Qt's Wayland plugin.
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins")
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libpulse0")
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "qt6-wayland")
    set(CPACK_GENERATOR DEB)
    include(CPack)
    install(FILES packaging/linux/qiyaa.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES resources/icons/qiyaa-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME qiyaa.png)
    endforeach()
    # License texts where Debian looks for them (/usr/share/doc/<package>/copyright).
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qiyaa RENAME copyright)
    install(FILES THIRD_PARTY.md DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qiyaa)
    # Its license must come with every binary (THIRD_PARTY.md).
    install(FILES contrib/fdk-aac/LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qiyaa
            RENAME fdk-aac-LICENSE.txt)
    install(FILES ${CMAKE_BINARY_DIR}/taglib-LICENSE.txt DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/qiyaa)
else()
    install(TARGETS qiyaa BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()

if(QIYAA_BUILD_TESTS)
    find_package(Qt6 6.4 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Qt6::Test not found, tests disabled")
    endif()
endif()
