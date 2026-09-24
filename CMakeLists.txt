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

# ---------------------------------------------------------------- third party
add_library(qiyaa_miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c)
target_include_directories(qiyaa_miniz PUBLIC third_party/miniz)
target_compile_definitions(qiyaa_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES MINIZ_NO_TIME)
set_target_properties(qiyaa_miniz PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(qiyaa_miniaudio STATIC src/audio/miniaudio_impl.c)
target_include_directories(qiyaa_miniaudio PUBLIC third_party/miniaudio)
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

# ---------------------------------------------------------------- core library
# Everything except main() lives here so that tests can link against it.
add_library(qiyaa_core STATIC
    src/app/App.cpp              src/app/App.h
    src/app/Paths.cpp            src/app/Paths.h
    src/skin/Skin.cpp            src/skin/Skin.h
    src/skin/SkinSprites.h
    src/skin/Region.cpp          src/skin/Region.h
    src/ui/Snap.cpp              src/ui/Snap.h
    src/ui/SkinnedWindow.cpp     src/ui/SkinnedWindow.h
    src/ui/MainWindow.cpp        src/ui/MainWindow.h
    src/ui/EqualizerWindow.cpp   src/ui/EqualizerWindow.h
    src/ui/PlaylistWindow.cpp    src/ui/PlaylistWindow.h
    src/ui/LibraryMenu.cpp       src/ui/LibraryMenu.h
    src/ui/LoginDialog.cpp       src/ui/LoginDialog.h
    src/ui/GenWindow.cpp         src/ui/GenWindow.h
    src/ui/NowPlayingWindow.cpp  src/ui/NowPlayingWindow.h
    src/vis/Visualizer.h         src/vis/Visualizers.cpp
    src/yandex/TrackUrl.cpp      src/yandex/TrackUrl.h
    src/yandex/ApiClient.cpp     src/yandex/ApiClient.h
    src/yandex/Library.cpp       src/yandex/Library.h
    src/yandex/OAuth.cpp         src/yandex/OAuth.h
    src/yandex/Token.cpp         src/yandex/Token.h
    src/audio/AudioEngine.cpp    src/audio/AudioEngine.h
    src/audio/Equalizer.cpp      src/audio/Equalizer.h
    src/audio/VisTap.h
    src/audio/EqPresets.cpp      src/audio/EqPresets.h
    src/core/Player.cpp          src/core/Player.h
    src/core/CoverCache.cpp      src/core/CoverCache.h
    src/integrations/MediaControls.cpp src/integrations/MediaControls.h
    src/vis/MilkdropPresets.cpp  src/vis/MilkdropPresets.h
)
target_include_directories(qiyaa_core PUBLIC src)
target_link_libraries(qiyaa_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    qiyaa_miniz qiyaa_miniaudio)
target_compile_definitions(qiyaa_core PUBLIC QT_NO_KEYWORDS)

# System media integration: MPRIS on Linux/BSD (needs Qt D-Bus), SMTC on Windows.
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        target_sources(qiyaa_core PRIVATE src/integrations/Mpris.cpp src/integrations/Mpris.h)
        target_link_libraries(qiyaa_core PUBLIC Qt6::DBus)
        target_compile_definitions(qiyaa_core PUBLIC QIYAA_HAVE_MPRIS)
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
            target_sources(qiyaa_core PRIVATE src/integrations/Smtc.cpp src/integrations/Smtc.h)
            target_link_libraries(qiyaa_core PUBLIC windowsapp)
            target_compile_definitions(qiyaa_core PUBLIC QIYAA_HAVE_SMTC)
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
        FetchContent_Declare(projectm
            GIT_REPOSITORY https://github.com/projectM-visualizer/projectm.git
            GIT_TAG v4.1.7
            GIT_SHALLOW TRUE)
        FetchContent_MakeAvailable(projectm)
        set(QIYAA_PROJECTM_FETCHED ON)
    endif()
    if(TARGET Qt6::OpenGL AND TARGET libprojectM::projectM)
        target_sources(qiyaa_core PRIVATE
            src/vis/MilkdropView.cpp     src/vis/MilkdropView.h
            src/ui/MilkdropWindow.cpp    src/ui/MilkdropWindow.h)
        target_link_libraries(qiyaa_core PUBLIC Qt6::OpenGL libprojectM::projectM)
        target_compile_definitions(qiyaa_core PUBLIC QIYAA_HAVE_MILKDROP)
        file(GLOB QIYAA_MILK_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/resources/milkdrop/*.milk)
        qt_add_resources(qiyaa_core "milkdrop" PREFIX "/milkdrop" BASE resources/milkdrop FILES ${QIYAA_MILK_PRESETS})
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

if(MSVC)
    target_compile_options(qiyaa_core PRIVATE /W4 /utf-8)
else()
    target_compile_options(qiyaa_core PRIVATE -Wall -Wextra)
endif()

qt_add_resources(qiyaa_core "skins"
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

qt_add_resources(qiyaa_core "icons"
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

# ---------------------------------------------------------------- application
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
target_link_libraries(qiyaa PRIVATE qiyaa_core)
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
        # A downloaded projectM ships with the app (and is found next to it).
        install(TARGETS projectM LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} NAMELINK_SKIP)
        set_target_properties(qiyaa PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(FILES packaging/linux/qiyaa.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    foreach(size 16 24 32 48 64 128 256 512)
        install(FILES resources/icons/qiyaa-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps RENAME qiyaa.png)
    endforeach()
else()
    install(TARGETS qiyaa BUNDLE DESTINATION . RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------- tests
if(QIYAA_BUILD_TESTS)
    find_package(Qt6 6.4 COMPONENTS Test)
    if(Qt6Test_FOUND)
        enable_testing()
        add_subdirectory(tests)
    else()
        message(STATUS "Qt6::Test not found, tests disabled")
    endif()
endif()
