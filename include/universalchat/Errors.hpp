#pragma once

#include <string>

namespace chat {

enum class ErrorCode : int {
    // Success
    Success = 0,

    // Connection errors (1xxx)
    ConnectionFailed = 1001,
    ConnectionTimeout = 1002,
    ConnectionClosed = 1003,
    MaxConnectionsReached = 1004,
    InvalidPacket = 1005,
    PacketTooLarge = 1006,

    // Authentication errors (2xxx)
    AuthRequired = 2001,
    AuthFailed = 2002,
    InvalidToken = 2003,
    SessionExpired = 2004,
    InvalidUserId = 2005,
    DuplicateSession = 2006,

    // Channel errors (3xxx)
    ChannelNotFound = 3001,
    ChannelFull = 3002,
    AlreadyInChannel = 3003,
    NotInChannel = 3004,
    ChannelCreateFailed = 3005,
    ChannelDeleteFailed = 3006,
    ChannelPasswordRequired = 3007,
    ChannelPasswordIncorrect = 3008,
    CannotDeleteSystemChannel = 3009,

    // Message errors (4xxx)
    MessageTooLong = 4001,
    MessageEmpty = 4002,
    SendFailed = 4003,
    InvalidMessageType = 4004,
    RateLimited = 4005,

    // Whisper errors (5xxx)
    UserNotFound = 5001,
    UserOffline = 5002,
    WhisperFailed = 5003,
    CannotWhisperSelf = 5004,

    // Scale-out errors (7xxx)
    PubSubPublishFailed = 7001,
    PubSubNotConnected = 7002,
    SessionRegistryError = 7003,
    ChannelRegistryError = 7004,
    CrossServerRoutingFailed = 7005,

    // Server errors (9xxx)
    InternalError = 9001,
    ServiceUnavailable = 9002,
    ConfigError = 9003,
    DatabaseError = 9004,
};

inline const char* getErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::Success:
            return "Success";

        // Connection errors
        case ErrorCode::ConnectionFailed:
            return "Connection failed";
        case ErrorCode::ConnectionTimeout:
            return "Connection timeout";
        case ErrorCode::ConnectionClosed:
            return "Connection closed";
        case ErrorCode::MaxConnectionsReached:
            return "Maximum connections reached";
        case ErrorCode::InvalidPacket:
            return "Invalid packet format";
        case ErrorCode::PacketTooLarge:
            return "Packet size exceeds limit";

        // Authentication errors
        case ErrorCode::AuthRequired:
            return "Authentication required";
        case ErrorCode::AuthFailed:
            return "Authentication failed";
        case ErrorCode::InvalidToken:
            return "Invalid authentication token";
        case ErrorCode::SessionExpired:
            return "Session has expired";
        case ErrorCode::InvalidUserId:
            return "Invalid user ID";
        case ErrorCode::DuplicateSession:
            return "User already connected";

        // Channel errors
        case ErrorCode::ChannelNotFound:
            return "Channel not found";
        case ErrorCode::ChannelFull:
            return "Channel is full";
        case ErrorCode::AlreadyInChannel:
            return "Already in this channel";
        case ErrorCode::NotInChannel:
            return "Not in this channel";
        case ErrorCode::ChannelCreateFailed:
            return "Failed to create channel";
        case ErrorCode::ChannelDeleteFailed:
            return "Failed to delete channel";
        case ErrorCode::ChannelPasswordRequired:
            return "Channel password required";
        case ErrorCode::ChannelPasswordIncorrect:
            return "Incorrect channel password";
        case ErrorCode::CannotDeleteSystemChannel:
            return "Cannot delete system channel";

        // Message errors
        case ErrorCode::MessageTooLong:
            return "Message is too long";
        case ErrorCode::MessageEmpty:
            return "Message cannot be empty";
        case ErrorCode::SendFailed:
            return "Failed to send message";
        case ErrorCode::InvalidMessageType:
            return "Invalid message type";
        case ErrorCode::RateLimited:
            return "Rate limit exceeded";

        // Whisper errors
        case ErrorCode::UserNotFound:
            return "User not found";
        case ErrorCode::UserOffline:
            return "User is offline";
        case ErrorCode::WhisperFailed:
            return "Failed to send whisper";
        case ErrorCode::CannotWhisperSelf:
            return "Cannot whisper to yourself";

        // Scale-out errors
        case ErrorCode::PubSubPublishFailed:
            return "Failed to publish message to Pub/Sub";
        case ErrorCode::PubSubNotConnected:
            return "Pub/Sub client is not connected";
        case ErrorCode::SessionRegistryError:
            return "Session registry operation failed";
        case ErrorCode::ChannelRegistryError:
            return "Channel registry operation failed";
        case ErrorCode::CrossServerRoutingFailed:
            return "Failed to route message to another server";

        // Server errors
        case ErrorCode::InternalError:
            return "Internal server error";
        case ErrorCode::ServiceUnavailable:
            return "Service unavailable";
        case ErrorCode::ConfigError:
            return "Configuration error";
        case ErrorCode::DatabaseError:
            return "Database error";

        default:
            return "Unknown error";
    }
}

inline int toInt(ErrorCode code) {
    return static_cast<int>(code);
}

} // namespace chat
