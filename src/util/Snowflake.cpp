#include "Snowflake.hpp"
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace chat {

uint16_t Snowflake::machine_id_ = 0;
std::atomic<uint16_t> Snowflake::sequence_{0};
int64_t Snowflake::last_timestamp_ = -1;
std::mutex Snowflake::mutex_;
bool Snowflake::initialized_ = false;

void Snowflake::init(uint16_t machine_id) {
    if (machine_id > MAX_MACHINE_ID) {
        throw std::invalid_argument("Machine ID must be between 0 and " + std::to_string(MAX_MACHINE_ID));
    }
    machine_id_ = machine_id;
    initialized_ = true;
}

uint64_t Snowflake::generateId() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        // Auto-initialize with machine ID 0
        machine_id_ = 0;
        initialized_ = true;
    }

    int64_t timestamp = currentTimeMillis();

    if (timestamp < last_timestamp_) {
        // Clock moved backwards - wait until we catch up
        timestamp = waitNextMillis(last_timestamp_);
    }

    if (timestamp == last_timestamp_) {
        // Same millisecond - increment sequence
        sequence_ = (sequence_ + 1) & MAX_SEQUENCE;

        if (sequence_ == 0) {
            // Sequence overflow - wait for next millisecond
            timestamp = waitNextMillis(last_timestamp_);
        }
    } else {
        // New millisecond - reset sequence
        sequence_ = 0;
    }

    last_timestamp_ = timestamp;

    // Construct the ID
    uint64_t id = ((timestamp - EPOCH) << TIMESTAMP_SHIFT)
                | (static_cast<uint64_t>(machine_id_) << MACHINE_ID_SHIFT)
                | sequence_.load();

    return id;
}

std::string Snowflake::generate() {
    uint64_t id = generateId();

    // Convert to string (can use hex or decimal)
    std::ostringstream oss;
    oss << id;
    return oss.str();
}

int64_t Snowflake::getTimestamp(uint64_t id) {
    return (id >> TIMESTAMP_SHIFT) + EPOCH;
}

uint16_t Snowflake::getMachineId(uint64_t id) {
    return static_cast<uint16_t>((id >> MACHINE_ID_SHIFT) & MAX_MACHINE_ID);
}

uint16_t Snowflake::getSequence(uint64_t id) {
    return static_cast<uint16_t>(id & MAX_SEQUENCE);
}

int64_t Snowflake::currentTimeMillis() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

int64_t Snowflake::waitNextMillis(int64_t last) {
    int64_t timestamp = currentTimeMillis();
    while (timestamp <= last) {
        timestamp = currentTimeMillis();
    }
    return timestamp;
}

} // namespace chat
