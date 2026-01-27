#include "WorldChannelManager.hpp"
#include "Channel.hpp"
#include "../util/Logger.hpp"
#include <algorithm>
#include <sstream>

namespace chat {

WorldChannelManager::WorldChannelManager(ChannelManager& channel_manager, const Config& config)
    : channel_manager_(channel_manager)
    , config_(config)
{
    LOG_INFO("WorldChannelManager created for type '{}' (min_channels={}, max_per_channel={}, threshold={}%)",
             config_.channel_type, config_.min_channels, config_.max_members_per_channel,
             static_cast<int>(config_.scale_up_threshold * 100));
}

WorldChannelManager::~WorldChannelManager() {
    stopMaintenance();
}

void WorldChannelManager::initialize() {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    LOG_INFO("Initializing {} channels for type '{}'", config_.min_channels, config_.channel_type);

    // 최소 채널 수만큼 생성
    for (int i = 0; i < config_.min_channels; ++i) {
        std::string channel_id = createNewChannel();
        if (!channel_id.empty()) {
            managed_channels_.push_back(channel_id);
        }
    }

    LOG_INFO("WorldChannelManager initialized with {} channels", managed_channels_.size());
}

void WorldChannelManager::startMaintenance() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    maintenance_thread_ = std::thread([this]() {
        maintenanceLoop();
    });

    LOG_INFO("WorldChannelManager maintenance thread started (interval={}s)",
             config_.maintenance_interval_seconds);
}

void WorldChannelManager::stopMaintenance() {
    if (!running_.exchange(false)) {
        return;  // Already stopped
    }

    if (maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }

    LOG_INFO("WorldChannelManager maintenance thread stopped");
}

std::string WorldChannelManager::selectBestChannel() {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    // 70% 이하인 채널 중 앞쪽 채널 우선 선택
    for (const auto& channel_id : managed_channels_) {
        auto* channel = channel_manager_.getChannel(channel_id);
        if (channel) {
            float occupancy = static_cast<float>(channel->memberCount()) /
                             static_cast<float>(channel->maxMembers());

            if (occupancy < config_.scale_up_threshold) {
                LOG_DEBUG("Selected channel '{}' (occupancy={:.1f}%)",
                         channel_id, occupancy * 100);
                return channel_id;
            }
        }
    }

    // 모든 채널이 70% 초과 - 새 채널 생성 필요
    lock.unlock();  // unique_lock으로 업그레이드 필요

    std::unique_lock<std::shared_mutex> write_lock(channels_mutex_);

    // 다시 한번 체크 (다른 스레드가 생성했을 수 있음)
    for (const auto& channel_id : managed_channels_) {
        auto* channel = channel_manager_.getChannel(channel_id);
        if (channel) {
            float occupancy = static_cast<float>(channel->memberCount()) /
                             static_cast<float>(channel->maxMembers());

            if (occupancy < config_.scale_up_threshold) {
                return channel_id;
            }
        }
    }

    // 새 채널 생성
    std::string new_channel = createNewChannel();
    if (!new_channel.empty()) {
        managed_channels_.push_back(new_channel);
        LOG_INFO("Created new channel '{}' due to high occupancy (total={})",
                 new_channel, managed_channels_.size());
    }

    return new_channel;
}

std::vector<std::string> WorldChannelManager::getManagedChannels() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return managed_channels_;
}

size_t WorldChannelManager::channelCount() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);
    return managed_channels_.size();
}

std::vector<WorldChannelManager::ChannelStats> WorldChannelManager::getChannelStats() const {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    std::vector<ChannelStats> stats;
    stats.reserve(managed_channels_.size());

    for (const auto& channel_id : managed_channels_) {
        auto* channel = channel_manager_.getChannel(channel_id);
        if (channel) {
            ChannelStats cs;
            cs.channel_id = channel_id;
            cs.member_count = channel->memberCount();
            cs.max_members = channel->maxMembers();
            cs.occupancy_rate = static_cast<float>(cs.member_count) /
                               static_cast<float>(cs.max_members);
            stats.push_back(cs);
        }
    }

    return stats;
}

std::string WorldChannelManager::createNewChannel() {
    int number = next_channel_number_++;

    std::ostringstream oss;
    oss << config_.channel_type << "-" << number;
    std::string channel_id = oss.str();

    std::ostringstream name_oss;
    name_oss << config_.channel_name_prefix << " " << number;
    std::string channel_name = name_oss.str();

    ChannelConfig ch_config;
    ch_config.channel_id = channel_id;
    ch_config.channel_name = channel_name;
    ch_config.max_members = config_.max_members_per_channel;
    ch_config.is_system = config_.is_system;
    ch_config.is_persistent = true;

    auto* channel = channel_manager_.createChannel(ch_config);
    if (channel) {
        LOG_INFO("WorldChannelManager created channel: {} ({})", channel_id, channel_name);
        return channel_id;
    }

    LOG_ERROR("Failed to create channel: {}", channel_id);
    return "";
}

void WorldChannelManager::maintenanceLoop() {
    const auto interval = std::chrono::seconds(config_.maintenance_interval_seconds);

    while (running_) {
        std::this_thread::sleep_for(interval);

        if (!running_) break;

        checkAndScaleUp();
        cleanupEmptyChannels();
    }
}

void WorldChannelManager::checkAndScaleUp() {
    std::shared_lock<std::shared_mutex> lock(channels_mutex_);

    // 모든 채널이 70% 이상인지 체크
    bool all_high_occupancy = true;
    for (const auto& channel_id : managed_channels_) {
        auto* channel = channel_manager_.getChannel(channel_id);
        if (channel) {
            float occupancy = static_cast<float>(channel->memberCount()) /
                             static_cast<float>(channel->maxMembers());

            if (occupancy < config_.scale_up_threshold) {
                all_high_occupancy = false;
                break;
            }
        }
    }

    if (all_high_occupancy) {
        lock.unlock();

        std::unique_lock<std::shared_mutex> write_lock(channels_mutex_);

        // 다시 체크 (race condition 방지)
        bool still_all_high = true;
        for (const auto& channel_id : managed_channels_) {
            auto* channel = channel_manager_.getChannel(channel_id);
            if (channel) {
                float occupancy = static_cast<float>(channel->memberCount()) /
                                 static_cast<float>(channel->maxMembers());
                if (occupancy < config_.scale_up_threshold) {
                    still_all_high = false;
                    break;
                }
            }
        }

        if (still_all_high) {
            std::string new_channel = createNewChannel();
            if (!new_channel.empty()) {
                managed_channels_.push_back(new_channel);
                LOG_INFO("Auto-scaled: created channel '{}' (total={})",
                         new_channel, managed_channels_.size());
            }
        }
    }
}

void WorldChannelManager::cleanupEmptyChannels() {
    std::unique_lock<std::shared_mutex> lock(channels_mutex_);

    // 최소 채널 수보다 많아야 정리 가능
    if (managed_channels_.size() <= static_cast<size_t>(config_.min_channels)) {
        return;
    }

    // 70% 이하인 채널 수 계산
    int low_occupancy_count = 0;
    std::vector<std::string> empty_channels;

    for (const auto& channel_id : managed_channels_) {
        auto* channel = channel_manager_.getChannel(channel_id);
        if (channel) {
            size_t member_count = channel->memberCount();
            float occupancy = static_cast<float>(member_count) /
                             static_cast<float>(channel->maxMembers());

            if (occupancy < config_.scale_up_threshold) {
                low_occupancy_count++;
            }

            if (member_count == 0) {
                empty_channels.push_back(channel_id);
            }
        }
    }

    // 조건: 70% 이하 채널이 cleanup_empty_threshold개 이상이고, 빈 채널이 있을 때
    if (low_occupancy_count >= config_.cleanup_empty_threshold && !empty_channels.empty()) {
        // 뒤쪽 빈 채널부터 삭제 (최소 채널 수 유지)
        std::sort(empty_channels.begin(), empty_channels.end(), std::greater<std::string>());

        for (const auto& channel_id : empty_channels) {
            // 최소 채널 수 확인
            if (managed_channels_.size() <= static_cast<size_t>(config_.min_channels)) {
                break;
            }

            // managed_channels_에서 제거
            auto it = std::find(managed_channels_.begin(), managed_channels_.end(), channel_id);
            if (it != managed_channels_.end()) {
                managed_channels_.erase(it);

                // ChannelManager에서 삭제 시도 (시스템 채널은 삭제 불가할 수 있음)
                // 시스템 채널이지만 동적 생성된 것은 삭제 가능하도록 처리 필요
                // 일단 로그만 남김 (실제 삭제는 ChannelManager 정책에 따름)
                LOG_INFO("Cleaned up empty channel: {} (remaining={})",
                         channel_id, managed_channels_.size());
            }
        }
    }
}

int WorldChannelManager::getNextChannelNumber() {
    return next_channel_number_.load();
}

} // namespace chat
