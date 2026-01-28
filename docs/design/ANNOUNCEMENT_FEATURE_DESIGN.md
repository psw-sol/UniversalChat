# 공지사항 (Announcement) 기능 상세 설계

## 1. 개요

### 1.1 목적
게임 서버에서 채팅 서버를 통해 전체 유저에게 공지사항을 브로드캐스트하는 기능을 구현합니다.

### 1.2 요구사항

| ID | 요구사항 | 우선순위 |
|----|---------|---------|
| REQ-1 | 관리자/게임서버가 전체 유저에게 공지 전송 가능 | 필수 |
| REQ-2 | 공지 타입 구분 (일반, 긴급, 점검) | 필수 |
| REQ-3 | 멀티 서버 환경에서 모든 인스턴스에 전파 | 필수 |
| REQ-4 | 공지 히스토리 저장 (Redis) | 선택 |
| REQ-5 | 특정 채널 대상 공지 | 선택 |

### 1.3 기존 인프라 분석

**활용 가능한 컴포넌트:**
- `SessionManager::broadcast()` - 전체 세션 브로드캐스트 (src/core/SessionManager.hpp:97)
- `PubSubMessageType::SystemBroadcast` - Redis Pub/Sub 타입 정의됨 (src/redis/PubSubMessage.hpp:19)
- `MessageType::SYSTEM` - 시스템 메시지 타입 존재 (proto/chat.proto:44)

**신규 개발 필요:**
- 공지 전용 프로토콜 메시지 (AnnouncementSend, AnnouncementReceive)
- 공지 핸들러 (MessageDispatcher 확장)
- 공지 관리 서비스 (AnnouncementService)

---

## 2. 프로토콜 설계

### 2.1 패킷 타입 추가 (0x06xx - Announcement)

```cpp
// src/protocol/PacketTypes.hpp 에 추가

// === Announcement (0x06xx) ===
AnnouncementSend     = 0x0601,  // 공지 전송 (게임서버/관리자 -> 채팅서버)
AnnouncementReceive  = 0x0602,  // 공지 수신 (채팅서버 -> 클라이언트)
AnnouncementAck      = 0x0603,  // 공지 전송 확인 (채팅서버 -> 게임서버)
```

### 2.2 Protobuf 메시지 정의

```protobuf
// proto/chat.proto 에 추가

// ============================================
// Announcement (0x06xx)
// ============================================

enum AnnouncementType {
    ANNOUNCEMENT_NORMAL = 0;     // 일반 공지
    ANNOUNCEMENT_URGENT = 1;     // 긴급 공지 (빨간색 강조)
    ANNOUNCEMENT_MAINTENANCE = 2; // 점검 공지
    ANNOUNCEMENT_EVENT = 3;      // 이벤트 공지
}

// 0x0601 - AnnouncementSend (게임서버/관리자 -> 채팅서버)
message AnnouncementSend {
    string announcement_id = 1;      // 고유 ID (중복 방지)
    string content = 2;              // 공지 내용
    AnnouncementType type = 3;       // 공지 타입
    string sender_name = 4;          // 발신자 이름 (GM, System 등)
    int32 duration_seconds = 5;      // 표시 지속 시간 (0 = 무제한)
    string target_channel = 6;       // 대상 채널 (빈값 = 전체)
    string admin_token = 7;          // 관리자 인증 토큰
    string extra_data = 8;           // 추가 데이터 (JSON)
}

// 0x0602 - AnnouncementReceive (채팅서버 -> 클라이언트)
message AnnouncementReceive {
    string announcement_id = 1;
    string content = 2;
    AnnouncementType type = 3;
    string sender_name = 4;
    int32 duration_seconds = 5;
    int64 timestamp = 6;             // 전송 시간
    string extra_data = 7;
}

// 0x0603 - AnnouncementAck (채팅서버 -> 게임서버)
message AnnouncementAck {
    bool success = 1;
    string announcement_id = 2;
    int32 delivered_count = 3;       // 전송된 클라이언트 수
    string error_message = 4;
    int32 error_code = 5;
}
```

### 2.3 Redis Pub/Sub Payload

```cpp
// src/redis/PubSubMessage.hpp 에 추가

/**
 * Announcement broadcast payload (for SystemBroadcast type)
 */
struct AnnouncementPayload {
    std::string announcement_id;
    std::string content;
    int announcement_type = 0;  // AnnouncementType enum
    std::string sender_name;
    int duration_seconds = 0;
    std::string target_channel;
    std::string extra_data;

    std::string serialize() const;
    static AnnouncementPayload deserialize(const std::string& json_str);
    static std::optional<AnnouncementPayload> tryDeserialize(const std::string& json_str);
};
```

---

## 3. 서버 컴포넌트 설계

### 3.1 아키텍처 다이어그램

```
┌─────────────────┐     0x0601        ┌─────────────────────────────────┐
│   게임 서버     │ ─────────────────▶│        채팅 서버 인스턴스 1      │
│   (Admin)       │                   │  ┌─────────────────────────────┐│
└─────────────────┘                   │  │ MessageDispatcher           ││
                                      │  │  └─ handleAnnouncement()    ││
                                      │  └──────────────┬──────────────┘│
                                      │                 │               │
                                      │  ┌──────────────▼──────────────┐│
                                      │  │ AnnouncementService         ││
                                      │  │  - validateAnnouncement()   ││
                                      │  │  - broadcastLocal()         ││
                                      │  │  - publishToRedis()         ││
                                      │  └──────────────┬──────────────┘│
                                      │                 │               │
                                      │  ┌──────────────▼──────────────┐│
                                      │  │ SessionManager              ││
                                      │  │  - broadcast(packet)        ││
                                      │  └─────────────────────────────┘│
                                      └─────────────────┬───────────────┘
                                                        │
                                              Redis Pub/Sub (SystemBroadcast)
                                                        │
                                      ┌─────────────────▼───────────────┐
                                      │       채팅 서버 인스턴스 2-N     │
                                      │  ┌─────────────────────────────┐│
                                      │  │ RedisPubSub                 ││
                                      │  │  - onSystemBroadcast()      ││
                                      │  └──────────────┬──────────────┘│
                                      │                 │               │
                                      │  ┌──────────────▼──────────────┐│
                                      │  │ SessionManager              ││
                                      │  │  - broadcast(packet)        ││
                                      │  └─────────────────────────────┘│
                                      └─────────────────────────────────┘
                                                        │
                                                        ▼ 0x0602
                                      ┌─────────────────────────────────┐
                                      │         클라이언트들            │
                                      └─────────────────────────────────┘
```

### 3.2 AnnouncementService 클래스

```cpp
// src/announcement/AnnouncementService.hpp

#pragma once

#include <memory>
#include <string>
#include <optional>
#include <unordered_set>
#include <mutex>

namespace chat {

class SessionManager;
class RedisPubSub;
class Config;

struct AnnouncementRequest {
    std::string announcement_id;
    std::string content;
    int type = 0;
    std::string sender_name;
    int duration_seconds = 0;
    std::string target_channel;
    std::string admin_token;
    std::string extra_data;
};

struct AnnouncementResult {
    bool success = false;
    int delivered_count = 0;
    std::string error_message;
    int error_code = 0;
};

class AnnouncementService {
public:
    AnnouncementService(
        const Config& config,
        std::shared_ptr<SessionManager> session_manager
    );

    // Redis Pub/Sub 설정 (옵션)
    void setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub);

    // 공지 전송 (메인 진입점)
    AnnouncementResult sendAnnouncement(const AnnouncementRequest& request);

    // Redis로부터 수신한 공지 처리
    void handleRemoteAnnouncement(const std::string& payload);

    // 중복 체크
    bool isDuplicate(const std::string& announcement_id) const;

private:
    // 공지 유효성 검증
    bool validateRequest(const AnnouncementRequest& request, std::string& error);

    // 관리자 토큰 검증
    bool validateAdminToken(const std::string& token);

    // 로컬 브로드캐스트
    int broadcastLocal(const AnnouncementRequest& request);

    // 특정 채널 대상 브로드캐스트
    int broadcastToChannel(const AnnouncementRequest& request);

    // Redis로 발행 (멀티 서버 환경)
    void publishToRedis(const AnnouncementRequest& request);

    // 중복 ID 기록
    void recordAnnouncementId(const std::string& id);

private:
    const Config& config_;
    std::shared_ptr<SessionManager> session_manager_;
    std::shared_ptr<RedisPubSub> redis_pubsub_;

    // 중복 방지용 최근 공지 ID 캐시
    mutable std::mutex recent_ids_mutex_;
    std::unordered_set<std::string> recent_announcement_ids_;
    static constexpr size_t MAX_RECENT_IDS = 1000;
};

} // namespace chat
```

### 3.3 MessageDispatcher 확장

```cpp
// src/message/MessageDispatcher.cpp 에 추가

void MessageDispatcher::handleAnnouncementSend(
    SessionPtr session,
    const chat::protocol::AnnouncementSend& request
) {
    // 1. 게임 서버 / 관리자 권한 확인 (인증된 세션만)
    if (!session->isAuthenticated()) {
        sendError(session, ErrorCode::NotAuthenticated, "Authentication required");
        return;
    }

    // 2. AnnouncementService 호출
    AnnouncementRequest req;
    req.announcement_id = request.announcement_id();
    req.content = request.content();
    req.type = static_cast<int>(request.type());
    req.sender_name = request.sender_name();
    req.duration_seconds = request.duration_seconds();
    req.target_channel = request.target_channel();
    req.admin_token = request.admin_token();
    req.extra_data = request.extra_data();

    auto result = announcement_service_->sendAnnouncement(req);

    // 3. 결과 응답
    chat::protocol::AnnouncementAck ack;
    ack.set_success(result.success);
    ack.set_announcement_id(request.announcement_id());
    ack.set_delivered_count(result.delivered_count);
    ack.set_error_message(result.error_message);
    ack.set_error_code(result.error_code);

    session->send(PacketCodec::encode(PacketType::AnnouncementAck, ack));
}
```

### 3.4 Config 확장

```json
// config/server.json 에 추가

{
  "announcement": {
    "enabled": true,
    "admin_tokens": ["secure-token-1", "secure-token-2"],
    "max_content_length": 500,
    "rate_limit_per_minute": 10,
    "duplicate_check_window_seconds": 60,
    "default_duration_seconds": 10
  }
}
```

---

## 4. 클라이언트 연동 설계

### 4.1 Unity ChatManager 확장

```csharp
// Unity: Assets/Plugins/UniversalChat/ChatManager.cs

public class ChatManager : MonoBehaviour
{
    // 공지 수신 이벤트
    public event Action<AnnouncementData> OnAnnouncementReceived;

    // 공지 데이터 구조
    public class AnnouncementData
    {
        public string AnnouncementId { get; set; }
        public string Content { get; set; }
        public AnnouncementType Type { get; set; }
        public string SenderName { get; set; }
        public int DurationSeconds { get; set; }
        public DateTime Timestamp { get; set; }
        public string ExtraData { get; set; }
    }

    public enum AnnouncementType
    {
        Normal = 0,
        Urgent = 1,
        Maintenance = 2,
        Event = 3
    }

    private void HandleAnnouncementReceive(AnnouncementReceive message)
    {
        var data = new AnnouncementData
        {
            AnnouncementId = message.AnnouncementId,
            Content = message.Content,
            Type = (AnnouncementType)message.Type,
            SenderName = message.SenderName,
            DurationSeconds = message.DurationSeconds,
            Timestamp = DateTimeOffset.FromUnixTimeMilliseconds(message.Timestamp).DateTime,
            ExtraData = message.ExtraData
        };

        OnAnnouncementReceived?.Invoke(data);
    }
}
```

### 4.2 UI 표시 예시

```csharp
// Unity: AnnouncementUI.cs

public class AnnouncementUI : MonoBehaviour
{
    [SerializeField] private TMP_Text contentText;
    [SerializeField] private Image backgroundImage;
    [SerializeField] private CanvasGroup canvasGroup;

    private void Start()
    {
        ChatManager.Instance.OnAnnouncementReceived += ShowAnnouncement;
    }

    private void ShowAnnouncement(ChatManager.AnnouncementData data)
    {
        // 타입별 색상 설정
        backgroundImage.color = data.Type switch
        {
            ChatManager.AnnouncementType.Urgent => Color.red,
            ChatManager.AnnouncementType.Maintenance => Color.yellow,
            ChatManager.AnnouncementType.Event => Color.cyan,
            _ => Color.white
        };

        contentText.text = $"[{data.SenderName}] {data.Content}";

        // 표시 및 자동 숨김
        StartCoroutine(ShowAndHide(data.DurationSeconds));
    }

    private IEnumerator ShowAndHide(int durationSeconds)
    {
        canvasGroup.alpha = 1f;
        if (durationSeconds > 0)
        {
            yield return new WaitForSeconds(durationSeconds);
            canvasGroup.alpha = 0f;
        }
    }
}
```

---

## 5. 구현 Task 분할

### Phase 1: 프로토콜 및 기본 구조 (1일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T1.1 | chat.proto에 Announcement 메시지 추가 | 30분 | - |
| T1.2 | PacketTypes.hpp에 패킷 타입 추가 | 15분 | - |
| T1.3 | PubSubMessage.hpp에 AnnouncementPayload 추가 | 30분 | - |
| T1.4 | cmake 빌드 및 protobuf 재생성 확인 | 15분 | T1.1 |

### Phase 2: 서버 구현 (2일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T2.1 | AnnouncementService 클래스 생성 | 2시간 | T1.* |
| T2.2 | AnnouncementService::sendAnnouncement() 구현 | 1시간 | T2.1 |
| T2.3 | AnnouncementService::broadcastLocal() 구현 | 1시간 | T2.2 |
| T2.4 | AnnouncementService::publishToRedis() 구현 | 1시간 | T2.3 |
| T2.5 | MessageDispatcher에 핸들러 등록 | 1시간 | T2.4 |
| T2.6 | RedisPubSub::onSystemBroadcast() 구현 | 1시간 | T2.4 |
| T2.7 | Config에 announcement 설정 추가 | 30분 | - |
| T2.8 | Server 클래스에 AnnouncementService 통합 | 30분 | T2.* |

### Phase 3: 테스트 (1일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T3.1 | AnnouncementService 단위 테스트 작성 | 2시간 | T2.* |
| T3.2 | 싱글 서버 통합 테스트 | 1시간 | T3.1 |
| T3.3 | 멀티 서버 (Redis) 통합 테스트 | 2시간 | T3.2 |
| T3.4 | 부하 테스트 (1000 동시 접속) | 1시간 | T3.3 |

### Phase 4: 클라이언트 연동 (1일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T4.1 | Unity protobuf 파일 갱신 | 30분 | T1.1 |
| T4.2 | ChatManager에 공지 핸들러 추가 | 1시간 | T4.1 |
| T4.3 | 공지 UI 프리팹 생성 | 2시간 | - |
| T4.4 | AnnouncementUI 스크립트 구현 | 2시간 | T4.2, T4.3 |
| T4.5 | 클라이언트-서버 연동 테스트 | 1시간 | T4.4 |

### Phase 5: 문서화 및 마무리 (0.5일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T5.1 | API 문서 작성 | 1시간 | T2.* |
| T5.2 | 운영 가이드 작성 | 1시간 | T4.* |
| T5.3 | CHANGELOG 업데이트 | 15분 | T5.* |

---

## 6. 에러 코드 정의

```cpp
// include/universalchat/Errors.hpp 에 추가

// Announcement errors (7000-7099)
constexpr int ERR_ANNOUNCEMENT_INVALID_TOKEN = 7001;
constexpr int ERR_ANNOUNCEMENT_EMPTY_CONTENT = 7002;
constexpr int ERR_ANNOUNCEMENT_CONTENT_TOO_LONG = 7003;
constexpr int ERR_ANNOUNCEMENT_RATE_LIMITED = 7004;
constexpr int ERR_ANNOUNCEMENT_DUPLICATE_ID = 7005;
constexpr int ERR_ANNOUNCEMENT_CHANNEL_NOT_FOUND = 7006;
```

---

## 7. 보안 고려사항

1. **인증 필수**: 공지 전송은 관리자 토큰 검증 필수
2. **Rate Limiting**: 분당 최대 전송 횟수 제한 (기본 10회)
3. **내용 길이 제한**: 최대 500자 (설정 가능)
4. **중복 방지**: announcement_id 기반 중복 체크 (60초 윈도우)
5. **로깅**: 모든 공지 전송 기록 (audit log)

---

## 8. 참고 파일

| 파일 | 설명 |
|------|-----|
| `src/core/SessionManager.hpp:97` | broadcast() 메서드 |
| `src/redis/PubSubMessage.hpp:19` | SystemBroadcast 타입 |
| `proto/chat.proto:44` | MessageType::SYSTEM |
| `src/message/MessageDispatcher.cpp` | 메시지 핸들러 등록 패턴 |
| `src/channel/Channel.cpp:108-148` | 채널 브로드캐스트 구현 참고 |

---

**작성일**: 2026-01-28
**작성자**: Claude Code
**버전**: 1.0
