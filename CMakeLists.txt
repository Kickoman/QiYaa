cmake_minimum_required(VERSION 3.21)

project(QiYaa
    VERSION 0.2.0
    DESCRIPTION "Winamp-style Yandex Music player (C++/Qt)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QIYAA_BUILD_TESTS "Build unit tests" ON)

# For packaging scripts (CI names the installers after it).
file(WRITE ${CMAKE_BINARY_DIR}/version.txt "${PROJECT_VERSION}")

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network)
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

# One static library per folder of src/. Dependencies point one way:
#   audio, yandex, skins  ->  vis, core  ->  ui, integrations  ->  app  ->  qiyaa (main)
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
    src/audio/error.h
    src/audio/eq_presets.cpp    src/audio/eq_presets.h
    src/audio/equalizer.cpp     src/audio/equalizer.h
    src/audio/vis_tap.cpp       src/audio/vis_tap.h)
target_link_libraries(qiyaa_audio PUBLIC Qt6::Core PRIVATE qiyaa_miniaudio)

qiyaa_add_module(yandex
    src/yandex/api_client.cpp   src/yandex/api_client.h
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
    src/vis/visualizer.h        src/vis/visualizer.cpp)
target_link_libraries(qiyaa_vis PUBLIC Qt6::Gui PRIVATE qiyaa_audio qiyaa_skins)

qiyaa_add_module(core
    src/core/cover_cache.cpp    src/core/cover_cache.h
    src/core/player.cpp         src/core/player.h)
target_link_libraries(qiyaa_core PUBLIC qiyaa_audio qiyaa_yandex Qt6::Gui Qt6::Network)

qiyaa_add_module(ui
    src/ui/equalizer_window.cpp src/ui/equalizer_window.h
    src/ui/gen_window.cpp       src/ui/gen_window.h
    src/ui/library_menu.cpp     src/ui/library_menu.h
    src/ui/login_dialog.cpp     src/ui/login_dialog.h
    src/ui/main_window.cpp      src/ui/main_window.h
    src/ui/now_playing_window.cpp src/ui/now_playing_window.h
    src/ui/playlist_window.cpp  src/ui/playlist_window.h
    src/ui/skinned_window.cpp   src/ui/skinned_window.h
    src/ui/snap.cpp             src/ui/snap.h)
target_link_libraries(qiyaa_ui
    PUBLIC qiyaa_audio qiyaa_vis Qt6::Widgets
    PRIVATE qiyaa_core qiyaa_skins qiyaa_yandex)

qiyaa_add_module(integrations
    src/integrations/media_controls.cpp src/integrations/media_controls.h)
target_link_libraries(qiyaa_integrations PUBLIC Qt6::Gui PRIVATE qiyaa_audio qiyaa_core qiyaa_yandex)

# System media integration: MPRIS on Linux/BSD (needs Qt D-Bus), SMTC on Windows.
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

qiyaa_add_module(app
    src/app/application.cpp     src/app/application.h
    src/app/offline_sources.cpp src/app/offline_sources.h
    src/app/paths.cpp           src/app/paths.h)
target_link_libraries(qiyaa_app
    PUBLIC qiyaa_audio qiyaa_core qiyaa_skins qiyaa_yandex
    PRIVATE qiyaa_integrations qiyaa_ui qiyaa_vis)
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
