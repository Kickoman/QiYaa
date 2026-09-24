function(qiyaa_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE qiyaa_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    # Tests need no display.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

qiyaa_add_test(test_snap)
qiyaa_add_test(test_region)
qiyaa_add_test(test_yandex)
qiyaa_add_test(test_skin)
qiyaa_add_test(test_audio)
target_compile_definitions(test_audio PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(test_mainwindow)
qiyaa_add_test(test_library)
target_compile_definitions(test_library PRIVATE QIYAA_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/data")
qiyaa_add_test(test_dsp)
qiyaa_add_test(test_windows)
# A realistic 2560x1440 virtual screen (the offscreen default is 800x600).
# Not on Windows: Qt splits platform arguments on ':', which breaks "C:/...".
if(NOT WIN32)
    set_tests_properties(test_windows PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen:configfile=${CMAKE_CURRENT_SOURCE_DIR}/data/screen-2560x1440.json")
endif()

# MPRIS: only where it's built (Linux with Qt D-Bus; macOS Qt has D-Bus too,
# but no MPRIS). It needs a session bus, so CTest starts a private one;
# without dbus-run-session the test skips itself.
if(QIYAA_HAVE_MPRIS)
    qt_add_executable(test_mpris test_mpris.cpp)
    target_link_libraries(test_mpris PRIVATE qiyaa_core Qt6::Test Qt6::DBus)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        add_test(NAME test_mpris COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:test_mpris>)
    else()
        add_test(NAME test_mpris COMMAND test_mpris)
    endif()
    set_tests_properties(test_mpris PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Milkdrop: rendering needs an OpenGL 3.3 context. The offscreen platform has
# none, so on Linux (with Qt's xcb plugin and Xvfb) the rendering tests also run
# on a virtual X server with Mesa; elsewhere they skip themselves.
if(QIYAA_HAVE_MILKDROP)
    qiyaa_add_test(test_milkdrop)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN AND TARGET Qt6::QXcbIntegrationPlugin)
        add_test(NAME test_milkdrop_gl COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x1024x24 +extension GLX"
                 $<TARGET_FILE:test_milkdrop> rendersWithProjectM fullScreenAndBack)
        # Fail instead of skipping: here OpenGL must work.
        set_tests_properties(test_milkdrop_gl PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=xcb;QIYAA_EXPECT_GL=1;LIBGL_ALWAYS_SOFTWARE=1")
    endif()
endif()
