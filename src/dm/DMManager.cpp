#include "DMManager.hpp"
#include "../core/Session.hpp"
#include "../core/SessionManager.hpp"
#include "../util/Config.hpp"
#include "../util/Logger.hpp"
#include "../util/Snowflake.hpp"
#include "../protocol/PacketTypes.hpp"
#include "chat.pb.h"
#include <algorithm>
#include <chrono>

namespace chat {

DMManager::DMManager(SessionManager& session_manager, const Config& config)
    : session_manager_(session_manager)
    , config_(config)
{
    LOG_INFO("DMManager initialized (enabled={}, history_max={})",
             config.dm_enabled, config.dm_history_max_per_channel);
}

DMChannel* DMManager::getOrCreateDMChannel(const std::string& user1_id, const std::string& user2_id) {
    std::string channel_id = DMChannel::makeDMChannelId(user1_id, user2_id);

    // Try read lock first
    {
        std::shared_lock<std::shared_mutex> lock(channels_mutex_);
        auto it = channels_.find(channel_id);
        if (it != channels_.end()) {
            // Unmark deletion if was deleted (reopening conversation)
            it->second->unmarkDeletedForUser(user1_id);
            it->second->unmarkDeletedForUser(user2_id);
            return it->second.get();
        }
    }

    // Need to create - upgrade to write lock
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    // Double-check after acquiring write lock
    auto it = channels_.find(channel_id);
    if (it != channels_.end()) {
        it->second->unmarkDeletedForUser(user1_id);
        it->second->unmarkDeletedForUser(user2_id);
        return it->second.get();
    }

    // Create new DM channel
    auto channel = std::make_unique<DMChannel>(
        user1_id, user2_id, config_.dm_history_max_per_channel);
    auto* ptr = channel.get();
    channels_[channel_id] = std::move(channel);

    // Release channels lock before updating user index
    lock.unlock();

    // Update user index
    addToUserIndex(user1_id, channel_id);
    addToUserIndex(user2_id, channel_id);

    LOG_INFO("DMChannel created: {} (users: {}, {})", channel_id, user1_id, user2_id);
    return ptr;
}

DMChannel* DMManager::getDMChannel(const std::string& dm_channel_id) {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    auto it = channels_.find(dm_channel_id);
    return (it != channels_.end()) ? it->second.get() : nullptr;
}

std::vector<DMConversationSummary> DMManager::getUserDMList(
    const std::string& user_id,
    int limit,
    int64_t before_timestamp)
{
    // Get user's DM channel IDs
    std::vector<std::string> channel_ids;
    {
        std::shared_lock<std::shared_mutex> lock(user_index_mutex_);
        auto it = user_dm_channels_.find(user_id);
        if (it != user_dm_channels_.end()) {
            channel_ids = it->second;
        }
    }

    // Build summaries
    std::vector<DMConversationSummary> summaries;
    summaries.reserve(channel_ids.size());

    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    for (const auto& ch_id : channel_ids) {
        auto it = channels_.find(ch_id);
        if (it == channels_.end()) continue;

        auto* channel = it->second.get();

        // Skip if deleted for this user
        if (channel->isDeletedForUser(user_id)) continue;

        DMConversationSummary summary;
        summary.dm_channel_id = ch_id;
        summary.peer_user_id = channel->getPeerId(user_id);

        // Get last message info
        const auto* last_msg = channel->getLastMessage();
        if (last_msg) {
            summary.last_message_content = last_msg->content;
            summary.last_message_timestamp = last_msg->timestamp;
        } else {
            summary.last_message_timestamp = channel->createdAt();
        }

        // Filter by timestamp
        if (before_timestamp > 0 && summary.last_message_timestamp >= before_timestamp) {
            continue;
        }

        summary.unread_count = channel->getUnreadCount(user_id);

        // Try to get peer info from active session
        auto peer_session = session_manager_.getByUserId(summary.peer_user_id);
        if (peer_session) {
            summary.peer_nickname = peer_session->nickname();
            summary.peer_profile_image = peer_session->profileImage();
            summary.peer_frame_image = peer_session->frameImage();
            summary.peer_extra_data = peer_session->extraData();
        } else {
            summary.peer_nickname = summary.peer_user_id;  // Fallback
        }

        summaries.push_back(std::move(summary));
    }

    // Sort by last message timestamp (newest first)
    std::sort(summaries.begin(), summaries.end(),
        [](const DMConversationSummary& a, const DMConversationSummary& b) {
            return a.last_message_timestamp > b.last_message_timestamp;
        });

    // Apply limit
    if (limit > 0 && static_cast<int>(summaries.size()) > limit) {
        summaries.resize(limit);
    }

    return summaries;
}

ChatMessage DMManager::sendDMMessage(const std::string& dm_channel_id,
                                      SessionPtr sender,
                                      const std::string& content,
                                      int message_type,
                                      const std::string& client_message_id) {
    auto* channel = getDMChannel(dm_channel_id);
    if (!channel) {
        LOG_WARN("DMManager::sendDMMessage: channel not found: {}", dm_channel_id);
        return {};
    }

    if (!channel->isMember(sender->userId())) {
        LOG_WARN("DMManager::sendDMMessage: user {} not member of {}", sender->userId(), dm_channel_id);
        return {};
    }

    // Build message
    ChatMessage msg;
    msg.message_id = Snowflake::generate();
    msg.channel_id = dm_channel_id;
    msg.sender_id = sender->userId();
    msg.sender_nickname = sender->nickname();
    msg.sender_profile_image = sender->profileImage();
    msg.sender_frame_image = sender->frameImage();
    msg.sender_extra_data = sender->extraData();
    msg.content = content;
    msg.message_type = message_type;
    msg.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Store in history
    channel->addMessage(msg);

    // If the channel was deleted for recipient, undelete it (new message restores)
    std::string peer_id = channel->getPeerId(sender->userId());
    channel->unmarkDeletedForUser(peer_id);

    // Deliver to recipient if online
    auto peer_session = session_manager_.getByUserId(peer_id);
    if (peer_session && peer_session->isConnected()) {
        // Build DMMessageReceive packet
        protocol::DMMessageReceive dm_receive;
        dm_receive.set_dm_channel_id(dm_channel_id);

        auto* proto_msg = dm_receive.mutable_message();
        proto_msg->set_message_id(msg.message_id);
        proto_msg->set_channel_id(msg.channel_id);
        proto_msg->set_sender_id(msg.sender_id);
        proto_msg->set_sender_nickname(msg.sender_nickname);
        proto_msg->set_sender_profile_image(msg.sender_profile_image);
        proto_msg->set_sender_frame_image(msg.sender_frame_image);
        proto_msg->set_sender_extra_data(msg.sender_extra_data);
        proto_msg->set_content(msg.content);
        proto_msg->set_timestamp(msg.timestamp);
        proto_msg->set_message_type(static_cast<protocol::MessageType>(msg.message_type));

        Packet packet;
        packet.type = PacketType::DMMessageReceive;
        std::string data;
        dm_receive.SerializeToString(&data);
        packet.data = std::vector<char>(data.begin(), data.end());

        peer_session->send(packet);

        LOG_DEBUG("DM message delivered: {} -> {} in {}", sender->userId(), peer_id, dm_channel_id);
    } else {
        LOG_DEBUG("DM message stored (peer offline): {} -> {} in {}", sender->userId(), peer_id, dm_channel_id);
    }

    return msg;
}

void DMManager::markAsRead(const std::string& dm_channel_id,
                            const std::string& user_id,
                            const std::string& last_read_message_id,
                            int64_t last_read_timestamp) {
    auto* channel = getDMChannel(dm_channel_id);
    if (!channel || !channel->isMember(user_id)) return;

    channel->markAsRead(user_id, last_read_message_id, last_read_timestamp);

    // Notify the peer about the read receipt
    std::string peer_id = channel->getPeerId(user_id);
    auto peer_session = session_manager_.getByUserId(peer_id);
    if (peer_session && peer_session->isConnected()) {
        protocol::DMReadReceiptNotify notify;
        notify.set_dm_channel_id(dm_channel_id);
        notify.set_reader_user_id(user_id);
        notify.set_last_read_message_id(last_read_message_id);
        notify.set_last_read_timestamp(last_read_timestamp);

        Packet packet;
        packet.type = PacketType::DMReadReceiptNotify;
        std::string data;
        notify.SerializeToString(&data);
        packet.data = std::vector<char>(data.begin(), data.end());

        peer_session->send(packet);
    }
}

bool DMManager::deleteDMForUser(const std::string& dm_channel_id, const std::string& user_id) {
    auto* channel = getDMChannel(dm_channel_id);
    if (!channel || !channel->isMember(user_id)) return false;

    channel->markDeletedForUser(user_id);
    LOG_DEBUG("DM channel {} marked as deleted for user {}", dm_channel_id, user_id);
    return true;
}

void DMManager::onSessionDisconnect(SessionPtr session) {
    // No cleanup needed for DM channels - they persist across sessions
    // This hook is available for future Redis pub/sub cleanup
}

std::vector<std::string> DMManager::getUserDMChannelIds(const std::string& user_id) const {
    std::shared_lock<std::shared_mutex> lock(user_index_mutex_);
    auto it = user_dm_channels_.find(user_id);
    if (it != user_dm_channels_.end()) {
        return it->second;
    }
    return {};
}

void DMManager::addToUserIndex(const std::string& user_id, const std::string& dm_channel_id) {
    std::unique_lock<std::shared_mutex> lock(user_index_mutex_);

    auto& channels = user_dm_channels_[user_id];
    // Avoid duplicates
    if (std::find(channels.begin(), channels.end(), dm_channel_id) == channels.end()) {
        channels.push_back(dm_channel_id);
    }
}

} // namespace chat
