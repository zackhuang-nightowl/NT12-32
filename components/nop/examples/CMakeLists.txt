set(NOP_EX_LINK nopcore_static)
if(NOT NOP_BUILD_STATIC)
    set(NOP_EX_LINK nopcore_shared)
endif()

add_executable(ipc_demo ipc_demo.c)
# Public API only, plus ports/stub for the demo HAL convenience header.
target_include_directories(ipc_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ports/stub)
target_link_libraries(ipc_demo PRIVATE ${NOP_EX_LINK})

# NOP client (UI process) demo: loopback + unix-socket channels.
add_executable(ui_client_demo ui_client_demo.c)
target_include_directories(ui_client_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ports/stub)
target_link_libraries(ui_client_demo PRIVATE ${NOP_EX_LINK})
find_package(Threads REQUIRED)
target_link_libraries(ui_client_demo PRIVATE Threads::Threads)
if(NOP_BUILD_TESTS)
    add_test(NAME ui_client_demo COMMAND ui_client_demo)
endif()

# NVR (videoRecorder) integration skeleton: NVR-role app + multi-channel
# recording manager over the stub HAL. Smoke test drives a short sequence.
add_executable(nvr_demo nvr_demo.c)
target_include_directories(nvr_demo PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/ports/stub)
target_link_libraries(nvr_demo PRIVATE ${NOP_EX_LINK})
if(NOP_BUILD_TESTS)
    add_test(NAME nvr_smoke COMMAND nvr_demo 3)
endif()

if(NOP_WITH_ONVIF)
    add_executable(onvif_client_demo onvif_client_demo.c)
    target_include_directories(onvif_client_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(onvif_client_demo PRIVATE ${NOP_EX_LINK})
    # Find the vendored OpenSSL at runtime without LD_LIBRARY_PATH (dev convenience).
    set_target_properties(onvif_client_demo PROPERTIES
        BUILD_RPATH ${CMAKE_SOURCE_DIR}/third_party/onvif/openssl/lib/linux)
endif()

if(NOP_WITH_ONVIF_SERVER)
    add_executable(onvif_server_demo onvif_server_demo.c)
    target_include_directories(onvif_server_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(onvif_server_demo PRIVATE ${NOP_EX_LINK})
    set_target_properties(onvif_server_demo PROPERTIES
        BUILD_RPATH ${CMAKE_SOURCE_DIR}/third_party/onvif_server/openssl/lib/linux)
    # Smoke test: start the server (binds, brings SOAP services up), then stop.
    if(NOP_BUILD_TESTS)
        add_test(NAME onvif_server_smoke COMMAND onvif_server_demo 1 8081)
    endif()
endif()
