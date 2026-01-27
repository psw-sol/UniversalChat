#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chat {

/**
 * Packet Type Definitions
 *
 * Packet types are organized by category:
 * - 0x00xx: Connection management
 * - 0x01xx: Authentication
 * - 0x02xx: Channel operations
 * - 0x03xx: Messages
 * - 0x04xx: Whisper/Private messages
 * - 0x05xx: Profile management
 * - 0xFFxx: Errors
 */
enum class PacketType : uint16_t {
    // === Connection (0x00xx) ===
    Heartbeat           = 0x0001,
    HeartbeatAck        = 0x0002,
    Disconnect          = 0x0003,

    // === Authentication (0x01xx) ===
    AuthRequest         = 0x0101,
    AuthResponse        = 0x0102,
    Logout              = 0x0103,
    LogoutAck           = 0x0104,

    // === Channel (0x02xx) ===
    ChannelListRequest  = 0x0201,
    ChannelListResponse = 0x0202,
    ChannelJoin         = 0x0203,
    ChannelJoinAck      = 0x0204,
    ChannelLeave        = 0x0205,
    ChannelLeaveAck     = 0x0206,
    ChannelMemberUpdate = 0x0207,
    ChannelCreate       = 0x0208,
    ChannelCreateAck    = 0x0209,
    ChannelAutoAssign   = 0x020A,  // 자동 채널 배정 요청
    ChannelAutoAssignAck = 0x020B, // 자동 채널 배정 응답

    // === Message (0x03xx) ===
    MessageSend         = 0x0301,
    MessageReceive      = 0x0302,
    MessageAck          = 0x0303,

    // === Whisper (0x04xx) ===
    WhisperSend         = 0x0401,
    WhisperReceive      = 0x0402,
    WhisperAck          = 0x0403,

    // === Profile (0x05xx) ===
    ProfileUpdateRequest  = 0x0501,
    ProfileUpdateResponse = 0x0502,
    ProfileChanged        = 0x0503,

    // === Error (0xFFxx) ===
    Error               = 0xFF01,

    // === Unknown ===
    Unknown             = 0xFFFF,
};

/**
 * Packet structure containing type and serialized data
 */
struct Packet {
    PacketType type = PacketType::Unknown;
    std::vector<char> data;

    Packet() = default;

    Packet(PacketType t) : type(t) {}

    Packet(PacketType t, std::vector<char> d)
        : type(t), data(std::move(d)) {}

    Packet(PacketType t, const std::string& s)
        : type(t), data(s.begin(), s.end()) {}

    // Get data as string
    std::string dataAsString() const {
        return std::string(data.begin(), data.end());
    }

    // Check if packet has data
    bool hasData() const {
        return !data.empty();
    }

    // Get total size (header + body)
    size_t totalSize() const {
        return 8 + data.size();  // 8 = header size
    }
};

/**
 * Helper function to get packet type name for debugging
 */
inline const char* getPacketTypeName(PacketType type) {
    switch (type) {
        case PacketType::Heartbeat:           return "Heartbeat";
        case PacketType::HeartbeatAck:        return "HeartbeatAck";
        case PacketType::Disconnect:          return "Disconnect";
        case PacketType::AuthRequest:         return "AuthRequest";
        case PacketType::AuthResponse:        return "AuthResponse";
        case PacketType::Logout:              return "Logout";
        case PacketType::LogoutAck:           return "LogoutAck";
        case PacketType::ChannelListRequest:  return "ChannelListRequest";
        case PacketType::ChannelListResponse: return "ChannelListResponse";
        case PacketType::ChannelJoin:         return "ChannelJoin";
        case PacketType::ChannelJoinAck:      return "ChannelJoinAck";
        case PacketType::ChannelLeave:        return "ChannelLeave";
        case PacketType::ChannelLeaveAck:     return "ChannelLeaveAck";
        case PacketType::ChannelMemberUpdate: return "ChannelMemberUpdate";
        case PacketType::ChannelCreate:       return "ChannelCreate";
        case PacketType::ChannelCreateAck:    return "ChannelCreateAck";
        case PacketType::ChannelAutoAssign:   return "ChannelAutoAssign";
        case PacketType::ChannelAutoAssignAck: return "ChannelAutoAssignAck";
        case PacketType::MessageSend:         return "MessageSend";
        case PacketType::MessageReceive:      return "MessageReceive";
        case PacketType::MessageAck:          return "MessageAck";
        case PacketType::WhisperSend:         return "WhisperSend";
        case PacketType::WhisperReceive:      return "WhisperReceive";
        case PacketType::WhisperAck:          return "WhisperAck";
        case PacketType::ProfileUpdateRequest:  return "ProfileUpdateRequest";
        case PacketType::ProfileUpdateResponse: return "ProfileUpdateResponse";
        case PacketType::ProfileChanged:        return "ProfileChanged";
        case PacketType::Error:               return "Error";
        default:                              return "Unknown";
    }
}

/**
 * Check if packet type requires authentication
 */
inline bool requiresAuth(PacketType type) {
    switch (type) {
        case PacketType::Heartbeat:
        case PacketType::HeartbeatAck:
        case PacketType::AuthRequest:
        case PacketType::AuthResponse:
        case PacketType::Error:
            return false;
        default:
            return true;
    }
}

} // namespace chat
