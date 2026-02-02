#include "ChannelRegistry.hpp"
#include "RedisClient.hpp"
#include "../util/Logger.hpp"
#include <algorithm>
#include <cctype>

namespace chat {

ChannelRegistry::ChannelRegistry(std::shared_ptr<RedisClient> redis)
    : redis_(std::move(redis)) {
}

// === Member Management ===

bool ChannelRegistry::addMember(const std::string& channel_id, const std::string& user_id) {
    if (!redis_ || !redis_->isConnected()) {
        LOG_ERROR("ChannelRegistry::addMember - Redis not connected");
        return false;
    }

    if (channel_id.empty() || user_id.empty()) {
        LOG_WARN("ChannelRegistry::addMember - Empty channel_id or user_id");
        return false;
    }

    try {
        // Add user to channel members set
        std::string members_key = makeMembersKey(channel_id);
        int64_t added = redis_->sadd(members_key, user_id);

        // Add channel to user's channels set
        std::string user_channels_key = makeUserChannelsKey(user_id);
        redis_->sadd(user_channels_key, channel_id);

        LOG_DEBUG("ChannelRegistry::addMember - Added user {} to channel {} (new={})",
                  user_id, channel_id, added > 0);

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::addMember - Exception: {}", e.what());
        return false;
    }
}

bool ChannelRegistry::removeMember(const std::string& channel_id, const std::string& user_id) {
    if (!redis_ || !redis_->isConnected()) {
        LOG_ERROR("ChannelRegistry::removeMember - Redis not connected");
        return false;
    }

    if (channel_id.empty() || user_id.empty()) {
        LOG_WARN("ChannelRegistry::removeMember - Empty channel_id or user_id");
        return false;
    }

    try {
        // Remove user from channel members set
        std::string members_key = makeMembersKey(channel_id);
        int64_t removed = redis_->srem(members_key, user_id);

        // Remove channel from user's channels set
        std::string user_channels_key = makeUserChannelsKey(user_id);
        redis_->srem(user_channels_key, channel_id);

        LOG_DEBUG("ChannelRegistry::removeMember - Removed user {} from channel {} (was_member={})",
                  user_id, channel_id, removed > 0);

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::removeMember - Exception: {}", e.what());
        return false;
    }
}

bool ChannelRegistry::isMember(const std::string& channel_id, const std::string& user_id) const {
    if (!redis_ || !redis_->isConnected()) {
        return false;
    }

    if (channel_id.empty() || user_id.empty()) {
        return false;
    }

    try {
        std::string members_key = makeMembersKey(channel_id);
        return redis_->sismember(members_key, user_id);
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::isMember - Exception: {}", e.what());
        return false;
    }
}

// === Member Query ===

int64_t ChannelRegistry::getMemberCount(const std::string& channel_id) const {
    if (!redis_ || !redis_->isConnected()) {
        return 0;
    }

    if (channel_id.empty()) {
        return 0;
    }

    try {
        std::string members_key = makeMembersKey(channel_id);
        return redis_->scard(members_key);
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::getMemberCount - Exception: {}", e.what());
        return 0;
    }
}

std::vector<std::string> ChannelRegistry::getMembers(const std::string& channel_id,
                                                      int offset,
                                                      int count) const {
    std::vector<std::string> result;

    if (!redis_ || !redis_->isConnected()) {
        return result;
    }

    if (channel_id.empty()) {
        return result;
    }

    try {
        std::string members_key = makeMembersKey(channel_id);

        // Use SSCAN for pagination
        std::string cursor = "0";
        int collected = 0;
        int skipped = 0;

        do {
            auto scan_result = redis_->sscan(members_key, cursor, "", count + offset);
            cursor = scan_result.first;

            for (const auto& member : scan_result.second) {
                if (skipped < offset) {
                    skipped++;
                    continue;
                }

                if (collected >= count) {
                    break;
                }

                result.push_back(member);
                collected++;
            }

            if (collected >= count) {
                break;
            }

        } while (cursor != "0");

        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::getMembers - Exception: {}", e.what());
        return result;
    }
}

// === User Channel Query ===

std::vector<std::string> ChannelRegistry::getUserChannels(const std::string& user_id) const {
    std::vector<std::string> result;

    if (!redis_ || !redis_->isConnected()) {
        return result;
    }

    if (user_id.empty()) {
        return result;
    }

    try {
        std::string user_channels_key = makeUserChannelsKey(user_id);
        return redis_->smembers(user_channels_key);
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::getUserChannels - Exception: {}", e.what());
        return result;
    }
}

int ChannelRegistry::removeUserFromAllChannels(const std::string& user_id) {
    if (!redis_ || !redis_->isConnected()) {
        LOG_ERROR("ChannelRegistry::removeUserFromAllChannels - Redis not connected");
        return 0;
    }

    if (user_id.empty()) {
        return 0;
    }

    try {
        // Get all channels the user is in
        std::string user_channels_key = makeUserChannelsKey(user_id);
        auto channels = redis_->smembers(user_channels_key);

        int removed_count = 0;

        // Remove user from each channel's members set
        for (const auto& channel_id : channels) {
            std::string members_key = makeMembersKey(channel_id);
            redis_->srem(members_key, user_id);
            removed_count++;
        }

        // Delete user's channels set
        redis_->del(user_channels_key);

        LOG_DEBUG("ChannelRegistry::removeUserFromAllChannels - Removed user {} from {} channels",
                  user_id, removed_count);

        return removed_count;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::removeUserFromAllChannels - Exception: {}", e.what());
        return 0;
    }
}

// === Channel Metadata ===

bool ChannelRegistry::setChannelMetadata(const std::string& channel_id, const ChannelConfig& config) {
    if (!redis_ || !redis_->isConnected()) {
        LOG_ERROR("ChannelRegistry::setChannelMetadata - Redis not connected");
        return false;
    }

    if (channel_id.empty()) {
        return false;
    }

    try {
        std::string metadata_key = makeMetadataKey(channel_id);

        std::unordered_map<std::string, std::string> fields;
        fields[FIELD_NAME] = config.channel_name;
        fields[FIELD_MAX_MEMBERS] = std::to_string(config.max_members);
        fields[FIELD_IS_SYSTEM] = config.is_system ? "1" : "0";
        fields[FIELD_IS_PERSISTENT] = config.is_persistent ? "1" : "0";

        // Get current time for created_at if not already set
        auto existing = redis_->hget(metadata_key, FIELD_CREATED_AT);
        if (!existing.has_value()) {
            fields[FIELD_CREATED_AT] = std::to_string(std::time(nullptr));
        }

        redis_->hmset(metadata_key, fields);

        LOG_DEBUG("ChannelRegistry::setChannelMetadata - Set metadata for channel {}", channel_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::setChannelMetadata - Exception: {}", e.what());
        return false;
    }
}

std::optional<ChannelRegistryInfo> ChannelRegistry::getChannelMetadata(const std::string& channel_id) const {
    if (!redis_ || !redis_->isConnected()) {
        return std::nullopt;
    }

    if (channel_id.empty()) {
        return std::nullopt;
    }

    try {
        std::string metadata_key = makeMetadataKey(channel_id);

        auto fields = redis_->hgetall(metadata_key);
        if (fields.empty()) {
            return std::nullopt;
        }

        ChannelRegistryInfo info;
        info.channel_id = channel_id;

        auto it = fields.find(FIELD_NAME);
        if (it != fields.end()) {
            info.channel_name = it->second;
        }

        it = fields.find(FIELD_MAX_MEMBERS);
        if (it != fields.end()) {
            info.max_members = std::stoi(it->second);
        }

        it = fields.find(FIELD_IS_SYSTEM);
        if (it != fields.end()) {
            info.is_system = (it->second == "1");
        }

        it = fields.find(FIELD_IS_PERSISTENT);
        if (it != fields.end()) {
            info.is_persistent = (it->second == "1");
        }

        it = fields.find(FIELD_CREATED_AT);
        if (it != fields.end()) {
            info.created_at = std::stoll(it->second);
        }

        // Get current global member count
        info.global_member_count = getMemberCount(channel_id);

        return info;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::getChannelMetadata - Exception: {}", e.what());
        return std::nullopt;
    }
}

bool ChannelRegistry::deleteChannelMetadata(const std::string& channel_id) {
    if (!redis_ || !redis_->isConnected()) {
        LOG_ERROR("ChannelRegistry::deleteChannelMetadata - Redis not connected");
        return false;
    }

    if (channel_id.empty()) {
        return false;
    }

    try {
        std::string metadata_key = makeMetadataKey(channel_id);
        std::string members_key = makeMembersKey(channel_id);

        // Delete metadata and members set
        redis_->del(metadata_key);
        redis_->del(members_key);

        LOG_DEBUG("ChannelRegistry::deleteChannelMetadata - Deleted metadata for channel {}", channel_id);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::deleteChannelMetadata - Exception: {}", e.what());
        return false;
    }
}

std::unordered_map<std::string, int64_t> ChannelRegistry::getMemberCountBatch(
    const std::vector<std::string>& channel_ids) const {

    std::unordered_map<std::string, int64_t> result;

    if (!redis_ || !redis_->isConnected()) {
        return result;
    }

    if (channel_ids.empty()) {
        return result;
    }

    try {
        // Use pipeline for efficiency
        for (const auto& channel_id : channel_ids) {
            result[channel_id] = getMemberCount(channel_id);
        }

        return result;
    } catch (const std::exception& e) {
        LOG_ERROR("ChannelRegistry::getMemberCountBatch - Exception: {}", e.what());
        return result;
    }
}

// === Utility ===

bool ChannelRegistry::isAvailable() const {
    return redis_ && redis_->isConnected();
}

// === Private Helpers ===

std::string ChannelRegistry::makeMembersKey(const std::string& channel_id) const {
    return std::string(CHANNEL_MEMBERS_PREFIX) + channel_id + CHANNEL_MEMBERS_SUFFIX;
}

std::string ChannelRegistry::makeUserChannelsKey(const std::string& user_id) const {
    return std::string(USER_CHANNELS_PREFIX) + user_id + USER_CHANNELS_SUFFIX;
}

std::string ChannelRegistry::makeMetadataKey(const std::string& channel_id) const {
    return std::string(CHANNEL_METADATA_PREFIX) + channel_id;
}

} // namespace chat
