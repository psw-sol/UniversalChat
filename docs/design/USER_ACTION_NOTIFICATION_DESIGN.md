# 유저 행동 알림 (User Action Notification) 기능 상세 설계

## 1. 개요

### 1.1 목적
특정 유저의 행동(아이템 획득, 랭킹 돌파, 미션 달성 등)을 다른 유저에게 채팅 또는 스플래시 팝업으로 알리는 기능을 구현합니다.

### 1.2 사용자 선택 시나리오

| 시나리오 | 채팅 알림 | 스플래시 팝업 | 구현 방법 |
|---------|----------|--------------|----------|
| 1. 팝업만 | ❌ | ✅ | 이벤트에 팝업 함수 연결 |
| 2. 채팅만 | ✅ | ❌ | 이벤트 연결 안함 (기본 동작) |
| 3. 둘 다 | ✅ | ✅ | 이벤트에 팝업 함수 연결 |

### 1.3 핵심 설계 원칙

```
┌─────────────────────────────────────────────────────────────────────┐
│                        서버 → 클라이언트                             │
│                                                                     │
│  UserActionNotification 패킷 수신                                   │
│         │                                                           │
│         ▼                                                           │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │              NotificationManager                            │   │
│  │  ┌───────────────────────────────────────────────────────┐  │   │
│  │  │  1. 채팅 메시지로 자동 표시 (기본 동작)                 │  │   │
│  │  │     - 채팅창에 시스템 메시지로 표시                     │  │   │
│  │  │     - 사용자가 비활성화 할 수 없음 (핵심 기능)          │  │   │
│  │  └───────────────────────────────────────────────────────┘  │   │
│  │                          │                                   │   │
│  │                          ▼                                   │   │
│  │  ┌───────────────────────────────────────────────────────┐  │   │
│  │  │  2. OnUserActionNotification 이벤트 발행               │  │   │
│  │  │     - 사용자가 원하면 구독하여 팝업 등 추가 처리       │  │   │
│  │  │     - 구독 안 하면 채팅 알림만 동작                    │  │   │
│  │  └───────────────────────────────────────────────────────┘  │   │
│  └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 1.4 알림 타입

| 타입 | 설명 | 예시 |
|------|-----|------|
| `ITEM_ACQUIRE` | 레어 아이템 획득 | "[GM] Player1님이 전설 검을 획득했습니다!" |
| `RANKING_BREAKTHROUGH` | 랭킹 돌파 | "[랭킹] Player2님이 1위를 달성했습니다!" |
| `MISSION_COMPLETE` | 최고 미션 달성 | "[미션] Player3님이 던전 100층을 클리어했습니다!" |
| `ACHIEVEMENT` | 업적 달성 | "[업적] Player4님이 '전설의 영웅' 칭호를 획득했습니다!" |
| `CUSTOM` | 커스텀 알림 | 게임서버 자유 정의 |

---

## 2. 프로토콜 설계

### 2.1 패킷 타입 추가 (0x07xx - UserAction)

```cpp
// src/protocol/PacketTypes.hpp 에 추가

// === UserAction (0x07xx) ===
UserActionNotificationSend    = 0x0701,  // 게임서버 → 채팅서버
UserActionNotificationReceive = 0x0702,  // 채팅서버 → 클라이언트
UserActionNotificationAck     = 0x0703,  // 채팅서버 → 게임서버
```

### 2.2 Protobuf 메시지 정의

```protobuf
// proto/chat.proto 에 추가

// ============================================
// User Action Notification (0x07xx)
// ============================================

enum UserActionType {
    ACTION_ITEM_ACQUIRE = 0;         // 레어 아이템 획득
    ACTION_RANKING_BREAKTHROUGH = 1; // 랭킹 돌파
    ACTION_MISSION_COMPLETE = 2;     // 최고 미션 달성
    ACTION_ACHIEVEMENT = 3;          // 업적 달성
    ACTION_CUSTOM = 99;              // 커스텀 (게임 정의)
}

// 0x0701 - UserActionNotificationSend (게임서버 → 채팅서버)
message UserActionNotificationSend {
    string notification_id = 1;      // 고유 ID (중복 방지)
    UserActionType action_type = 2;  // 행동 타입

    // 행동 주체 정보
    string actor_user_id = 3;        // 행동한 유저 ID
    string actor_nickname = 4;       // 행동한 유저 닉네임
    string actor_profile_image = 5;  // 프로필 이미지
    string actor_frame_image = 6;    // 프레임 이미지

    // 알림 내용
    string title = 7;                // 제목 (팝업용)
    string content = 8;              // 내용 (채팅 메시지)
    string icon_id = 9;              // 아이콘 ID (아이템 아이콘 등)
    string extra_data = 10;          // 추가 데이터 (JSON)

    // 대상 설정
    string target_channel = 11;      // 대상 채널 (빈값 = 전체)
    bool exclude_actor = 12;         // 행동 주체 제외 여부

    // 인증
    string admin_token = 13;         // 관리자 토큰
}

// 0x0702 - UserActionNotificationReceive (채팅서버 → 클라이언트)
message UserActionNotificationReceive {
    string notification_id = 1;
    UserActionType action_type = 2;

    // 행동 주체 정보
    string actor_user_id = 3;
    string actor_nickname = 4;
    string actor_profile_image = 5;
    string actor_frame_image = 6;

    // 알림 내용
    string title = 7;
    string content = 8;
    string icon_id = 9;
    string extra_data = 10;

    int64 timestamp = 11;
}

// 0x0703 - UserActionNotificationAck (채팅서버 → 게임서버)
message UserActionNotificationAck {
    bool success = 1;
    string notification_id = 2;
    int32 delivered_count = 3;
    string error_message = 4;
    int32 error_code = 5;
}
```

---

## 3. 서버 컴포넌트 설계

### 3.1 아키텍처 다이어그램

```
┌─────────────────┐     0x0701         ┌─────────────────────────────────┐
│   게임 서버     │ ──────────────────▶│        채팅 서버                │
│   - 아이템 획득 │                    │  ┌─────────────────────────────┐│
│   - 랭킹 갱신   │                    │  │ MessageDispatcher           ││
│   - 미션 클리어 │                    │  │  └─ handleUserAction()      ││
└─────────────────┘                    │  └──────────────┬──────────────┘│
        ▲                              │                 │               │
        │ 0x0703                       │  ┌──────────────▼──────────────┐│
        └──────────────────────────────│  │ UserActionService           ││
                                       │  │  - validate()               ││
                                       │  │  - formatChatMessage()      ││
                                       │  │  - broadcastLocal()         ││
                                       │  │  - publishToRedis()         ││
                                       │  └──────────────┬──────────────┘│
                                       │                 │               │
                                       │  ┌──────────────▼──────────────┐│
                                       │  │ SessionManager              ││
                                       │  │  - broadcast(packet)        ││
                                       │  └─────────────────────────────┘│
                                       └────────────────┬────────────────┘
                                                        │ 0x0702
                                                        ▼
                                       ┌─────────────────────────────────┐
                                       │           클라이언트            │
                                       │  ┌─────────────────────────────┐│
                                       │  │ ChatClient                  ││
                                       │  │  - OnUserActionReceived     ││
                                       │  └──────────────┬──────────────┘│
                                       │                 │               │
                                       │  ┌──────────────▼──────────────┐│
                                       │  │ NotificationManager         ││
                                       │  │  1. 채팅 메시지 표시 (자동) ││
                                       │  │  2. 이벤트 발행 (선택적)    ││
                                       │  │     └─ OnUserActionNotification
                                       │  │        └─ 사용자 팝업 연결  ││
                                       │  └─────────────────────────────┘│
                                       └─────────────────────────────────┘
```

### 3.2 UserActionService 클래스

```cpp
// src/notification/UserActionService.hpp

#pragma once

#include <memory>
#include <string>
#include <unordered_set>
#include <mutex>

namespace chat {

class SessionManager;
class ChannelManager;
class RedisPubSub;
class Config;

struct UserActionRequest {
    std::string notification_id;
    int action_type = 0;

    // 행동 주체
    std::string actor_user_id;
    std::string actor_nickname;
    std::string actor_profile_image;
    std::string actor_frame_image;

    // 알림 내용
    std::string title;
    std::string content;
    std::string icon_id;
    std::string extra_data;

    // 대상 설정
    std::string target_channel;
    bool exclude_actor = false;

    std::string admin_token;
};

struct UserActionResult {
    bool success = false;
    int delivered_count = 0;
    std::string error_message;
    int error_code = 0;
};

class UserActionService {
public:
    UserActionService(
        const Config& config,
        std::shared_ptr<SessionManager> session_manager,
        std::shared_ptr<ChannelManager> channel_manager
    );

    void setRedisPubSub(std::shared_ptr<RedisPubSub> pubsub);

    // 유저 행동 알림 전송
    UserActionResult sendNotification(const UserActionRequest& request);

    // Redis로부터 수신한 알림 처리
    void handleRemoteNotification(const std::string& payload);

    bool isDuplicate(const std::string& notification_id) const;

private:
    bool validateRequest(const UserActionRequest& request, std::string& error);
    bool validateAdminToken(const std::string& token);

    // 채팅 메시지 포맷팅
    std::string formatChatMessage(const UserActionRequest& request);

    // 로컬 브로드캐스트
    int broadcastLocal(const UserActionRequest& request);

    // 특정 채널 대상
    int broadcastToChannel(const UserActionRequest& request);

    // Redis 발행
    void publishToRedis(const UserActionRequest& request);

    void recordNotificationId(const std::string& id);

private:
    const Config& config_;
    std::shared_ptr<SessionManager> session_manager_;
    std::shared_ptr<ChannelManager> channel_manager_;
    std::shared_ptr<RedisPubSub> redis_pubsub_;

    mutable std::mutex recent_ids_mutex_;
    std::unordered_set<std::string> recent_notification_ids_;
    static constexpr size_t MAX_RECENT_IDS = 1000;
};

} // namespace chat
```

### 3.3 채팅 메시지 포맷팅 규칙

```cpp
std::string UserActionService::formatChatMessage(const UserActionRequest& request) {
    // 타입별 프리픽스
    std::string prefix;
    switch (request.action_type) {
        case 0: prefix = "[아이템]"; break;
        case 1: prefix = "[랭킹]"; break;
        case 2: prefix = "[미션]"; break;
        case 3: prefix = "[업적]"; break;
        default: prefix = "[알림]"; break;
    }

    // 포맷: "[타입] 닉네임님이 ..."
    return prefix + " " + request.content;
}
```

---

## 4. 클라이언트 설계 (핵심: 이벤트 기반 구조)

### 4.1 NotificationManager 클래스

```csharp
// Unity: Assets/Plugins/UniversalChat/Runtime/Notification/NotificationManager.cs

using System;
using UnityEngine;
using UniversalChat.Core;

namespace UniversalChat.Notification
{
    /// <summary>
    /// 유저 행동 알림 관리자
    /// - 채팅 알림: 기본 동작 (항상 표시)
    /// - 팝업 알림: 이벤트 구독 시에만 동작
    /// </summary>
    public class NotificationManager : MonoBehaviour
    {
        #region Singleton

        private static NotificationManager _instance;
        public static NotificationManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    var go = new GameObject("[NotificationManager]");
                    _instance = go.AddComponent<NotificationManager>();
                    DontDestroyOnLoad(go);
                }
                return _instance;
            }
        }

        #endregion

        #region Settings

        [Header("Settings")]
        [SerializeField] private bool _enableChatNotification = true;
        [SerializeField] private bool _enableDebugLog = false;

        #endregion

        #region Events

        /// <summary>
        /// 유저 행동 알림 이벤트
        ///
        /// 사용 방법:
        /// 1. 채팅 알림만 원하면 → 이 이벤트에 아무것도 연결하지 않음
        /// 2. 스플래시 팝업도 원하면 → 이 이벤트에 팝업 표시 함수 연결
        ///
        /// 예시:
        /// NotificationManager.Instance.OnUserActionNotification += ShowSplashPopup;
        /// </summary>
        public event Action<UserActionNotificationData> OnUserActionNotification;

        /// <summary>
        /// 알림 타입별 필터링이 필요한 경우 사용
        /// </summary>
        public event Action<UserActionNotificationData> OnItemAcquireNotification;
        public event Action<UserActionNotificationData> OnRankingBreakthroughNotification;
        public event Action<UserActionNotificationData> OnMissionCompleteNotification;
        public event Action<UserActionNotificationData> OnAchievementNotification;

        #endregion

        #region Properties

        /// <summary>
        /// 채팅 알림 활성화 여부 (기본: true)
        /// </summary>
        public bool EnableChatNotification
        {
            get => _enableChatNotification;
            set => _enableChatNotification = value;
        }

        /// <summary>
        /// 팝업 이벤트 구독자가 있는지 확인
        /// </summary>
        public bool HasPopupSubscribers => OnUserActionNotification != null;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }

            _instance = this;
            DontDestroyOnLoad(gameObject);
        }

        private void Start()
        {
            // ChatClient 이벤트 연결
            if (ChatManager.Instance != null)
            {
                // ChatClient에서 UserActionNotification 수신 시 호출됨
                // (ChatClient에 OnUserActionReceived 이벤트 추가 필요)
            }
        }

        private void OnDestroy()
        {
            if (_instance == this)
            {
                _instance = null;
            }
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 서버로부터 수신한 알림 처리 (ChatClient에서 호출)
        /// </summary>
        public void HandleNotification(UserActionNotificationData data)
        {
            if (_enableDebugLog)
            {
                Debug.Log($"[NotificationManager] Received: {data.ActionType} - {data.Content}");
            }

            // 1. 채팅 메시지로 표시 (기본 동작)
            if (_enableChatNotification)
            {
                ShowChatNotification(data);
            }

            // 2. 이벤트 발행 (구독자가 있으면 호출됨)
            InvokeNotificationEvents(data);
        }

        #endregion

        #region Private Methods

        private void ShowChatNotification(UserActionNotificationData data)
        {
            // 시스템 메시지로 채팅창에 표시
            var chatMessage = new ChannelMessage
            {
                MessageId = data.NotificationId,
                ChannelId = "system",
                SenderId = data.ActorUserId,
                SenderNickname = data.ActorNickname,
                SenderProfileImage = data.ActorProfileImage,
                SenderFrameImage = data.ActorFrameImage,
                Content = data.Content,
                Timestamp = data.Timestamp,
                MessageType = MessageType.System
            };

            // ChatManager의 메시지 수신 이벤트 트리거
            ChatManager.Instance?.TriggerSystemMessage(chatMessage);
        }

        private void InvokeNotificationEvents(UserActionNotificationData data)
        {
            // 공통 이벤트 발행
            OnUserActionNotification?.Invoke(data);

            // 타입별 이벤트 발행
            switch (data.ActionType)
            {
                case UserActionType.ItemAcquire:
                    OnItemAcquireNotification?.Invoke(data);
                    break;
                case UserActionType.RankingBreakthrough:
                    OnRankingBreakthroughNotification?.Invoke(data);
                    break;
                case UserActionType.MissionComplete:
                    OnMissionCompleteNotification?.Invoke(data);
                    break;
                case UserActionType.Achievement:
                    OnAchievementNotification?.Invoke(data);
                    break;
            }
        }

        #endregion
    }
}
```

### 4.2 데이터 모델

```csharp
// Unity: Assets/Plugins/UniversalChat/Runtime/Notification/UserActionNotificationData.cs

using System;

namespace UniversalChat.Notification
{
    public enum UserActionType
    {
        ItemAcquire = 0,
        RankingBreakthrough = 1,
        MissionComplete = 2,
        Achievement = 3,
        Custom = 99
    }

    /// <summary>
    /// 유저 행동 알림 데이터
    /// </summary>
    public class UserActionNotificationData
    {
        public string NotificationId { get; set; }
        public UserActionType ActionType { get; set; }

        // 행동 주체 정보
        public string ActorUserId { get; set; }
        public string ActorNickname { get; set; }
        public string ActorProfileImage { get; set; }
        public string ActorFrameImage { get; set; }

        // 알림 내용
        public string Title { get; set; }      // 팝업 제목
        public string Content { get; set; }    // 채팅 메시지 내용
        public string IconId { get; set; }     // 아이템 아이콘 등
        public string ExtraData { get; set; }  // JSON 추가 데이터

        public DateTime Timestamp { get; set; }

        /// <summary>
        /// Protobuf 메시지에서 변환
        /// </summary>
        public UserActionNotificationData(Chat.Protocol.UserActionNotificationReceive proto)
        {
            NotificationId = proto.NotificationId;
            ActionType = (UserActionType)proto.ActionType;
            ActorUserId = proto.ActorUserId;
            ActorNickname = proto.ActorNickname;
            ActorProfileImage = proto.ActorProfileImage;
            ActorFrameImage = proto.ActorFrameImage;
            Title = proto.Title;
            Content = proto.Content;
            IconId = proto.IconId;
            ExtraData = proto.ExtraData;
            Timestamp = DateTimeOffset.FromUnixTimeMilliseconds(proto.Timestamp).DateTime;
        }
    }
}
```

### 4.3 사용자 팝업 연결 예시

```csharp
// 게임 프로젝트: SplashPopupHandler.cs

using UnityEngine;
using UniversalChat.Notification;

/// <summary>
/// 스플래시 팝업 연결 예시
///
/// 시나리오별 사용법:
/// 1. 채팅 알림만: 이 스크립트를 사용하지 않음
/// 2. 팝업만: EnableChatNotification = false 설정 후 이 스크립트 사용
/// 3. 둘 다: 이 스크립트 사용 (채팅 알림은 기본 동작)
/// </summary>
public class SplashPopupHandler : MonoBehaviour
{
    [Header("Popup Settings")]
    [SerializeField] private GameObject _splashPopupPrefab;
    [SerializeField] private Transform _popupParent;
    [SerializeField] private float _displayDuration = 3f;

    [Header("Filter Settings")]
    [SerializeField] private bool _showItemAcquire = true;
    [SerializeField] private bool _showRankingBreakthrough = true;
    [SerializeField] private bool _showMissionComplete = true;
    [SerializeField] private bool _showAchievement = true;

    private void Start()
    {
        // ============================================
        // 핵심: 이벤트 연결
        // 이 한 줄로 팝업 기능이 활성화됨
        // ============================================
        NotificationManager.Instance.OnUserActionNotification += ShowSplashPopup;

        // 또는 타입별로 선택적 연결 가능
        // NotificationManager.Instance.OnItemAcquireNotification += ShowItemPopup;
        // NotificationManager.Instance.OnRankingBreakthroughNotification += ShowRankingPopup;
    }

    private void OnDestroy()
    {
        // 이벤트 해제
        if (NotificationManager.Instance != null)
        {
            NotificationManager.Instance.OnUserActionNotification -= ShowSplashPopup;
        }
    }

    /// <summary>
    /// 스플래시 팝업 표시
    /// </summary>
    private void ShowSplashPopup(UserActionNotificationData data)
    {
        // 필터링
        if (!ShouldShow(data.ActionType)) return;

        // 팝업 생성 및 표시
        var popup = Instantiate(_splashPopupPrefab, _popupParent);
        var popupComponent = popup.GetComponent<SplashPopupUI>();

        if (popupComponent != null)
        {
            popupComponent.Setup(
                title: data.Title,
                content: data.Content,
                iconId: data.IconId,
                actorNickname: data.ActorNickname,
                actorProfileImage: data.ActorProfileImage,
                duration: _displayDuration
            );
        }
    }

    private bool ShouldShow(UserActionType type)
    {
        return type switch
        {
            UserActionType.ItemAcquire => _showItemAcquire,
            UserActionType.RankingBreakthrough => _showRankingBreakthrough,
            UserActionType.MissionComplete => _showMissionComplete,
            UserActionType.Achievement => _showAchievement,
            _ => true
        };
    }
}
```

### 4.4 팝업 UI 예시

```csharp
// 게임 프로젝트: SplashPopupUI.cs

using UnityEngine;
using UnityEngine.UI;
using TMPro;
using System.Collections;

public class SplashPopupUI : MonoBehaviour
{
    [SerializeField] private TMP_Text _titleText;
    [SerializeField] private TMP_Text _contentText;
    [SerializeField] private TMP_Text _nicknameText;
    [SerializeField] private Image _iconImage;
    [SerializeField] private Image _profileImage;
    [SerializeField] private CanvasGroup _canvasGroup;
    [SerializeField] private Animator _animator;

    public void Setup(
        string title,
        string content,
        string iconId,
        string actorNickname,
        string actorProfileImage,
        float duration)
    {
        _titleText.text = title;
        _contentText.text = content;
        _nicknameText.text = actorNickname;

        // 아이콘 로드 (게임별 구현)
        // _iconImage.sprite = LoadIcon(iconId);

        // 프로필 이미지 로드 (게임별 구현)
        // _profileImage.sprite = LoadProfileImage(actorProfileImage);

        // 애니메이션 시작
        if (_animator != null)
        {
            _animator.SetTrigger("Show");
        }

        // 자동 닫기
        StartCoroutine(AutoClose(duration));
    }

    private IEnumerator AutoClose(float duration)
    {
        yield return new WaitForSeconds(duration);

        if (_animator != null)
        {
            _animator.SetTrigger("Hide");
            yield return new WaitForSeconds(0.5f); // 페이드아웃 대기
        }

        Destroy(gameObject);
    }
}
```

---

## 5. ChatClient/ChatManager 확장

### 5.1 ChatClient에 추가

```csharp
// ChatClient.cs에 추가

// Events 영역
public event Action<UserActionNotificationReceive> OnUserActionReceived;

// Packet Handling 영역
private void HandlePacketReceived(PacketType type, byte[] data)
{
    // ... 기존 코드 ...

    case PacketType.UserActionNotificationReceive:
        HandleUserActionNotification(data);
        break;
}

private void HandleUserActionNotification(byte[] data)
{
    var notification = _serializer.Deserialize<UserActionNotificationReceive>(data);
    OnUserActionReceived?.Invoke(notification);
}
```

### 5.2 ChatManager에 추가

```csharp
// ChatManager.cs에 추가

// Fields
private NotificationManager _notificationManager;

// Initialize
private void InitializeClient()
{
    // ... 기존 코드 ...
    _client.OnUserActionReceived += HandleUserActionReceived;

    // NotificationManager 초기화
    _notificationManager = NotificationManager.Instance;
}

// Event Handler
private void HandleUserActionReceived(UserActionNotificationReceive protoNotification)
{
    var data = new UserActionNotificationData(protoNotification);
    _notificationManager.HandleNotification(data);
}

// 시스템 메시지 트리거 (NotificationManager에서 호출)
public void TriggerSystemMessage(ChannelMessage message)
{
    OnMessageReceived?.Invoke(message);
}
```

---

## 6. 구현 Task 분할

### Phase 1: 프로토콜 정의 (0.5일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T1.1 | chat.proto에 UserAction 메시지 추가 | 30분 | - |
| T1.2 | PacketTypes.hpp에 패킷 타입 추가 | 15분 | - |
| T1.3 | PubSubMessage.hpp에 UserActionPayload 추가 | 30분 | - |
| T1.4 | cmake 빌드 및 protobuf 재생성 | 15분 | T1.1 |

### Phase 2: 서버 구현 (1.5일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T2.1 | UserActionService 클래스 생성 | 2시간 | T1.* |
| T2.2 | sendNotification() 구현 | 1시간 | T2.1 |
| T2.3 | formatChatMessage() 구현 | 30분 | T2.2 |
| T2.4 | broadcastLocal() 구현 | 1시간 | T2.3 |
| T2.5 | publishToRedis() 구현 | 1시간 | T2.4 |
| T2.6 | MessageDispatcher에 핸들러 등록 | 1시간 | T2.5 |
| T2.7 | Config에 user_action 설정 추가 | 30분 | - |
| T2.8 | Server 클래스에 통합 | 30분 | T2.* |

### Phase 3: 클라이언트 핵심 구현 (1일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T3.1 | Unity protobuf 파일 갱신 | 30분 | T1.1 |
| T3.2 | UserActionNotificationData 클래스 생성 | 30분 | T3.1 |
| T3.3 | NotificationManager 클래스 생성 | 2시간 | T3.2 |
| T3.4 | ChatClient에 OnUserActionReceived 추가 | 1시간 | T3.3 |
| T3.5 | ChatManager에 NotificationManager 연동 | 1시간 | T3.4 |
| T3.6 | PacketType enum에 UserAction 추가 | 15분 | - |

### Phase 4: 테스트 (1일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T4.1 | UserActionService 단위 테스트 | 2시간 | T2.* |
| T4.2 | 싱글 서버 통합 테스트 | 1시간 | T4.1 |
| T4.3 | 멀티 서버 (Redis) 통합 테스트 | 2시간 | T4.2 |
| T4.4 | 클라이언트 통합 테스트 | 2시간 | T3.* |

### Phase 5: 샘플 및 문서화 (0.5일)

| Task ID | 작업 내용 | 예상 시간 | 의존성 |
|---------|---------|----------|--------|
| T5.1 | SplashPopupHandler 샘플 코드 작성 | 1시간 | T3.* |
| T5.2 | SplashPopupUI 샘플 프리팹 생성 | 1시간 | T5.1 |
| T5.3 | API 문서 작성 | 1시간 | T4.* |
| T5.4 | 통합 가이드 작성 | 1시간 | T5.3 |

---

## 7. 에러 코드 정의

```cpp
// include/universalchat/Errors.hpp 에 추가

// UserAction errors (7100-7199)
constexpr int ERR_USERACTION_INVALID_TOKEN = 7101;
constexpr int ERR_USERACTION_EMPTY_CONTENT = 7102;
constexpr int ERR_USERACTION_INVALID_TYPE = 7103;
constexpr int ERR_USERACTION_RATE_LIMITED = 7104;
constexpr int ERR_USERACTION_DUPLICATE_ID = 7105;
constexpr int ERR_USERACTION_CHANNEL_NOT_FOUND = 7106;
```

---

## 8. Config 설정

```json
// config/server.json 에 추가

{
  "user_action": {
    "enabled": true,
    "admin_tokens": ["secure-token-1", "secure-token-2"],
    "rate_limit_per_minute": 60,
    "duplicate_check_window_seconds": 30,
    "max_content_length": 200,
    "default_exclude_actor": false
  }
}
```

---

## 9. 시퀀스 다이어그램

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────────────┐
│게임서버  │    │채팅서버  │    │클라이언트│    │SplashPopupHandler│
└────┬─────┘    └────┬─────┘    └────┬─────┘    └────────┬─────────┘
     │               │               │                   │
     │ UserActionNotificationSend    │                   │
     │──────────────▶│               │                   │
     │               │               │                   │
     │               │ validate()    │                   │
     │               │──────┐        │                   │
     │               │      │        │                   │
     │               │◀─────┘        │                   │
     │               │               │                   │
     │               │ broadcast     │                   │
     │               │ (to all)      │                   │
     │               │───────────────│───────────────────│
     │               │               │                   │
     │               │ UserActionNotificationReceive     │
     │               │──────────────▶│                   │
     │               │               │                   │
     │ UserActionAck │               │                   │
     │◀──────────────│               │                   │
     │               │               │ HandleNotification│
     │               │               │──────┐            │
     │               │               │      │            │
     │               │               │◀─────┘            │
     │               │               │                   │
     │               │               │ 1. Show Chat Msg  │
     │               │               │──────┐            │
     │               │               │      │            │
     │               │               │◀─────┘            │
     │               │               │                   │
     │               │               │ 2. Invoke Event   │
     │               │               │──────────────────▶│
     │               │               │                   │
     │               │               │                   │ ShowSplashPopup
     │               │               │                   │──────┐
     │               │               │                   │      │
     │               │               │                   │◀─────┘
     │               │               │                   │
```

---

## 10. 참고 파일

| 파일 | 설명 |
|------|-----|
| `unity-client/.../ChatManager.cs` | 기존 이벤트 패턴 참고 |
| `unity-client/.../ChatClient.cs` | 패킷 핸들링 패턴 참고 |
| `docs/design/ANNOUNCEMENT_FEATURE_DESIGN.md` | 공지사항 설계 참고 |
| `src/core/SessionManager.hpp:97` | broadcast() 메서드 |
| `proto/chat.proto` | 기존 프로토콜 구조 |

---

**작성일**: 2026-01-28
**작성자**: Claude Code
**버전**: 1.0
