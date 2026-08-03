# Tests link the static lib and reach into private headers (src/, cJSON, stub).
set(NOP_TEST_LINK nopcore_static)
if(NOT NOP_BUILD_STATIC)
    set(NOP_TEST_LINK nopcore_shared)
endif()

set(NOP_TESTS
    test_map
    test_envelope
    test_router
    test_transport
    test_dispatch
    test_media
    test_config
    test_push
    test_snapshot
    test_record
    test_sdcard_upgrade
    test_system_events
    test_nvr_recorder
    test_nvr_channels
    test_nvr_addcamera
    test_wizard
    test_analog_camera
    test_chime)

foreach(t ${NOP_TESTS})
    add_executable(${t} ${t}.c)
    target_include_directories(${t} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${NOP_PRIVATE_INCLUDES})
    target_link_libraries(${t} PRIVATE ${NOP_TEST_LINK})
    add_test(NAME ${t} COMMAND ${t})
endforeach()

# Network tests need threads (server / OTA download run on background threads).
find_package(Threads REQUIRED)
foreach(nt test_http_server test_server_request test_rtsp_server test_event8012 test_light_linkage test_ptz_patrol)
    add_executable(${nt} ${nt}.c)
    target_include_directories(${nt} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR} ${NOP_PRIVATE_INCLUDES})
    target_link_libraries(${nt} PRIVATE ${NOP_TEST_LINK} Threads::Threads)
    add_test(NAME ${nt} COMMAND ${nt})
endforeach()
