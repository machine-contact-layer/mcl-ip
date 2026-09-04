cmake_minimum_required(VERSION 3.16)
project(mcl_ip LANGUAGES C)

option(MCL_IP_BUILD_TESTS "Build host-side reference tests" ON)
option(MCL_IP_BUILD_TOOLS "Build host tools that require real hardware" OFF)

set(MCL_WIRE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-wire" CACHE PATH "Path to mcl-wire repository")

set(MCL_LINK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mcl-link" CACHE PATH "Path to mcl-link repository")

if(NOT EXISTS "${MCL_LINK_DIR}/include/mcl/link.h")
    message(FATAL_ERROR
        "mcl-ip requires mcl-link. Set MCL_LINK_DIR to the mcl-link checkout.")
endif()

# The binding carries MCL Link frames, so it depends on Link and on nothing else.
# There are deliberately no sockets here: how bytes reach the network is the
# integrator's decision, and a binding must not become a network stack.
add_library(mcl_ip STATIC
    src/ip_binding.c
    "${MCL_LINK_DIR}/src/link.c"
    "${MCL_LINK_DIR}/src/rendezvous.c")
target_include_directories(mcl_ip PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    "${MCL_LINK_DIR}/include")
set_target_properties(mcl_ip PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS NO)

if(MCL_IP_BUILD_TESTS)
    enable_testing()

    add_executable(mcl_ip_test_binding tests/test_ip_binding.c)
    target_link_libraries(mcl_ip_test_binding PRIVATE mcl_ip)
    set_target_properties(mcl_ip_test_binding PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    add_test(NAME mcl_ip_binding COMMAND mcl_ip_test_binding)
endif()

# The over-air peer needs a radio, a network and a second machine, so it is a
# tool rather than a test and is off by default. It carries no protocol logic
# of its own: every frame it sends is built by mcl-link and every reply it
# accepts is checked by mcl-ip, so what runs over the air is what ships.
if(MCL_IP_BUILD_TOOLS)
    if(NOT EXISTS "${MCL_WIRE_DIR}/include/mcl/wire.h")
        message(FATAL_ERROR
            "MCL_IP_BUILD_TOOLS requires mcl-wire. Set MCL_WIRE_DIR to the mcl-wire checkout.")
    endif()

    add_executable(mcl_ip_udp_over_air_peer
        tools/udp_over_air_peer.c
        "${MCL_WIRE_DIR}/src/wire.c")
    target_link_libraries(mcl_ip_udp_over_air_peer PRIVATE mcl_ip)
    target_include_directories(mcl_ip_udp_over_air_peer PRIVATE "${MCL_WIRE_DIR}/include")
    set_target_properties(mcl_ip_udp_over_air_peer PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    if(WIN32)
        target_link_libraries(mcl_ip_udp_over_air_peer PRIVATE ws2_32)
    endif()

    # The responding half. It exists so that the peer answering an over-air run
    # does not have to be the ESP32 sketch: any machine with a socket can be it,
    # which is what makes an Android or second-laptop peer possible at all.
    # contact.c is linked here and not into mcl_ip: the binding does not own
    # contact state and must not start doing so. The responder needs it because
    # it judges a TRANSPORT_OFFER with the library's rule rather than its own.
    add_executable(mcl_ip_udp_over_air_responder
        tools/udp_over_air_responder.c
        "${MCL_WIRE_DIR}/src/wire.c"
        "${MCL_LINK_DIR}/src/contact.c")
    target_link_libraries(mcl_ip_udp_over_air_responder PRIVATE mcl_ip)
    target_include_directories(mcl_ip_udp_over_air_responder PRIVATE "${MCL_WIRE_DIR}/include")
    set_target_properties(mcl_ip_udp_over_air_responder PROPERTIES
        C_STANDARD 99
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO)
    if(WIN32)
        target_link_libraries(mcl_ip_udp_over_air_responder PRIVATE ws2_32)
    endif()
endif()
