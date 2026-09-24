cmake_minimum_required(VERSION 3.21)

project(QiYaa
    VERSION 0.0.1
    DESCRIPTION "Winamp-style Yandex Music player (C++/Qt)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(QIYAA_BUILD_TESTS "Build unit tests" ON)

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
    src/app/Paths.cpp            src/app/Paths.h
    src/skin/Skin.cpp            src/skin/Skin.h
    src/skin/SkinSprites.h
    src/skin/Region.cpp          src/skin/Region.h
    src/ui/Snap.cpp              src/ui/Snap.h
    src/ui/SkinnedWindow.cpp     src/ui/SkinnedWindow.h
    src/ui/MainWindow.cpp        src/ui/MainWindow.h
    src/yandex/TrackUrl.cpp      src/yandex/TrackUrl.h
    src/yandex/ApiClient.cpp     src/yandex/ApiClient.h
    src/yandex/Token.cpp         src/yandex/Token.h
    src/audio/AudioEngine.cpp    src/audio/AudioEngine.h
    src/core/Player.cpp          src/core/Player.h
)
target_include_directories(qiyaa_core PUBLIC src)
target_link_libraries(qiyaa_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network
    qiyaa_miniz qiyaa_miniaudio)
target_compile_definitions(qiyaa_core PUBLIC QT_NO_KEYWORDS)

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

# ---------------------------------------------------------------- application
qt_add_executable(qiyaa WIN32 MACOSX_BUNDLE src/app/main.cpp)
target_link_libraries(qiyaa PRIVATE qiyaa_core)
target_compile_definitions(qiyaa PRIVATE QIYAA_VERSION="${PROJECT_VERSION}")
set_target_properties(qiyaa PROPERTIES
    OUTPUT_NAME QiYaa
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.kickoman.qiyaa
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})

install(TARGETS qiyaa BUNDLE DESTINATION . RUNTIME DESTINATION bin)

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
