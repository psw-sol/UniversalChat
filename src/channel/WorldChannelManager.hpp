#pragma once

#include "ChannelManager.hpp"
#include <string>
#include <vector>
#include <shared_mutex>
#include <atomic>
#include <thread>
#include <chrono>

namespace chat {

class Config;

/**
 * WorldChannelManager
 *
 * 월드 채널 동적 샤딩 관리
 * - 초기 최소 채널 수 유지 (기본 3개)
 * - 수용 인원 70% 도달 시 채널 자동 확장
 * - 유휴 채널 자동 정리 (0명 채널 삭제, 최소 채널 수 유지)
 */
class WorldChannelManager {
public:
    struct Config {
        std::string channel_type = "world";      // 채널 타입 (world, trade 등)
        std::string channel_name_prefix = "World Chat";  // 채널 이름 접두사
        int min_channels = 3;                    // 최소 채널 수
        int max_members_per_channel = 1000;      // 채널당 최대 인원
        float scale_up_threshold = 0.7f;         // 확장 임계치 (70%)
        int cleanup_empty_threshold = 4;         // 빈 채널 정리 기준 (70% 이하 채널 4개 이상)
        int maintenance_interval_seconds = 30;   // 유지보수 체크 간격
        bool is_system = true;                   // 시스템 채널 여부
    };

    WorldChannelManager(ChannelManager& channel_manager, const Config& config);
    ~WorldChannelManager();

    // Non-copyable
    WorldChannelManager(const WorldChannelManager&) = delete;
    WorldChannelManager& operator=(const WorldChannelManager&) = delete;

    /**
     * 초기 채널 생성 (서버 시작 시 호출)
     */
    void initialize();

    /**
     * 유지보수 스레드 시작
     */
    void startMaintenance();

    /**
     * 유지보수 스레드 중지
     */
    void stopMaintenance();

    /**
     * 가장 적합한 채널 선택 (자동 배정용)
     * - 70% 이하인 채널 중 앞쪽 채널 우선
     * - 모든 채널이 70% 초과면 새 채널 생성 후 반환
     * @return 선택된 채널 ID (예: "world-1")
     */
    std::string selectBestChannel();

    /**
     * 현재 관리 중인 채널 목록
     */
    std::vector<std::string> getManagedChannels() const;

    /**
     * 채널 수
     */
    size_t channelCount() const;

    /**
     * 채널 통계 정보
     */
    struct ChannelStats {
        std::string channel_id;
        size_t member_count;
        int max_members;
        float occupancy_rate;  // 점유율 (0.0 ~ 1.0)
    };
    std::vector<ChannelStats> getChannelStats() const;

private:
    /**
     * 새 채널 생성
     * @return 생성된 채널 ID
     */
    std::string createNewChannel();

    /**
     * 유지보수 루프 (확장/정리)
     */
    void maintenanceLoop();

    /**
     * 채널 확장 체크 및 실행
     */
    void checkAndScaleUp();

    /**
     * 유휴 채널 정리
     */
    void cleanupEmptyChannels();

    /**
     * 다음 채널 번호 생성
     */
    int getNextChannelNumber();

    ChannelManager& channel_manager_;
    Config config_;

    mutable std::shared_mutex channels_mutex_;
    std::vector<std::string> managed_channels_;  // 관리 중인 채널 ID 목록
    std::atomic<int> next_channel_number_{1};

    std::atomic<bool> running_{false};
    std::thread maintenance_thread_;
};

} // namespace chat
