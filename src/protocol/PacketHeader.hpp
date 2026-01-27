#pragma once

#include <cstdint>
#include <cstring>

namespace chat {

/**
 * Packet Header Structure (8 bytes)
 *
 * +----------------+----------------+----------------+
 * |    Length (4)  |    Type (2)    |  Reserved (2)  |
 * +----------------+----------------+----------------+
 *
 * - Length: Total packet length including header (uint32_t)
 * - Type: Packet type identifier (uint16_t)
 * - Reserved: Reserved for future use (uint16_t)
 */

#pragma pack(push, 1)
struct PacketHeader {
    uint32_t length;     // Total packet length (header + body)
    uint16_t type;       // Packet type
    uint16_t reserved;   // Reserved (set to 0)

    PacketHeader() : length(0), type(0), reserved(0) {}

    PacketHeader(uint32_t len, uint16_t t)
        : length(len), type(t), reserved(0) {}

    // Serialize to bytes
    void toBytes(char* buffer) const {
        std::memcpy(buffer, this, sizeof(PacketHeader));
    }

    // Deserialize from bytes
    static PacketHeader fromBytes(const char* buffer) {
        PacketHeader header;
        std::memcpy(&header, buffer, sizeof(PacketHeader));
        return header;
    }

    // Get body length (total - header)
    uint32_t bodyLength() const {
        return length > sizeof(PacketHeader) ? length - sizeof(PacketHeader) : 0;
    }
};
#pragma pack(pop)

// Compile-time check for header size
static_assert(sizeof(PacketHeader) == 8, "PacketHeader must be exactly 8 bytes");

} // namespace chat
