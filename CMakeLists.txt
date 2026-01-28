cmake_minimum_required(VERSION 3.20)
project(UniversalChatServer VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options
option(BUILD_TESTS "Build unit tests" ON)
option(ENABLE_REDIS "Enable Redis support" OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include cmake modules
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)
include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

# Protobuf generation
file(GLOB PROTO_FILES ${CMAKE_SOURCE_DIR}/proto/*.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Source files
set(CORE_SOURCES
    src/core/Server.cpp
    src/core/Session.cpp
    src/core/SessionManager.cpp
    src/core/IOContextPool.cpp
)

set(CHANNEL_SOURCES
    src/channel/Channel.cpp
    src/channel/ChannelManager.cpp
    src/channel/WorldChannelManager.cpp
)

set(MESSAGE_SOURCES
    src/message/MessageDispatcher.cpp
)

set(ANNOUNCEMENT_SOURCES
    src/announcement/AnnouncementService.cpp
)

set(NOTIFICATION_SOURCES
    src/notification/UserActionService.cpp
)

set(PROTOCOL_SOURCES
    src/protocol/PacketCodec.cpp
)

set(UTIL_SOURCES
    src/util/Config.cpp
    src/util/Logger.cpp
    src/util/Snowflake.cpp
    src/util/RateLimiter.cpp
    src/util/PasswordHasher.cpp
)

set(ALL_SOURCES
    ${CORE_SOURCES}
    ${CHANNEL_SOURCES}
    ${MESSAGE_SOURCES}
    ${ANNOUNCEMENT_SOURCES}
    ${NOTIFICATION_SOURCES}
    ${PROTOCOL_SOURCES}
    ${UTIL_SOURCES}
    ${PROTO_SRCS}
)

# Library
add_library(chat_server_lib STATIC ${ALL_SOURCES})

target_include_directories(chat_server_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}  # Protobuf generated headers
)

target_link_libraries(chat_server_lib PUBLIC
    Boost::system
    Boost::thread
    protobuf::libprotobuf
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# Platform-specific settings
if(WIN32)
    target_link_libraries(chat_server_lib PUBLIC ws2_32 wsock32 bcrypt)
    target_compile_definitions(chat_server_lib PUBLIC
        _WIN32_WINNT=0x0601
        BOOST_ASIO_NO_DEPRECATED
    )
else()
    # Linux/Unix - link OpenSSL for password hashing
    target_link_libraries(chat_server_lib PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# Message store (always built - InMemoryMessageStore works without Redis)
target_sources(chat_server_lib PRIVATE
    src/redis/MessageStore.cpp
)

# Redis support (optional)
if(ENABLE_REDIS)
    find_package(hiredis REQUIRED)
    target_sources(chat_server_lib PRIVATE
        src/redis/RedisClient.cpp
        src/redis/RedisPubSub.cpp
    )
    target_link_libraries(chat_server_lib PUBLIC hiredis::hiredis)
    target_compile_definitions(chat_server_lib PUBLIC ENABLE_REDIS)
endif()

# Main executable
add_executable(chat_server src/main.cpp)
target_link_libraries(chat_server PRIVATE chat_server_lib)

# Tests
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Install
install(TARGETS chat_server RUNTIME DESTINATION bin)
install(DIRECTORY config/ DESTINATION etc/chat_server)
install(DIRECTORY include/ DESTINATION include)

# Print configuration
message(STATUS "")
message(STATUS "=== UniversalChatServer Configuration ===")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Redis:        ${ENABLE_REDIS}")
message(STATUS "  Tests:        ${BUILD_TESTS}")
message(STATUS "")
