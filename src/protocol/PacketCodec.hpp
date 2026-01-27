#pragma once

#include "PacketHeader.hpp"
#include "PacketTypes.hpp"
#include <vector>
#include <optional>
#include <stdexcept>

namespace chat {

/**
 * Packet Codec
 *
 * Handles encoding and decoding of packets.
 * Format: [Header (8 bytes)][Body (variable)]
 */
class PacketCodec {
public:
    /**
     * Encode a packet to byte buffer
     *
     * @param packet The packet to encode
     * @return Byte buffer containing header + body
     */
    static std::vector<char> encode(const Packet& packet);

    /**
     * Decode header from byte buffer
     *
     * @param data Pointer to buffer
     * @param length Buffer length
     * @return Decoded header
     * @throws std::runtime_error if buffer is too small
     */
    static PacketHeader decodeHeader(const char* data, size_t length);

    /**
     * Decode complete packet from byte buffer
     *
     * @param data Pointer to buffer
     * @param length Buffer length
     * @return Decoded packet
     * @throws std::runtime_error if buffer is invalid
     */
    static Packet decode(const char* data, size_t length);

    /**
     * Check if buffer contains a complete packet
     *
     * @param data Pointer to buffer
     * @param length Buffer length
     * @return Total packet length if complete, 0 if incomplete
     */
    static size_t isComplete(const char* data, size_t length);

    /**
     * Validate packet header
     *
     * @param header The header to validate
     * @param max_packet_size Maximum allowed packet size
     * @return true if valid
     */
    static bool validateHeader(const PacketHeader& header, size_t max_packet_size);

    // Header size constant
    static constexpr size_t HEADER_SIZE = sizeof(PacketHeader);
};

// ==========================================
// Inline implementations
// ==========================================

inline std::vector<char> PacketCodec::encode(const Packet& packet) {
    PacketHeader header;
    header.length = static_cast<uint32_t>(HEADER_SIZE + packet.data.size());
    header.type = static_cast<uint16_t>(packet.type);
    header.reserved = 0;

    std::vector<char> buffer(header.length);

    // Copy header
    header.toBytes(buffer.data());

    // Copy body
    if (!packet.data.empty()) {
        std::memcpy(buffer.data() + HEADER_SIZE,
                   packet.data.data(),
                   packet.data.size());
    }

    return buffer;
}

inline PacketHeader PacketCodec::decodeHeader(const char* data, size_t length) {
    if (length < HEADER_SIZE) {
        throw std::runtime_error("Buffer too small for header");
    }
    return PacketHeader::fromBytes(data);
}

inline Packet PacketCodec::decode(const char* data, size_t length) {
    if (length < HEADER_SIZE) {
        throw std::runtime_error("Buffer too small for packet");
    }

    PacketHeader header = decodeHeader(data, length);

    if (length < header.length) {
        throw std::runtime_error("Incomplete packet data");
    }

    Packet packet;
    packet.type = static_cast<PacketType>(header.type);

    size_t body_length = header.bodyLength();
    if (body_length > 0) {
        packet.data.resize(body_length);
        std::memcpy(packet.data.data(),
                   data + HEADER_SIZE,
                   body_length);
    }

    return packet;
}

inline size_t PacketCodec::isComplete(const char* data, size_t length) {
    if (length < HEADER_SIZE) {
        return 0;  // Not enough data for header
    }

    PacketHeader header = PacketHeader::fromBytes(data);

    if (length >= header.length) {
        return header.length;  // Complete packet
    }

    return 0;  // Incomplete
}

inline bool PacketCodec::validateHeader(const PacketHeader& header, size_t max_packet_size) {
    // Check minimum length
    if (header.length < HEADER_SIZE) {
        return false;
    }

    // Check maximum length
    if (header.length > max_packet_size) {
        return false;
    }

    // Reserved field should be 0
    if (header.reserved != 0) {
        // Warning but don't fail - future compatibility
    }

    return true;
}

} // namespace chat
