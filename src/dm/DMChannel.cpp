#include "DMChannel.hpp"
#include "../util/Logger.hpp"
#include <algorithm>
#include <chrono>

namespace chat {

DMChannel::DMChannel(const std::string& user1_id, const std::string& user2_id, int max_history)
    : max_history_(max_history)
{
    // Sort user IDs alphabetically for canonical channel ID
    if (user1_id < user2_id) {
        user1_id_ = user1_id;
        user2_id_ = user2_id;
    } else {
        user1_id_ = user2_id;
        user2_id_ = user1_id;
    }

    channel_id_ = makeDMChannelId(user1_id_, user2_id_);
    created_at_ = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    LOG_DEBUG("DMChannel created: {} (users: {}, {})", channel_id_, user1_id_, user2_id_);
}

std::string DMChannel::getPeerId(const std::string& my_user_id) const {
    if (my_user_id == user1_id_) return user2_id_;
    if (my_user_id == user2_id_) return user1_id_;
    return "";
}

bool DMChannel::isMember(const std::string& user_id) const {
    return user_id == user1_id_ || user_id == user2_id_;
}

// === Message History ===

void DMChannel::addMessage(const ChatMessage& message) {
    std::lock_guard<std::mutex> lock(history_mutex_);

    messages_.push_back(message);

    // Trim if exceeds max
    while (static_cast<int>(messages_.size()) > max_history_) {
        messages_.pop_front();
    }
}

std::vector<ChatMessage> DMChannel::getRecentMessages(int count) const {
    std::lock_guard<std::mutex> lock(history_mutex_);

    int start = std::max(0, static_cast<int>(messages_.size()) - count);
    return std::vector<ChatMessage>(messages_.begin() + start, messages_.end());
}

std::vector<ChatMessage> DMChannel::getMessagesBefore(int64_t before_timestamp, int count) const {
    std::lock_guard<std::mutex> lock(history_mutex_);

    std::vector<ChatMessage> result;
    result.reserve(count);

    // Iterate backwards to find messages before the timestamp
    for (auto it = messages_.rbegin(); it != messages_.rend() && static_cast<int>(result.size()) < count; ++it) {
        if (it->timestamp < before_timestamp) {
            result.push_back(*it);
        }
    }

    // Reverse to maintain chronological order
    std::reverse(result.begin(), result.end());
    return result;
}

const ChatMessage* DMChannel::getLastMessage() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    if (messages_.empty()) return nullptr;
    return &messages_.back();
}

// === Read Receipts ===

void DMChannel::markAsRead(const std::string& user_id, const std::string& message_id, int64_t timestamp) {
    std::lock_guard<std::mutex> lock(read_mutex_);

    auto& state = read_states_[user_id];
    if (timestamp > state.last_read_timestamp) {
        state.last_read_message_id = message_id;
        state.last_read_timestamp = timestamp;
    }
}

int64_t DMChannel::getLastReadTimestamp(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(read_mutex_);

    auto it = read_states_.find(user_id);
    return (it != read_states_.end()) ? it->second.last_read_timestamp : 0;
}

std::string DMChannel::getLastReadMessageId(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(read_mutex_);

    auto it = read_states_.find(user_id);
    return (it != read_states_.end()) ? it->second.last_read_message_id : "";
}

int DMChannel::getUnreadCount(const std::string& user_id) const {
    int64_t last_read_ts = getLastReadTimestamp(user_id);

    std::lock_guard<std::mutex> lock(history_mutex_);

    int count = 0;
    for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
        if (it->timestamp <= last_read_ts) break;
        // Don't count own messages as unread
        if (it->sender_id != user_id) {
            count++;
        }
    }
    return count;
}

// === Deletion ===

void DMChannel::markDeletedForUser(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(delete_mutex_);
    deleted_by_.insert(user_id);
}

bool DMChannel::isDeletedForUser(const std::string& user_id) const {
    std::lock_guard<std::mutex> lock(delete_mutex_);
    return deleted_by_.count(user_id) > 0;
}

void DMChannel::unmarkDeletedForUser(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(delete_mutex_);
    deleted_by_.erase(user_id);
}

// === Static Helpers ===

std::string DMChannel::makeDMChannelId(const std::string& user_id_a, const std::string& user_id_b) {
    if (user_id_a < user_id_b) {
        return "dm:" + user_id_a + ":" + user_id_b;
    } else {
        return "dm:" + user_id_b + ":" + user_id_a;
    }
}

} // namespace chat
