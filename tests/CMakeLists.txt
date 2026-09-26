add_library(qiyaa_test_support STATIC support/mock_http_server.cpp support/mock_http_server.h)
target_include_directories(qiyaa_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qiyaa_test_support PUBLIC Qt6::Core Qt6::Network)

function(qiyaa_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE qiyaa_core qiyaa_test_support Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Tests need no display, and no sound card: miniaudio's Null output runs
    # in real time like a device but doesn't depend on the machine's audio
    # (the macOS runner has Core Audio, and its timing in the VM is unreliable).
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QIYAA_AUDIO_BACKEND=null")
endfunction()

qiyaa_add_test(snap_test)
qiyaa_add_test(region_test)
qiyaa_add_test(yandex_test)
qiyaa_add_test(skin_test)
qiyaa_add_test(audio_test)
target_compile_definitions(audio_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(main_window_test)
qiyaa_add_test(library_test)
target_compile_definitions(library_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(dsp_test)
qiyaa_add_test(windows_test)
qiyaa_add_test(screenshots_test)
target_compile_definitions(screenshots_test PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
# A realistic 2560x1440 virtual screen (the offscreen default is 800x600).
# Not on Windows: Qt splits platform arguments on ':', which breaks "C:/...".
if(NOT WIN32)
    set_tests_properties(windows_test PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/data/screen-2560x1440.json;QIYAA_AUDIO_BACKEND=null")
endif()

# MPRIS: only where it's built (Linux with Qt D-Bus; macOS Qt has D-Bus too,
# but no MPRIS). It needs a session bus, so CTest starts a private one;
# without dbus-run-session the test skips itself.
if(QIYAA_HAVE_MPRIS)
    qt_add_executable(mpris_test mpris_test.cpp)
    target_link_libraries(mpris_test PRIVATE qiyaa_core Qt6::Test Qt6::DBus)
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
    qiyaa_add_test(milkdrop_test)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN AND TARGET Qt6::QXcbIntegrationPlugin)
        add_test(NAME milkdrop_gl_test COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x1024x24 +extension GLX"
                 $<TARGET_FILE:milkdrop_test> rendersWithProjectM blackPictureIsNoticed fullScreenAndBack)
        # Fail instead of skipping: here OpenGL must work.
        set_tests_properties(milkdrop_gl_test PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;QIYAA_EXPECT_GL=1;LIBGL_ALWAYS_SOFTWARE=1;QIYAA_AUDIO_BACKEND=null")
    endif()
endif()
