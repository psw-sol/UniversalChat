cmake_minimum_required(VERSION 3.20)

find_package(GTest CONFIG REQUIRED)

# Test executable
add_executable(chat_server_tests
    test_main.cpp
    test_rate_limiter.cpp
    test_session.cpp
    test_password_hasher.cpp
    test_integration.cpp
    test_pubsub_message.cpp
    test_channel_pubsub.cpp
    test_session_registry.cpp
    test_channel_registry.cpp
    test_scale_out_integration.cpp
)

target_include_directories(chat_server_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(chat_server_tests PRIVATE
    chat_server_lib
    GTest::gtest
    GTest::gtest_main
)

# Register tests with CTest
include(GoogleTest)
gtest_discover_tests(chat_server_tests)
