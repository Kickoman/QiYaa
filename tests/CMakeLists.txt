# The API fixtures and expected results come from the spec/ submodule.
set(QIYAA_SPEC_DIR "${PROJECT_SOURCE_DIR}/spec")
if(NOT EXISTS "${QIYAA_SPEC_DIR}/fixtures/yandex")
    message(FATAL_ERROR "spec/ is empty; run: git submodule update --init")
endif()

add_library(qiyaa_test_support STATIC
    support/mock_http_server.cpp support/mock_http_server.h
    support/spec_fixtures.cpp support/spec_fixtures.h)
target_include_directories(qiyaa_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qiyaa_test_support PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(qiyaa_test_support PRIVATE QIYAA_SPEC_DIR="${QIYAA_SPEC_DIR}")
qiyaa_target_defaults(qiyaa_test_support)

# The Yandex models in the neutral JSON of spec/expected/yandex.
add_library(qiyaa_test_yandex STATIC support/yandex_json.cpp support/yandex_json.h)
target_link_libraries(qiyaa_test_yandex PUBLIC qiyaa_test_support qiyaa_yandex)
qiyaa_target_defaults(qiyaa_test_yandex)

# qiyaa_add_test(<name> <modules...>): <name>.cpp linked with qiyaa_<module> for each module.
function(qiyaa_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    list(TRANSFORM ARGN PREPEND qiyaa_ OUTPUT_VARIABLE modules)
    target_link_libraries(${name} PRIVATE ${modules} qiyaa_test_support Qt6::Test)
    qiyaa_target_defaults(${name})
    add_test(NAME ${name} COMMAND ${name})
    # Tests need no display, and no sound card: miniaudio's Null output runs
    # in real time like a device but doesn't depend on the machine's audio
    # (the macOS runner has Core Audio, and its timing in the VM is unreliable).
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QIYAA_AUDIO_BACKEND=null")
endfunction()

qiyaa_add_test(snap_test ui)
qiyaa_add_test(region_test skins)
qiyaa_add_test(yandex_test yandex)
target_link_libraries(yandex_test PRIVATE qiyaa_test_yandex)
qiyaa_add_test(skin_test skins)
qiyaa_add_test(audio_test audio core)
target_compile_definitions(audio_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(main_window_test audio core skins ui yandex)
qiyaa_add_test(library_test audio core yandex)
target_link_libraries(library_test PRIVATE qiyaa_test_yandex)
target_compile_definitions(library_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(track_navigator_test core)
qiyaa_add_test(sources_test audio core yandex)
target_link_libraries(sources_test PRIVATE qiyaa_test_yandex)
target_compile_definitions(sources_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(jam_mode_test audio core yandex)
target_compile_definitions(jam_mode_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(failures_test audio core yandex)
target_compile_definitions(failures_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(dsp_test audio skins ui vis)
qiyaa_add_test(windows_test app core ui)
qiyaa_add_test(screenshots_test audio core skins ui yandex)
qiyaa_add_test(translations_test app ui)
target_compile_definitions(translations_test PRIVATE QIYAA_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_compile_definitions(screenshots_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
# A realistic 2560x1440 virtual screen (the offscreen default is 800x600).
# Not on Windows: Qt splits platform arguments on ':', which breaks "C:/...".
if(NOT WIN32)
    set_tests_properties(windows_test PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/data/screen-2560x1440.json;QIYAA_AUDIO_BACKEND=null")
endif()

# The jam: only where it's built (Qt WebSockets). The client talks to a QWebSocketServer on localhost.
if(QIYAA_HAVE_JAM)
    add_library(qiyaa_test_jam STATIC support/jam_stub_server.cpp support/jam_stub_server.h)
    target_link_libraries(qiyaa_test_jam PUBLIC qiyaa_test_support qiyaa_jam)
    qiyaa_target_defaults(qiyaa_test_jam)
    qiyaa_add_test(jam_test jam)
    target_link_libraries(jam_test PRIVATE qiyaa_test_jam)
    qiyaa_add_test(jam_host_test audio core jam yandex)
    target_link_libraries(jam_host_test PRIVATE qiyaa_test_jam)
    target_compile_definitions(jam_host_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    qiyaa_add_test(jam_window_test app audio core jam skins ui yandex)
    target_link_libraries(jam_window_test PRIVATE qiyaa_test_jam)
    target_compile_definitions(jam_window_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
    # Against a real jam server: QIYAA_JAM_TEST_SERVER (its PUBLIC_URL), or the test skips.
    qiyaa_add_test(jam_e2e_test app audio core jam ui yandex)
    target_compile_definitions(jam_e2e_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
endif()

# MPRIS: only where it's built (Linux with Qt D-Bus; macOS Qt has D-Bus too,
# but no MPRIS). It needs a session bus, so CTest starts a private one;
# without dbus-run-session the test skips itself.
if(QIYAA_HAVE_MPRIS)
    qt_add_executable(mpris_test mpris_test.cpp)
    target_link_libraries(mpris_test PRIVATE qiyaa_core qiyaa_integrations Qt6::Test Qt6::DBus)
    qiyaa_target_defaults(mpris_test)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_test(NAME mpris_test COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:mpris_test>)
    else()
        add_test(NAME mpris_test COMMAND mpris_test)
    endif()
    set_tests_properties(mpris_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QIYAA_AUDIO_BACKEND=null")
endif()

# Milkdrop: rendering needs an OpenGL 3.3 context. The offscreen platform has
# none, so on Linux (with Qt's xcb plugin and Xvfb) the rendering tests also run
# on a virtual X server with Mesa; elsewhere they skip themselves.
if(QIYAA_HAVE_MILKDROP)
    qiyaa_add_test(milkdrop_test audio skins ui vis)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN AND TARGET Qt6::QXcbIntegrationPlugin)
        add_test(NAME milkdrop_gl_test COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x1024x24 +extension GLX"
                 $<TARGET_FILE:milkdrop_test> rendersWithProjectM blackPictureIsNoticed fullScreenTakesOverRenderingAndGivesItBack)
        # Fail instead of skipping: here OpenGL must work.
        set_tests_properties(milkdrop_gl_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;QIYAA_EXPECT_GL=1;LIBGL_ALWAYS_SOFTWARE=1;QIYAA_AUDIO_BACKEND=null")
    endif()
endif()
