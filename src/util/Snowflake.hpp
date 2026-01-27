#pragma once

#include <cstdint>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>

namespace chat {

/**
 * Snowflake ID Generator
 *
 * Generates unique 64-bit IDs with the following structure:
 * - 41 bits: timestamp (milliseconds since custom epoch)
 * - 10 bits: machine ID (0-1023)
 * - 12 bits: sequence number (0-4095)
 *
 * This allows generating up to 4096 IDs per millisecond per machine.
 */
class Snowflake {
public:
    // Initialize with machine ID (0-1023)
    static void init(uint16_t machine_id = 0);

    // Generate a unique ID as uint64_t
    static uint64_t generateId();

    // Generate a unique ID as string
    static std::string generate();

    // Parse a snowflake ID to extract timestamp
    static int64_t getTimestamp(uint64_t id);

    // Get machine ID from snowflake ID
    static uint16_t getMachineId(uint64_t id);

    // Get sequence from snowflake ID
    static uint16_t getSequence(uint64_t id);

private:
    // Custom epoch: 2020-01-01 00:00:00 UTC
    static constexpr int64_t EPOCH = 1577836800000LL;

    // Bit lengths
    static constexpr int TIMESTAMP_BITS = 41;
    static constexpr int MACHINE_ID_BITS = 10;
    static constexpr int SEQUENCE_BITS = 12;

    // Max values
    static constexpr uint16_t MAX_MACHINE_ID = (1 << MACHINE_ID_BITS) - 1;  // 1023
    static constexpr uint16_t MAX_SEQUENCE = (1 << SEQUENCE_BITS) - 1;      // 4095

    // Bit shifts
    static constexpr int MACHINE_ID_SHIFT = SEQUENCE_BITS;
    static constexpr int TIMESTAMP_SHIFT = SEQUENCE_BITS + MACHINE_ID_BITS;

    static uint16_t machine_id_;
    static std::atomic<uint16_t> sequence_;
    static int64_t last_timestamp_;
    static std::mutex mutex_;
    static bool initialized_;

    static int64_t currentTimeMillis();
    static int64_t waitNextMillis(int64_t last);
};

} // namespace chat
