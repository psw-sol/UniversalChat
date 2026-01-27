# UniversalChatServer Scale-Out Architecture Design

## 문서 정보

| 항목 | 내용 |
|------|------|
| 버전 | 1.0.0 |
| 작성일 | 2026-01-21 |
| 상태 | **Implemented** |
| 구현일 | 2026-01-21 |

---

## 1. 개요

### 1.1 목표

현재 단일 인스턴스 채팅 서버를 수평 확장(Scale-Out) 가능한 분산 시스템으로 전환

### 1.2 범위

- Redis Pub/Sub 기반 메시지 브로드캐스트
- 글로벌 세션 레지스트리
- 채널 멤버십 동기화
- 서버 간 귓속말(Whisper) 라우팅

### 1.3 전체 아키텍처

```
┌─────────────────────────────────────────────────────────────────────────┐
│                              Clients                                     │
└───────────────────────────────────┬─────────────────────────────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │     Nginx (IP Hash LB)        │
                    └───────────────┬───────────────┘
                                    │
        ┌───────────────────────────┼───────────────────────────┐
        │                           │                           │
┌───────▼───────┐           ┌───────▼───────┐           ┌───────▼───────┐
│   Server #1   │           │   Server #2   │           │   Server #3   │
│ ┌───────────┐ │           │ ┌───────────┐ │           │ ┌───────────┐ │
│ │ Sessions  │ │           │ │ Sessions  │ │           │ │ Sessions  │ │
│ │ Channels  │ │           │ │ Channels  │ │           │ │ Channels  │ │
│ │ PubSub    │ │           │ │ PubSub    │ │           │ │ PubSub    │ │
│ │ Subscriber│ │           │ │ Subscriber│ │           │ │ Subscriber│ │
│ └─────┬─────┘ │           │ └─────┬─────┘ │           │ └─────┬─────┘ │
└───────┼───────┘           └───────┼───────┘           └───────┼───────┘
        │                           │                           │
        └───────────────────────────┼───────────────────────────┘
                                    │
                    ┌───────────────▼───────────────┐
                    │         Redis Cluster         │
                    │ ┌─────────────────────────┐   │
                    │ │ Pub/Sub Channels        │   │
                    │ │ - chat:channel:{id}     │   │
                    │ │ - chat:whisper:{userId} │   │
                    │ │ - chat:system           │   │
                    │ ├─────────────────────────┤   │
                    │ │ Session Registry        │   │
                    │ │ - chat:sessions (Hash)  │   │
                    │ ├─────────────────────────┤   │
                    │ │ Channel Members         │   │
                    │ │ - chat:members:{id}     │   │
                    │ └─────────────────────────┘   │
                    └───────────────────────────────┘
```

---

## 2. Phase 별 설계

---

## Phase 1: Redis Pub/Sub 인프라

### 2.1.1 목표

- Redis Pub/Sub 기능 추가 (PUBLISH, SUBSCRIBE, PSUBSCRIBE)
- 비동기 구독 리스너 구현
- 메시지 직렬화/역직렬화

### 2.1.2 새 파일 목록

| 파일 | 설명 |
|------|------|
| `src/redis/RedisPubSub.hpp` | Pub/Sub 클라이언트 헤더 |
| `src/redis/RedisPubSub.cpp` | Pub/Sub 클라이언트 구현 |
| `src/redis/PubSubMessage.hpp` | Pub/Sub 메시지 구조체 |

### 2.1.3 클래스 설계

```cpp
// src/redis/PubSubMessage.hpp
namespace chat {

/**
 * Pub/Sub 메시지 타입
 */
enum class PubSubMessageType : uint8_t {
    ChannelMessage = 1,     // 채널 메시지
    ChannelJoin = 2,        // 채널 입장 알림
    ChannelLeave = 3,       // 채널 퇴장 알림
    Whisper = 4,            // 귓속말
    ProfileUpdate = 5,      // 프로필 업데이트
    SystemBroadcast = 6     // 시스템 브로드캐스트
};

/**
 * Pub/Sub 메시지 구조체
 */
struct PubSubMessage {
    PubSubMessageType type;
    std::string origin_server_id;   // 발신 서버 ID (중복 처리 방지)
    std::string channel_id;         // 대상 채널
    std::string sender_session_id;  // 발신자 세션 (로컬 전송 제외용)
    std::string payload;            // JSON 직렬화된 데이터
    int64_t timestamp;

    // JSON 직렬화/역직렬화
    std::string serialize() const;
    static PubSubMessage deserialize(const std::string& json);
};

} // namespace chat
```

```cpp
// src/redis/RedisPubSub.hpp
namespace chat {

class RedisClient;

/**
 * Redis Pub/Sub 클라이언트
 *
 * 별도의 Redis 연결을 사용하여 비동기 구독 처리
 * (hiredis에서 SUBSCRIBE 후에는 해당 연결로 다른 명령 불가)
 */
class RedisPubSub {
public:
    using MessageHandler = std::function<void(const std::string& channel,
                                               const PubSubMessage& message)>;

    struct Config {
        std::string host = "127.0.0.1";
        int port = 6379;
        std::string password;
        int reconnect_interval_ms = 5000;
        std::string server_id;  // 현재 서버 고유 ID
    };

    explicit RedisPubSub(const Config& config);
    ~RedisPubSub();

    // 연결 관리
    bool connect();
    void disconnect();
    bool isConnected() const;

    // 발행 (별도 RedisClient 사용)
    bool publish(const std::string& channel, const PubSubMessage& message);

    // 구독 관리
    bool subscribe(const std::string& channel);
    bool psubscribe(const std::string& pattern);  // 패턴 구독 (chat:channel:*)
    bool unsubscribe(const std::string& channel);
    bool punsubscribe(const std::string& pattern);

    // 메시지 핸들러 등록
    void setMessageHandler(MessageHandler handler);

    // 구독 루프 시작/중지 (별도 스레드)
    void startListening();
    void stopListening();

    // 현재 서버 ID
    const std::string& serverId() const { return config_.server_id; }

private:
    void listenLoop();
    void processMessage(redisReply* reply);
    bool reconnect();

    Config config_;

    // 발행용 Redis 클라이언트 (기존 RedisClient 공유)
    std::shared_ptr<RedisClient> publish_client_;

    // 구독용 별도 연결
    redisContext* subscribe_context_ = nullptr;

    std::atomic<bool> running_{false};
    std::thread listen_thread_;
    mutable std::mutex mutex_;

    std::set<std::string> subscribed_channels_;
    std::set<std::string> subscribed_patterns_;
    MessageHandler message_handler_;
};

} // namespace chat
```

### 2.1.4 토픽 구조

| 토픽 패턴 | 용도 | 예시 |
|-----------|------|------|
| `chat:channel:{channel_id}` | 채널 메시지 | `chat:channel:world-1` |
| `chat:whisper:{user_id}` | 귓속말 | `chat:whisper:user123` |
| `chat:system` | 시스템 브로드캐스트 | 서버 공지 등 |
| `chat:presence` | 접속/퇴장 알림 | 친구 목록용 (향후) |

### 2.1.5 구현 태스크

| Task ID | 설명 | 예상 시간 | 의존성 |
|---------|------|-----------|--------|
| P1-T1 | `PubSubMessage` 구조체 및 JSON 직렬화 | 2h | - |
| P1-T2 | `RedisPubSub` 클래스 헤더 | 1h | P1-T1 |
| P1-T3 | `RedisPubSub::connect/disconnect` | 2h | P1-T2 |
| P1-T4 | `RedisPubSub::publish` | 1h | P1-T3 |
| P1-T5 | `RedisPubSub::subscribe/psubscribe` | 2h | P1-T3 |
| P1-T6 | `RedisPubSub::listenLoop` (비동기) | 3h | P1-T5 |
| P1-T7 | 재연결 로직 및 에러 처리 | 2h | P1-T6 |
| P1-T8 | 단위 테스트 작성 | 2h | P1-T7 |

**Phase 1 총 예상 시간: 15시간 (약 2일)**

---

## Phase 2: 메시지 브로드캐스트 분산화

### 2.2.1 목표

- 채널 메시지를 로컬 + Redis Pub/Sub로 이중 전송
- 다른 서버에서 수신한 메시지를 로컬 멤버에게 전달
- 중복 전송 방지 (origin_server_id 체크)

### 2.2.2 수정 대상 파일

| 파일 | 수정 내용 |
|------|-----------|
| `src/channel/Channel.hpp` | `RedisPubSub` 의존성 추가 |
| `src/channel/Channel.cpp` | `broadcast()` 로직 변경 |
| `src/channel/ChannelManager.hpp` | `RedisPubSub` 초기화 |
| `src/channel/ChannelManager.cpp` | Pub/Sub 메시지 수신 핸들러 |
| `src/message/MessageDispatcher.cpp` | 메시지 전송 시 Pub/Sub 활용 |

### 2.2.3 변경 설계

#### Channel::broadcast() 변경

```cpp
// 현재 (로컬 전송만)
void Channel::broadcast(const Packet& packet, const std::string& exclude_session) {
    auto data = PacketCodec::encode(packet);
    for (const auto& [id, session] : members_) {
        if (id != exclude_session && session->isConnected()) {
            session->sendRaw(data);
        }
    }
}

// 변경 후 (로컬 + Pub/Sub)
void Channel::broadcast(const Packet& packet,
                        const std::string& exclude_session,
                        bool publish_to_redis = true) {
    auto data = PacketCodec::encode(packet);

    // 1. 로컬 멤버에게 전송
    {
        std::shared_lock<std::shared_mutex> lock(members_mutex_);
        for (const auto& [id, session] : members_) {
            if (id != exclude_session && session->isConnected()) {
                session->sendRaw(data);
            }
        }
    }

    // 2. Redis Pub/Sub로 다른 서버에 발행
    if (publish_to_redis && pubsub_) {
        PubSubMessage msg;
        msg.type = PubSubMessageType::ChannelMessage;
        msg.origin_server_id = pubsub_->serverId();
        msg.channel_id = config_.channel_id;
        msg.sender_session_id = exclude_session;
        msg.payload = /* packet을 JSON으로 변환 */;
        msg.timestamp = /* 현재 타임스탬프 */;

        pubsub_->publish("chat:channel:" + config_.channel_id, msg);
    }
}

// Pub/Sub 메시지 수신 핸들러 (ChannelManager에서 호출)
void Channel::onPubSubMessage(const PubSubMessage& msg) {
    // 자신이 보낸 메시지는 무시 (중복 방지)
    if (msg.origin_server_id == pubsub_->serverId()) {
        return;
    }

    // JSON에서 Packet 복원
    Packet packet = /* msg.payload에서 복원 */;

    // 로컬 멤버에게만 전송 (Redis 재발행 안함)
    broadcast(packet, msg.sender_session_id, false);
}
```

### 2.2.4 시퀀스 다이어그램

```
User A (Server 1)          Server 1             Redis            Server 2          User B (Server 2)
     │                        │                   │                  │                    │
     │─── Send Message ──────▶│                   │                  │                    │
     │                        │                   │                  │                    │
     │                        │─── Broadcast ────▶│ (Local Users)    │                    │
     │                        │                   │                  │                    │
     │                        │─── PUBLISH ──────▶│                  │                    │
     │                        │   chat:channel:X  │                  │                    │
     │                        │                   │                  │                    │
     │                        │                   │─── Notify ──────▶│ (Subscriber)      │
     │                        │                   │                  │                    │
     │                        │                   │                  │─── Broadcast ─────▶│
     │                        │                   │                  │   (Local Users)    │
     │                        │                   │                  │                    │
```

### 2.2.5 구현 태스크

| Task ID | 설명 | 예상 시간 | 의존성 |
|---------|------|-----------|--------|
| P2-T1 | `Channel` 클래스에 `RedisPubSub` 의존성 추가 | 1h | Phase 1 |
| P2-T2 | `Channel::broadcast()` 이중 전송 로직 | 2h | P2-T1 |
| P2-T3 | `Channel::onPubSubMessage()` 핸들러 | 2h | P2-T2 |
| P2-T4 | `ChannelManager`에 Pub/Sub 초기화 및 구독 | 2h | P2-T3 |
| P2-T5 | Packet ↔ JSON 변환 유틸리티 | 2h | - |
| P2-T6 | 채널 Join/Leave 알림 Pub/Sub | 2h | P2-T4 |
| P2-T7 | 통합 테스트 (2개 서버 인스턴스) | 3h | P2-T6 |

**Phase 2 총 예상 시간: 14시간 (약 2일)**

---

## Phase 3: 글로벌 세션 레지스트리

### 2.3.1 목표

- Redis에 전역 세션 정보 저장
- 귓속말 대상 사용자의 서버 위치 조회
- 서버 간 귓속말 라우팅

### 2.3.2 새 파일 목록

| 파일 | 설명 |
|------|------|
| `src/redis/SessionRegistry.hpp` | 세션 레지스트리 헤더 |
| `src/redis/SessionRegistry.cpp` | 세션 레지스트리 구현 |

### 2.3.3 Redis 데이터 구조

```
# 세션 정보 (Hash)
HSET chat:sessions:{user_id} server_id "server-1"
HSET chat:sessions:{user_id} session_id "sess-abc123"
HSET chat:sessions:{user_id} nickname "PlayerOne"
HSET chat:sessions:{user_id} connected_at "1705827600"

# TTL 설정 (하트비트 시 갱신)
EXPIRE chat:sessions:{user_id} 120
```

### 2.3.4 클래스 설계

```cpp
// src/redis/SessionRegistry.hpp
namespace chat {

/**
 * 글로벌 세션 레지스트리
 *
 * Redis에 세션 정보를 저장하여 서버 간 세션 조회 가능
 */
class SessionRegistry {
public:
    struct SessionInfo {
        std::string user_id;
        std::string session_id;
        std::string server_id;
        std::string nickname;
        int64_t connected_at;
    };

    explicit SessionRegistry(std::shared_ptr<RedisClient> redis,
                             const std::string& server_id);

    // 세션 등록/해제
    bool registerSession(const std::string& user_id,
                         const std::string& session_id,
                         const std::string& nickname);
    bool unregisterSession(const std::string& user_id);

    // 세션 조회
    std::optional<SessionInfo> getSession(const std::string& user_id) const;
    bool isUserOnline(const std::string& user_id) const;

    // 하트비트 갱신 (TTL 연장)
    bool refreshSession(const std::string& user_id);

    // 닉네임으로 사용자 검색 (정확히 일치)
    std::optional<SessionInfo> findByNickname(const std::string& nickname) const;

private:
    std::string makeKey(const std::string& user_id) const;
    std::string makeNicknameIndex(const std::string& nickname) const;

    std::shared_ptr<RedisClient> redis_;
    std::string server_id_;
    int ttl_seconds_ = 120;

    static constexpr const char* KEY_PREFIX = "chat:sessions:";
    static constexpr const char* NICKNAME_INDEX_PREFIX = "chat:nickname:";
};

} // namespace chat
```

### 2.3.5 귓속말 라우팅 설계

```cpp
// MessageDispatcher::handleWhisperSend() 변경

void MessageDispatcher::handleWhisperSend(SessionPtr session, const Packet& packet) {
    // 1. 대상 사용자 검색
    auto target_info = session_registry_->findByNickname(request.target_nickname());

    if (!target_info) {
        sendError(session, ErrorCode::UserNotFound, "User not found");
        return;
    }

    // 2. 대상이 같은 서버에 있는지 확인
    if (target_info->server_id == pubsub_->serverId()) {
        // 로컬 전송
        auto target_session = session_manager_.getByUserId(target_info->user_id);
        if (target_session) {
            target_session->send(whisper_packet);
        }
    } else {
        // 3. 다른 서버에 Pub/Sub로 전달
        PubSubMessage msg;
        msg.type = PubSubMessageType::Whisper;
        msg.origin_server_id = pubsub_->serverId();
        msg.payload = /* 귓속말 데이터 JSON */;

        pubsub_->publish("chat:whisper:" + target_info->user_id, msg);
    }

    // 4. 발신자에게 전송 확인 응답
    sendWhisperResponse(session, WhisperResult::Success);
}
```

### 2.3.6 구현 태스크

| Task ID | 설명 | 예상 시간 | 의존성 |
|---------|------|-----------|--------|
| P3-T1 | `SessionRegistry` 클래스 헤더 | 1h | Phase 1 |
| P3-T2 | `registerSession/unregisterSession` | 2h | P3-T1 |
| P3-T3 | `getSession/isUserOnline` | 1h | P3-T2 |
| P3-T4 | `refreshSession` (하트비트 통합) | 2h | P3-T3 |
| P3-T5 | 닉네임 인덱스 및 `findByNickname` | 2h | P3-T3 |
| P3-T6 | `SessionManager` 연동 (등록/해제 호출) | 2h | P3-T4 |
| P3-T7 | `handleWhisperSend` 라우팅 로직 변경 | 3h | P3-T5, Phase 1 |
| P3-T8 | 귓속말 Pub/Sub 구독 및 핸들러 | 2h | P3-T7 |
| P3-T9 | 통합 테스트 (서버 간 귓속말) | 2h | P3-T8 |

**Phase 3 총 예상 시간: 17시간 (약 2-3일)**

---

## Phase 4: 채널 멤버십 동기화

### 2.4.1 목표

- Redis에 채널별 글로벌 멤버 수 저장
- 채널 목록 조회 시 정확한 멤버 수 표시
- Join/Leave 시 Redis 동기화

### 2.4.2 새 파일 목록

| 파일 | 설명 |
|------|------|
| `src/redis/ChannelRegistry.hpp` | 채널 레지스트리 헤더 |
| `src/redis/ChannelRegistry.cpp` | 채널 레지스트리 구현 |

### 2.4.3 Redis 데이터 구조

```
# 채널 멤버 목록 (Set)
SADD chat:channel:world-1:members "user123"
SADD chat:channel:world-1:members "user456"

# 채널 멤버 수 (즉시 조회용 캐시)
INCR chat:channel:world-1:count

# 채널 메타데이터 (Hash)
HSET chat:channels:world-1 name "World Chat #1"
HSET chat:channels:world-1 max_members "1000"
HSET chat:channels:world-1 created_at "1705827600"
```

### 2.4.4 클래스 설계

```cpp
// src/redis/ChannelRegistry.hpp
namespace chat {

/**
 * 글로벌 채널 레지스트리
 *
 * Redis에 채널 멤버십 정보를 저장하여 정확한 글로벌 멤버 수 제공
 */
class ChannelRegistry {
public:
    explicit ChannelRegistry(std::shared_ptr<RedisClient> redis);

    // 멤버 관리
    bool addMember(const std::string& channel_id, const std::string& user_id);
    bool removeMember(const std::string& channel_id, const std::string& user_id);
    bool isMember(const std::string& channel_id, const std::string& user_id) const;

    // 멤버 수 조회
    int64_t getMemberCount(const std::string& channel_id) const;
    std::vector<std::string> getMembers(const std::string& channel_id,
                                         int offset = 0,
                                         int count = 100) const;

    // 채널 메타데이터
    bool setChannelMetadata(const std::string& channel_id,
                            const ChannelConfig& config);
    std::optional<ChannelConfig> getChannelMetadata(const std::string& channel_id) const;

    // 사용자가 속한 채널 목록
    std::vector<std::string> getUserChannels(const std::string& user_id) const;

    // 정리 (서버 종료 시)
    void cleanupServerMembers(const std::string& server_id);

private:
    std::string makeMembersKey(const std::string& channel_id) const;
    std::string makeCountKey(const std::string& channel_id) const;
    std::string makeMetadataKey(const std::string& channel_id) const;
    std::string makeUserChannelsKey(const std::string& user_id) const;

    std::shared_ptr<RedisClient> redis_;

    static constexpr const char* MEMBERS_PREFIX = "chat:channel:";
    static constexpr const char* MEMBERS_SUFFIX = ":members";
};

} // namespace chat
```

### 2.4.5 ChannelManager 연동

```cpp
// ChannelManager::joinChannel() 변경

Channel::JoinResult ChannelManager::joinChannel(const std::string& channel_id,
                                                 SessionPtr session,
                                                 const std::string& password) {
    // ... 기존 로직 ...

    auto result = channel->addMember(session, password);

    if (result == Channel::JoinResult::Success) {
        // Redis 동기화
        if (channel_registry_) {
            channel_registry_->addMember(channel_id, session->userId());
        }

        // Pub/Sub로 Join 알림 (다른 서버에서 채널 목록 갱신용)
        if (pubsub_) {
            PubSubMessage msg;
            msg.type = PubSubMessageType::ChannelJoin;
            msg.channel_id = channel_id;
            msg.payload = /* 사용자 정보 JSON */;
            pubsub_->publish("chat:channel:" + channel_id, msg);
        }
    }

    return result;
}
```

### 2.4.6 구현 태스크

| Task ID | 설명 | 예상 시간 | 의존성 |
|---------|------|-----------|--------|
| P4-T1 | `ChannelRegistry` 클래스 헤더 | 1h | Phase 1 |
| P4-T2 | `addMember/removeMember` | 2h | P4-T1 |
| P4-T3 | `getMemberCount/getMembers` | 1h | P4-T2 |
| P4-T4 | 채널 메타데이터 관리 | 2h | P4-T3 |
| P4-T5 | `getUserChannels` (사용자 채널 목록) | 1h | P4-T3 |
| P4-T6 | `ChannelManager` 연동 (Join/Leave) | 2h | P4-T4 |
| P4-T7 | `handleChannelList` 응답에 글로벌 멤버 수 반영 | 1h | P4-T6 |
| P4-T8 | 서버 종료 시 정리 로직 | 2h | P4-T6 |
| P4-T9 | 통합 테스트 | 2h | P4-T8 |

**Phase 4 총 예상 시간: 14시간 (약 2일)**

---

## 3. 설정 변경

### 3.1 server.json 확장

```json
{
    "server": {
        "host": "0.0.0.0",
        "port": 7777,
        "io_threads": 4,
        "server_id": "server-1"  // 추가: 서버 고유 ID
    },

    "redis": {
        "enabled": true,
        "host": "127.0.0.1",
        "port": 6379,
        "password": "",
        "db": 0,
        "connect_timeout_ms": 5000,
        "command_timeout_ms": 1000
    },

    "pubsub": {                      // 추가: Pub/Sub 설정
        "enabled": true,
        "reconnect_interval_ms": 5000,
        "channel_patterns": [
            "chat:channel:*",
            "chat:whisper:*",
            "chat:system"
        ]
    },

    "session_registry": {            // 추가: 세션 레지스트리 설정
        "enabled": true,
        "ttl_seconds": 120,
        "refresh_on_heartbeat": true
    },

    "channel_registry": {            // 추가: 채널 레지스트리 설정
        "enabled": true,
        "sync_member_count": true
    }
}
```

---

## 4. 전체 태스크 요약

### 4.1 Phase별 태스크 수 및 시간

| Phase | 태스크 수 | 예상 시간 | 누적 시간 |
|-------|----------|-----------|-----------|
| Phase 1 (Pub/Sub 인프라) | 8 | 15h | 15h |
| Phase 2 (메시지 분산화) | 7 | 14h | 29h |
| Phase 3 (세션 레지스트리) | 9 | 17h | 46h |
| Phase 4 (채널 멤버십) | 9 | 14h | 60h |
| **합계** | **33** | **60h** | **약 8-10일** |

### 4.2 의존성 그래프

```
Phase 1 (P1-T1 ~ P1-T8)
    │
    ├───────────────────────────────┐
    ▼                               ▼
Phase 2 (P2-T1 ~ P2-T7)      Phase 3 (P3-T1 ~ P3-T5)
    │                               │
    │                               ▼
    │                        Phase 3 (P3-T6 ~ P3-T9)
    │                               │
    └───────────────┬───────────────┘
                    ▼
             Phase 4 (P4-T1 ~ P4-T9)
```

### 4.3 병렬 작업 가능 영역

- **Phase 2**와 **Phase 3 (T1~T5)**: Phase 1 완료 후 병렬 진행 가능
- **P2-T5** (Packet ↔ JSON 변환)는 독립적으로 선행 가능

---

## 5. 테스트 전략

### 5.1 단위 테스트

| 대상 | 테스트 항목 |
|------|-------------|
| `PubSubMessage` | JSON 직렬화/역직렬화 |
| `RedisPubSub` | connect, publish, subscribe |
| `SessionRegistry` | register, unregister, find |
| `ChannelRegistry` | addMember, removeMember, count |

### 5.2 통합 테스트

| 시나리오 | 검증 항목 |
|----------|-----------|
| 멀티 서버 채팅 | Server1 사용자가 보낸 메시지를 Server2 사용자가 수신 |
| 서버 간 귓속말 | Server1 → Server2 귓속말 전달 |
| 채널 멤버 수 | 2개 서버에서 동일 채널 Join 후 정확한 멤버 수 |
| 장애 복구 | Redis 연결 끊김 후 재연결 시 정상 동작 |

### 5.3 부하 테스트

```yaml
# artillery 시나리오 추가
scenarios:
  - name: "Multi-server chat"
    engine: custom
    flow:
      - connect: { server: "server1:7777" }
      - authenticate: { user_id: "user_{{ $randomNumber }}" }
      - join_channel: { channel: "world-1" }
      - loop:
        - send_message: { content: "Hello from server1" }
        - think: 1
        count: 50
```

---

## 6. 롤백 계획

각 Phase는 독립적으로 비활성화 가능:

```json
{
    "pubsub": { "enabled": false },
    "session_registry": { "enabled": false },
    "channel_registry": { "enabled": false }
}
```

- `enabled: false` 시 기존 단일 서버 모드로 동작
- 점진적 롤아웃 가능 (일부 서버만 분산 모드 활성화)

---

## 부록 A: 파일 목록 요약

### 신규 파일

| 경로 | 설명 |
|------|------|
| `src/redis/PubSubMessage.hpp` | Pub/Sub 메시지 구조체 |
| `src/redis/RedisPubSub.hpp` | Pub/Sub 클라이언트 헤더 |
| `src/redis/RedisPubSub.cpp` | Pub/Sub 클라이언트 구현 |
| `src/redis/SessionRegistry.hpp` | 세션 레지스트리 헤더 |
| `src/redis/SessionRegistry.cpp` | 세션 레지스트리 구현 |
| `src/redis/ChannelRegistry.hpp` | 채널 레지스트리 헤더 |
| `src/redis/ChannelRegistry.cpp` | 채널 레지스트리 구현 |

### 수정 파일

| 경로 | 수정 내용 |
|------|-----------|
| `src/channel/Channel.hpp/cpp` | Pub/Sub 브로드캐스트 |
| `src/channel/ChannelManager.hpp/cpp` | 레지스트리 연동 |
| `src/core/SessionManager.hpp/cpp` | 세션 레지스트리 연동 |
| `src/message/MessageDispatcher.cpp` | 귓속말 라우팅 |
| `src/util/Config.hpp/cpp` | 새 설정 항목 |
| `CMakeLists.txt` | 새 소스 파일 추가 |

---

## 부록 B: 에러 코드 추가

```cpp
// include/universalchat/Errors.hpp 추가

namespace ErrorCode {
    // Scale-out related errors (7xx)
    constexpr int PUBSUB_PUBLISH_FAILED = 701;
    constexpr int PUBSUB_NOT_CONNECTED = 702;
    constexpr int SESSION_REGISTRY_ERROR = 703;
    constexpr int CHANNEL_REGISTRY_ERROR = 704;
    constexpr int CROSS_SERVER_ROUTING_FAILED = 705;
}
```
