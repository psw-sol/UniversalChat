# UniversalChat Unity Client - 사용 가이드

UniversalChatServer(C++ 채팅 서버)와 통신하는 Unity 채팅 클라이언트 패키지의 상세 가이드입니다.

---

## 목차

1. [패키지 개요](#패키지-개요)
2. [설치 및 요구사항](#설치-및-요구사항)
3. [빠른 시작](#빠른-시작)
4. [아키텍처](#아키텍처)
   - [3-Level 사용 패턴](#3-level-사용-패턴)
   - [계층 구조](#계층-구조)
   - [이벤트 파이프라인](#이벤트-파이프라인)
5. [핵심 컴포넌트 API](#핵심-컴포넌트-api)
   - [IChatService (인터페이스)](#ichatservice-인터페이스)
   - [ChatManager (Level 1)](#chatmanager-level-1)
   - [ChatServiceBase (Level 2)](#chatservicebase-level-2)
   - [ChatClient](#chatclient)
   - [ChatUIManager](#chatuimanager)
   - [ChatUIBuilder](#chatuibuilder)
6. [UI 생성 및 설정](#ui-생성-및-설정)
   - [에디터 메뉴로 생성](#방법-1-에디터-메뉴-사용)
   - [코드에서 생성](#방법-2-코드에서-생성)
   - [ChatUIConfig 테마 설정](#chatuiconfig-테마-설정)
7. [게임 통합 패턴](#게임-통합-패턴)
   - [GameChatManager 샘플](#gamechatmanager-샘플)
   - [채널 타입별 관리](#채널-타입별-관리)
   - [메시지 히스토리](#메시지-히스토리)
8. [이벤트 시스템](#이벤트-시스템)
   - [IChatService 이벤트 (C# Action)](#ichatservice-이벤트-c-action)
   - [Typed Channel 이벤트 (ChatServiceBase)](#typed-channel-이벤트-chatservicebase)
   - [UnityEvent 이벤트 (ChatUIManager)](#unityevent-이벤트-chatuimanager)
9. [데이터 모델](#데이터-모델)
10. [Rich Content 시스템](#rich-content-시스템)
11. [번역 시스템](#번역-시스템)
12. [고급 사용법](#고급-사용법)
    - [커스텀 UI 통합](#커스텀-ui-통합)
    - [프로필 관리](#프로필-관리)
    - [귓속말](#귓속말)
    - [공지사항 수신](#공지사항-수신)
13. [프로토콜 참조](#프로토콜-참조)
14. [문제 해결](#문제-해결)

---

## 패키지 개요

| 항목 | 내용 |
|------|------|
| **패키지명** | `com.universalchat.client` |
| **버전** | 1.0.0 |
| **Unity 최소 버전** | 2020.3 |
| **프로토콜** | TCP + Protobuf |
| **의존성** | Google.Protobuf, TextMeshPro |
| **서버** | UniversalChatServer (C++17, Boost.Asio) |

### 주요 기능

- **실시간 채팅**: TCP 소켓 기반 저지연 메시지 송수신
- **채널 시스템**: 월드/길드/파티 등 다중 채널 동시 참여
- **Plug & Play UI**: 프리팹 없이 런타임 UI 동적 생성
- **3-Level 사용 패턴**: Zero Code → Minimal Code → Full Control
- **IChatService 추상화**: 모든 UI가 의존하는 공통 인터페이스
- **Rich Content**: 클릭 가능한 링크 (아이템, 유저 등) 지원
- **번역**: REST API 기반 다국어 자동 번역
- **자동 재연결**: 연결 끊김 시 자동 복구
- **가상화 스크롤**: 대량 메시지 처리 성능 최적화
- **테마 커스터마이징**: ScriptableObject 기반 UI 설정
- **공지사항 및 유저 행동 알림**: 서버 푸시 알림 수신

### 폴더 구조

```
Assets/Plugins/UniversalChat/
├── Runtime/                        # 런타임 코드
│   ├── Core/                      # 핵심 비즈니스 로직
│   │   ├── IChatService.cs        # 채팅 서비스 추상화 인터페이스
│   │   ├── ChatServiceBase.cs     # 제네릭 기본 구현 (Level 2)
│   │   ├── ChatManager.cs         # MonoBehaviour 싱글톤 (Level 1)
│   │   ├── ChatClient.cs          # 서버 통신 클라이언트
│   │   └── DataModels.cs          # Proto 래퍼 클래스
│   ├── Network/                   # 네트워크 통신
│   │   ├── ChatConnection.cs      # TCP 소켓 연결
│   │   └── MainThreadDispatcher.cs # 메인 스레드 디스패치
│   ├── Protocol/                  # 통신 프로토콜
│   │   ├── Chat.cs                # Protobuf 자동 생성 파일
│   │   ├── PacketType.cs          # 패킷 타입 enum
│   │   ├── PacketHeader.cs        # 8바이트 패킷 헤더
│   │   └── PacketSerializer.cs    # 직렬화/역직렬화
│   ├── UI/                        # 사용자 인터페이스
│   │   ├── ChatUIBuilder.cs       # 런타임 UI 동적 생성
│   │   ├── Components/
│   │   │   ├── ChatUIManager.cs          # UI 메인 매니저
│   │   │   ├── VirtualizedChatPanel.cs   # 가상화 스크롤
│   │   │   ├── ChatMessageView.cs        # 메시지 항목 뷰
│   │   │   ├── ChatInputField.cs         # 입력 필드
│   │   │   ├── ChannelListPanel.cs       # 채널 목록
│   │   │   └── ChatMessageData.cs        # 메시지 데이터 모델
│   │   └── Themes/
│   │       └── ChatUIConfig.cs    # ScriptableObject 테마 설정
│   ├── RichContent/               # 링크 포함 메시지 시스템
│   │   ├── RichContentManager.cs
│   │   ├── RichContentConfig.cs
│   │   ├── Data/RichLinkData.cs
│   │   ├── Interfaces/            # 확장 인터페이스
│   │   ├── Parser/RichTextParser.cs
│   │   └── UI/                    # Rich Content UI 컴포넌트
│   └── Translation/               # 다국어 번역 시스템
│       ├── TranslationManager.cs
│       ├── TranslationConfig.cs
│       └── TranslationService.cs
├── Editor/                        # 에디터 도구
│   ├── ChatUIManagerEditor.cs     # Inspector 커스텀 에디터
│   ├── ChatUISampleSceneGenerator.cs
│   └── RichContentEditorMenu.cs
├── Samples/                       # 샘플 및 통합 예시
│   ├── Scripts/GameChatManager.cs # 게임 통합 예시 (ChatServiceBase 기반)
│   ├── GameIntegration/           # Rich Content 예시
│   └── README.md                  # ← 이 파일
└── package.json                   # Unity 패키지 메타데이터
```

---

## 설치 및 요구사항

### 요구사항

- Unity 2020.3 이상
- TextMeshPro (Unity 기본 포함)
- Google.Protobuf 런타임 DLL

### 설치 방법

#### 방법 A: Unity Package Manager (Git URL)

`Packages/manifest.json`에 추가:

```json
"com.universalchat.client": "https://github.com/psw-sol/UniversalChat.git?path=unity-client/Assets/Plugins/UniversalChat#v1.0.0"
```

#### 방법 B: 폴더 복사

```
Assets/Plugins/UniversalChat/ 폴더 전체를 프로젝트에 복사
```

### Google.Protobuf 설정

NuGet 또는 직접 DLL을 `Assets/Plugins/` 에 배치:

```
Assets/Plugins/
├── Google.Protobuf.dll
└── UniversalChat/  ← 패키지
```

---

## 빠른 시작

### Level 1: 30초 빠른 시작 (Zero Code)

```csharp
using UniversalChat.Core;

// 1. 서버 연결 + 로그인 + 채널 입장 (한 줄)
await ChatManager.Instance.ConnectAndLoginAsync(
    "localhost", 7777, "user123", nickname: "홍길동");
await ChatManager.Instance.JoinAutoAssignedChannelAsync("world");

// 2. 메시지 전송
await ChatManager.Instance.SendMessageAsync("안녕하세요!");

// 3. 메시지 수신
ChatManager.Instance.OnMessageReceived += msg =>
    Debug.Log($"[{msg.SenderNickname}] {msg.Content}");
```

### Level 2: 게임 통합 (Minimal Code)

```csharp
using UniversalChat.Core;
using UniversalChat.UI;

// 1. 채팅 서비스 정의 (ClassifyChannel 하나만 구현)
public class MyChatService : ChatServiceBase<MyChatService.ChannelType>
{
    public enum ChannelType { World, Guild, Party, Custom }

    protected override ChannelType ClassifyChannel(string channelId)
    {
        if (channelId.StartsWith("world")) return ChannelType.World;
        if (channelId.StartsWith("guild_")) return ChannelType.Guild;
        if (channelId.StartsWith("party_")) return ChannelType.Party;
        return ChannelType.Custom;
    }
}

// 2. MonoBehaviour에서 사용
public class GameChat : MonoBehaviour
{
    [SerializeField] private ChatUIManager chatUI;
    private MyChatService _chatService;

    void Awake()
    {
        _chatService = new MyChatService();
        chatUI.SetChatService(_chatService);  // UI에 서비스 주입
    }

    async void Start()
    {
        await _chatService.ConnectAndLoginAsync("localhost", 7777, "user123", nickname: "홍길동");
        await _chatService.RequestAutoAssignChannelAsync("world");
    }

    void OnDestroy() => _chatService?.Dispose();
}
```

### 기본 UI 통합 예시

```csharp
using UnityEngine;
using UniversalChat.Core;
using UniversalChat.UI;

public class SimpleChatSetup : MonoBehaviour
{
    [SerializeField] private Canvas canvas;
    [SerializeField] private ChatUIConfig uiConfig; // 선택사항

    async void Start()
    {
        // UI 생성 (프리팹 불필요)
        var chatUI = ChatUIBuilder.Build(canvas.transform, uiConfig);

        // 위치/크기 설정
        var rect = chatUI.GetComponent<RectTransform>();
        rect.anchorMin = new Vector2(1, 0);
        rect.anchorMax = new Vector2(1, 0);
        rect.pivot = new Vector2(1, 0);
        rect.anchoredPosition = new Vector2(-10, 10);
        rect.sizeDelta = new Vector2(400, 500);

        // 서버 연결 → 로그인 → 채널 입장
        await chatUI.ConnectAsync("localhost", 7777);
        await chatUI.LoginAsync("user123", nickname: "TestUser");
        await chatUI.JoinChannelAsync("world");
    }
}
```

---

## 아키텍처

### 3-Level 사용 패턴

UniversalChat은 프로젝트 요구에 맞는 3단계 사용 레벨을 제공합니다:

| Level | 이름 | 구현 방식 | 적합한 경우 |
|-------|------|----------|-------------|
| **Level 1** | Zero Code | `ChatManager` + `ChatUIManager` | 빠른 프로토타이핑, 단순 채팅 |
| **Level 2** | Minimal Code | `ChatServiceBase<T>` 상속 | 게임별 채널 타입 관리 필요 |
| **Level 3** | Full Control | `IChatService` 직접 구현 | 기존 시스템 통합, 완전 커스텀 |

```
Level 1 (Zero Code)         Level 2 (Minimal Code)        Level 3 (Full Control)
┌──────────────────┐     ┌──────────────────────┐     ┌──────────────────────┐
│  ChatManager     │     │ ChatServiceBase<T>   │     │ IChatService 직접    │
│  (MonoBehaviour  │     │ - ClassifyChannel()  │     │ 구현                  │
│   싱글톤)        │     │   하나만 구현         │     │                      │
│                  │     │ - 자동: 재연결,       │     │ 완전한 자유도        │
│  Inspector만으로 │     │   히스토리, 타입별    │     │ 기존 시스템 통합     │
│  사용 가능       │     │   이벤트 등 내장      │     │                      │
└──────┬───────────┘     └──────────┬───────────┘     └──────────┬───────────┘
       │                            │                            │
       └────────────────────────────┼────────────────────────────┘
                                    │
                          ┌─────────▼─────────┐
                          │   IChatService     │ ← 공통 인터페이스
                          │  (14개 이벤트)     │
                          └─────────┬─────────┘
                                    │
                          ┌─────────▼─────────┐
                          │  ChatUIManager     │ ← UI 자동 연결
                          │  (13개 UnityEvent) │
                          └───────────────────┘
```

### 계층 구조

```
┌─────────────────────────────────────────────────────────────────┐
│                     게임 코드 (사용자 구현)                       │
│  Level 1: ChatManager 직접 사용                                  │
│  Level 2: ChatServiceBase<T> 상속 (ClassifyChannel만 구현)       │
│  Level 3: IChatService 직접 구현                                 │
└──────────────────────────┬──────────────────────────────────────┘
                           │ SetChatService(IChatService)
┌──────────────────────────┼──────────────────────────────────────┐
│  ChatUIManager (UI 계층) │ IChatService 의존, UnityEvent 기반    │
│  ├─ VirtualizedChatPanel │ 가상화 스크롤 메시지 표시              │
│  ├─ ChatInputField       │ 메시지 입력                           │
│  ├─ ChannelListPanel     │ 채널 목록                             │
│  └─ RichContentManager   │ 클릭 가능 링크                        │
└──────────────────────────┼──────────────────────────────────────┘
                           │ IChatService 인터페이스
┌──────────────────────────┼──────────────────────────────────────┐
│  IChatService 구현체                                             │
│  ├─ ChatManager              │ Level 1 (MonoBehaviour 싱글톤)    │
│  ├─ ChatServiceBase<T>       │ Level 2 (제네릭 기본 클래스)      │
│  │   └─ GameChatManager      │ Level 2 샘플                      │
│  └─ (커스텀 구현)            │ Level 3                           │
└──────────────────────────┼──────────────────────────────────────┘
                           │
┌──────────────────────────┼──────────────────────────────────────┐
│  ChatClient (통신 계층)  │ Protobuf 패킷 송수신, 이벤트 발행     │
│  └─ ChatConnection       │ TCP 소켓, MainThread 디스패치         │
│      └─ PacketSerializer │ Protobuf 직렬화/역직렬화              │
└──────────────────────────┴──────────────────────────────────────┘
```

### 이벤트 파이프라인

```
ChatClient (proto 이벤트)
    │
    ├─ ChatManager (Level 1)
    │   Proto → Domain 변환 → IChatService 이벤트 발행
    │
    ├─ ChatServiceBase<T> (Level 2)
    │   Proto → Domain 변환 → IChatService 이벤트 + Typed Channel 이벤트 발행
    │
    └─ 커스텀 (Level 3)
        사용자 정의 변환 및 이벤트 발행
            │
            ▼
    ChatUIManager (IChatService 이벤트 구독)
        → UnityEvent 발행 → Inspector 연결 가능
```

### 스레드 안전성

네트워크 콜백은 별도 스레드에서 발생하므로, `MainThreadDispatcher`가 Unity 메인 스레드로 안전하게 디스패치합니다. 사용자 코드에서 별도의 스레드 처리가 필요 없습니다.

---

## 핵심 컴포넌트 API

### IChatService (인터페이스)

**역할**: 모든 UI 컴포넌트가 의존하는 채팅 서비스 추상화. Level 1~3 모두 이 인터페이스를 구현합니다.

```csharp
using UniversalChat.Core;
```

#### 프로퍼티

| 프로퍼티 | 타입 | 설명 |
|----------|------|------|
| `IsConnected` | `bool` | 서버 연결 상태 |
| `IsAuthenticated` | `bool` | 인증(로그인) 상태 |
| `UserId` | `string` | 로그인한 사용자 ID |
| `CurrentChannelId` | `string` | 현재 활성 채널 ID |
| `JoinedChannels` | `IReadOnlyList<string>` | 입장한 채널 ID 목록 |

#### 메서드

```csharp
IChatService chat = ...; // ChatManager.Instance 또는 커스텀 서비스

// 연결
await chat.ConnectAsync("192.168.1.100", 7777, timeoutMs: 5000);
chat.Disconnect();

// 로그인 (모든 파라미터는 nickname 이후 선택사항)
await chat.LoginAsync(
    userId: "user123",
    authToken: null,
    nickname: "홍길동",
    profileImage: "avatar_01",
    frameImage: "frame_gold",
    extraData: "{\"level\":50}");

// 채널
await chat.JoinChannelAsync("lobby", password: null);
await chat.LeaveChannelAsync("lobby");
await chat.RequestAutoAssignChannelAsync("world");

// 메시지
await chat.SendMessageAsync("channelId", "안녕하세요!", messageType: 0);
await chat.SendWhisperAsync("targetUserId", "귓속말입니다");

// 프로필
await chat.UpdateProfileAsync(nickname: "새닉네임", profileImage: "avatar_02");
```

#### 이벤트

```csharp
IChatService chat = ...;

// Connection
chat.OnConnected += () => Debug.Log("연결됨");
chat.OnDisconnected += reason => Debug.Log($"연결 끊김: {reason}");
chat.OnError += error => Debug.LogError(error);

// Auth
chat.OnAuthenticated += (success, message) => Debug.Log($"인증: {success}");

// Channel
chat.OnChannelJoined += (channelId, channelName) => Debug.Log($"채널 입장: {channelId}");
chat.OnChannelJoinedWithHistory += result => Debug.Log($"멤버 {result.Members.Count}명");
chat.OnChannelLeft += channelId => Debug.Log($"채널 퇴장: {channelId}");
chat.OnChannelListUpdated += channels => Debug.Log($"채널 {channels.Count}개");
chat.OnUserListUpdated += (channelId, users) => Debug.Log($"유저 {users.Count}명");

// Message
chat.OnMessageReceived += msg => Debug.Log($"[{msg.SenderNickname}] {msg.Content}");
chat.OnWhisperReceived += whisper => Debug.Log($"[귓속말] {whisper.Content}");

// Notification
chat.OnAnnouncementReceived += ann => Debug.Log($"[공지] {ann.Content}");
chat.OnUserActionNotificationReceived += notif => Debug.Log($"[알림] {notif.Title}");

// State
chat.OnChatReady += () => Debug.Log("채팅 준비 완료!");
```

---

### ChatManager (Level 1)

**역할**: MonoBehaviour 싱글톤. `IChatService`를 구현하며, Inspector 설정만으로 사용 가능합니다.

```csharp
using UniversalChat.Core;

var chat = ChatManager.Instance;
```

#### ChatManager 전용 프로퍼티

| 프로퍼티 | 타입 | 설명 |
|----------|------|------|
| `Instance` | `ChatManager` | 싱글톤 인스턴스 (없으면 자동 생성) |
| `ChannelList` | `IReadOnlyList<ChannelInfo>` | 채널 목록 |
| `UserLists` | `IReadOnlyDictionary<string, List<UserInfo>>` | 채널별 유저 목록 |

#### ChatManager 전용 메서드

```csharp
// 서버 주소 설정 (Inspector 또는 코드)
chat.Configure("192.168.1.100", 7777);

// 파라미터 없는 연결 (Configure된 주소 사용)
bool connected = await chat.ConnectAsync();

// 연결 + 로그인 한 번에
bool success = await chat.ConnectAndLoginAsync(
    "localhost", 7777, "user123", nickname: "홍길동");

// 현재 활성 채널에 메시지 전송 (channelId 불필요)
await chat.SendMessageAsync("안녕하세요!");

// 특정 채널에 메시지 전송
await chat.SendMessageToChannelAsync("world_1", "월드 메시지입니다");

// 자동 배정 채널 입장
await chat.JoinAutoAssignedChannelAsync("world");

// 채널 목록/멤버 새로고침
await chat.RefreshChannelListAsync();
await chat.RefreshUserListAsync("lobby");

// 채널 생성
await chat.CreateChannelAsync("my_room", password: null, maxUsers: 100);
```

#### ChatManager 전용 이벤트

```csharp
chat.OnChannelAutoAssigned += (success, channelId, message) => {
    if (success) Debug.Log($"자동 배정: {channelId}");
};
```

> **참고**: ChatManager는 IChatService의 모든 이벤트도 지원합니다. 위의 [IChatService 이벤트](#이벤트) 섹션을 참조하세요.

---

### ChatServiceBase (Level 2)

**역할**: 게임 프로젝트를 위한 제네릭 기본 구현. `IChatService`를 구현하며, `ClassifyChannel()` 하나만 구현하면 됩니다.

#### 내장 기능 (재구현 불필요)

- ChatClient 이벤트 브릿징 (Proto → Domain 자동 변환)
- 자동 재연결 (설정 가능)
- 인증 정보 캐싱 (재연결 시 자동 재로그인)
- 채널 상태 추적 (입장/퇴장, 현재 채널)
- 메시지 히스토리 캐싱 (채널별)
- 멤버 목록 관리 (채널별)
- 채널 타입별 자동 분류 및 이벤트 분배

#### 기본 사용법

```csharp
using UniversalChat.Core;

public class MyChatService : ChatServiceBase<MyChatService.ChannelType>
{
    public enum ChannelType { World, Guild, Party, Custom }

    // 유일한 필수 구현: 채널 ID → 채널 타입 분류
    protected override ChannelType ClassifyChannel(string channelId)
    {
        if (channelId.StartsWith("world")) return ChannelType.World;
        if (channelId.StartsWith("guild_")) return ChannelType.Guild;
        if (channelId.StartsWith("party_")) return ChannelType.Party;
        return ChannelType.Custom;
    }
}
```

#### ChatServiceBase 전용 프로퍼티

| 프로퍼티 | 타입 | 설명 |
|----------|------|------|
| `CurrentChannelsByType` | `IReadOnlyDictionary<T, string>` | 채널 타입별 현재 활성 채널 ID |
| `AutoReconnect` | `bool` | 자동 재연결 활성화 (기본: true) |
| `MaxReconnectAttempts` | `int` | 최대 재연결 시도 횟수 (기본: 3) |
| `ReconnectDelay` | `float` | 재연결 대기 시간(초) (기본: 5f) |
| `MaxHistoryPerChannel` | `int` | 채널별 최대 히스토리 수 (기본: 100) |

#### ChatServiceBase 전용 메서드

```csharp
var chat = new MyChatService();

// 연결 + 로그인 한 번에
await chat.ConnectAndLoginAsync("localhost", 7777, "user123", nickname: "홍길동");

// 채널 타입별 메시지 전송
await chat.SendMessageToChannelTypeAsync(MyChatService.ChannelType.World, "월드 메시지");

// 현재 활성 채널에 메시지 전송
await chat.SendMessageToCurrentChannelAsync("안녕하세요");

// 히스토리 조회 (채널별 또는 채널 타입별)
List<ChannelMessage> history = chat.GetMessageHistory("world_1", 50);
List<ChannelMessage> worldHistory = chat.GetMessageHistory(MyChatService.ChannelType.World, 50);

// 멤버 목록 조회
List<UserInfo> members = chat.GetChannelMembers("world_1");

// 채널 타입 확인
bool inWorld = chat.IsInChannelType(MyChatService.ChannelType.World);
string worldChannelId = chat.GetCurrentChannelId(MyChatService.ChannelType.World);

// 상태 초기화
chat.ClearAllState();

// 리소스 해제
chat.Dispose();
```

#### Virtual Extension Points

`ChatServiceBase`는 게임 특화 처리를 위한 가상 메서드를 제공합니다:

```csharp
public class MyChatService : ChatServiceBase<MyChatService.ChannelType>
{
    // ... ClassifyChannel 구현 ...

    protected override void OnChannelJoinedInternal(ChannelType type, string channelId, ChannelJoinResult result)
    {
        // 채널 타입별 입장 후 처리
        Debug.Log($"[{type}] 입장: {channelId}, 멤버 {result.Members.Count}명");
    }

    protected override void OnMessageReceivedInternal(ChannelType type, ChannelMessage message)
    {
        // 게임 특화 메시지 처리 (예: 금칙어 필터링, 아이템 링크 파싱)
    }

    protected override void OnDisconnectedInternal(string reason)
    {
        // 연결 끊김 처리 (예: UI 상태 업데이트)
    }

    protected override void OnReconnectedInternal()
    {
        // 재연결 성공 처리 (예: 채널 상태 복원 알림)
    }

    protected override void OnWhisperReceivedInternal(WhisperMessage whisper)
    {
        // 귓속말 처리 (예: 알림 팝업)
    }

    protected override void OnAuthenticatedInternal(bool success, string errorMessage)
    {
        // 인증 처리 (예: 로비 씬 전환)
    }
}
```

#### Typed Channel 이벤트

```csharp
var chat = new MyChatService();

// 채널 타입별 메시지 수신
chat.OnTypedMessageReceived += (type, msg) =>
    Debug.Log($"[{type}] {msg.SenderNickname}: {msg.Content}");

// 채널 타입별 입장
chat.OnTypedChannelJoined += (type, channelId, result) =>
    Debug.Log($"[{type}] 입장: {channelId}");

// 채널 타입별 퇴장
chat.OnTypedChannelLeft += (type, channelId) =>
    Debug.Log($"[{type}] 퇴장: {channelId}");

// 채널 타입별 멤버 업데이트
chat.OnTypedMembersUpdated += (type, channelId, members) =>
    Debug.Log($"[{type}] 멤버 변경: {members.Count}명");
```

---

### ChatClient

**역할**: 저수준 서버 통신. 직접 사용할 일은 드물며, 커스텀 통합 시 활용.

```csharp
using UniversalChat.Core;

var client = new ChatClient();

// 연결 (타임아웃 지정 가능)
bool connected = await client.ConnectAsync("localhost", 7777, timeoutMs: 5000);

// 인증
await client.AuthenticateAsync("user123", nickname: "홍길동");

// 프로필 업데이트
await client.UpdateProfileAsync(nickname: "새닉네임", profileImage: "avatar_02");

// 귓속말
await client.SendWhisperAsync("targetUser", "안녕하세요");

// 리소스 해제
client.Dispose();
```

#### ChatClient 전용 이벤트

| 이벤트 | 파라미터 | 설명 |
|--------|----------|------|
| `OnWhisperReceived` | `WhisperReceive` | 귓속말 수신 (Protobuf 원본) |
| `OnMemberUpdated` | `ChannelMemberUpdate` | 채널 멤버 변경 (입장/퇴장) |
| `OnProfileUpdated` | `ProfileUpdateResponse` | 내 프로필 업데이트 결과 |
| `OnProfileChanged` | `ProfileChanged` | 다른 유저 프로필 변경 알림 |

---

### ChatUIManager

**역할**: Plug & Play UI 매니저. `IChatService`에 의존하며, Inspector에서 모든 설정이 가능합니다.

#### SetChatService() - 서비스 주입

```csharp
// Level 2/3에서 커스텀 서비스를 ChatUIManager에 주입
var chatService = new MyChatService();
chatUIManager.SetChatService(chatService);
```

> **하위 호환**: `SetChatService()`를 호출하지 않으면 `Start()`에서 자동으로 `ChatManager.Instance`를 감지합니다 (Level 1 동작).

#### Inspector 설정 항목

```
[Connection Settings]
├── Connect On Start: bool      # Start()에서 자동 연결 여부

[UI References]  (ChatUIBuilder 사용 시 자동 연결)
├── Virtualized Chat Panel
├── Input Field
├── Channel List Panel
├── Send Button
├── Connect Button
└── Connection Status Text

[Theme]
└── UI Config: ChatUIConfig     # ScriptableObject 테마 설정

[Translation]
├── Enable Translation: bool
├── Translation Config: TranslationConfig
└── Auto Translate On Receive: bool  # 수신 메시지 자동 번역

[Auto Features]
├── Auto Load History On Join: bool   # 채널 입장 시 최근 메시지 자동 표시 (기본: true)
└── Auto Scroll To Bottom: bool       # 새 메시지 수신 시 자동 하단 스크롤 (기본: true)

[Lifecycle Events]
├── OnChatReadyEvent                              # Connect→Login→JoinChannel 파이프라인 완료
├── OnConnectedEvent
├── OnDisconnectedEvent(string)
└── OnErrorEvent(string)

[Auth Events]
└── OnAuthenticatedEvent(bool)

[Channel Events]
├── OnChannelJoinedEvent(string)
├── OnChannelJoinedCompleteEvent(ChannelJoinResult)  # RecentMessages + Members 포함
└── OnChannelLeftEvent(string)

[Message Events]
├── OnMessageReceivedEvent(ChannelMessage)
└── OnWhisperReceivedEvent(WhisperMessage)           # 귓속말 수신

[Notification Events]
├── OnAnnouncementReceivedEvent(AnnouncementMessage) # 공지사항 수신
└── OnUserActionNotificationEvent(UserActionNotificationMessage)  # 유저 행동 알림

[Rich Content Events]
└── OnLinkClickedEvent(RichLinkData)                 # 링크 클릭 (아이템, 유저 등)
```

#### 코드 사용

```csharp
[SerializeField] private ChatUIManager chatUI;

async void Start()
{
    // Level 2/3: 커스텀 서비스 주입 (선택)
    // chatUI.SetChatService(myChatService);

    // 서버 연결
    await chatUI.ConnectAsync("localhost", 7777);

    // 로그인 (기본)
    await chatUI.LoginAsync("user123");

    // 로그인 (프로필 포함)
    await chatUI.LoginAsync("user123", null, "홍길동",
        profileImage: "avatar_01", frameImage: "frame_gold",
        extraData: "{\"level\":50}");

    // 채널 입장
    await chatUI.JoinChannelAsync("lobby");
}

// 메시지 전송
public async void OnSendButtonClick(string text)
{
    await chatUI.SendMessageAsync(text);
}

// 귓속말 전송
public async void OnWhisperButtonClick(string targetUserId, string text)
{
    await chatUI.SendWhisperAsync(targetUserId, text);
}

// 시스템 메시지 표시 (서버와 무관, 로컬 UI에만 표시)
public void ShowNotice(string text)
{
    chatUI.AddSystemMessage(text);
}

// 메시지 전체 삭제
public void ClearChat()
{
    chatUI.ClearMessages();
}
```

---

### ChatUIBuilder

**역할**: 런타임에 프리팹 없이 완전한 채팅 UI를 동적으로 생성합니다.

```csharp
using UniversalChat.UI;

// 완전한 채팅 UI 생성
ChatUIManager chatUI = ChatUIBuilder.Build(canvas.transform, uiConfig);

// 메시지 뷰만 생성 (커스텀 UI에 임베딩)
ChatMessageView messageView = ChatUIBuilder.BuildMessageView(parent, uiConfig);

// 가상화 채팅 패널만 생성
VirtualizedChatPanel panel = ChatUIBuilder.CreateVirtualizedChatPanel(parent, uiConfig);
```

`ChatUIBuilder.Build()`가 생성하는 UI 계층:

```
Chat UI (ChatUIManager)
├── Header
│   ├── Title Text ("Chat")
│   └── Status Text (연결 상태 표시)
├── VirtualizedChatPanel (ScrollRect)
│   └── Viewport
│       └── Content (VerticalLayoutGroup)
│           └── [ChatMessageView] × N  (동적 생성/재사용)
└── Input Area
    ├── Input Field (TMP_InputField)
    │   └── Text Area
    │       ├── Placeholder ("Type a message...")
    │       └── Text
    └── Send Button
```

---

## UI 생성 및 설정

### 방법 1: 에디터 메뉴 사용

#### 샘플 씬 생성

```
메뉴: UniversalChat → Create Sample Scene
```

완전한 채팅 UI가 포함된 새 씬을 생성합니다. Canvas, EventSystem, ChatUIManager가 자동 구성됩니다.

#### 현재 씬에 UI 추가

```
메뉴: UniversalChat → Add Chat UI to Scene
```

현재 씬에 채팅 UI를 추가합니다 (우측 하단 배치). Rich Content 시스템도 함께 구성됩니다.

### 방법 2: 코드에서 생성

```csharp
using UnityEngine;
using UniversalChat.UI;

public class GameManager : MonoBehaviour
{
    [SerializeField] private Canvas canvas;
    [SerializeField] private ChatUIConfig uiConfig; // 선택사항

    private ChatUIManager chatUI;

    void Start()
    {
        // 런타임에 채팅 UI 생성
        chatUI = ChatUIBuilder.Build(canvas.transform, uiConfig);

        // 위치/크기 커스터마이징
        var rect = chatUI.GetComponent<RectTransform>();
        rect.anchorMin = new Vector2(1, 0);    // 우측 하단 앵커
        rect.anchorMax = new Vector2(1, 0);
        rect.pivot = new Vector2(1, 0);
        rect.anchoredPosition = new Vector2(-10, 10);
        rect.sizeDelta = new Vector2(400, 500);

        // 서버 연결
        _ = ConnectToServer();
    }

    async Task ConnectToServer()
    {
        await chatUI.ConnectAsync("localhost", 7777);
        await chatUI.LoginAsync("user123", nickname: "TestUser");
        await chatUI.JoinChannelAsync("lobby");
    }
}
```

### ChatUIConfig 테마 설정

#### Config 생성

```
메뉴: Assets → Create → UniversalChat → UI Config
```

#### 주요 설정 항목

| 카테고리 | 항목 | 설명 | 기본값 |
|----------|------|------|--------|
| **배경색** | Background Color | 전체 배경색 | - |
| | Panel Background Color | 메시지 패널 배경색 | - |
| | Input Background Color | 입력 필드 배경색 | - |
| **버튼** | Send Button Color | 전송 버튼 색 | - |
| | Send Button Hover Color | 호버 시 색 | - |
| | Send Button Text Color | 버튼 텍스트 색 | - |
| | Send Button Text | 버튼 텍스트 | "Send" |
| **헤더** | Title Color | 제목 색 | - |
| | Connected Color | 연결됨 상태 색 | 녹색 |
| | Disconnected Color | 연결 끊김 상태 색 | 빨간색 |
| | Title Font Size | 제목 글자 크기 | - |
| **메시지** | Message Color | 기본 메시지 색 | - |
| | Nickname Color | 닉네임 색 | - |
| | Timestamp Color | 시간 표시 색 | - |
| | System Text Color | 시스템 메시지 색 | - |
| | Error Text Color | 에러 메시지 색 | - |
| **말풍선** | My Bubble Color | 내 메시지 말풍선 색 | - |
| | Other Bubble Color | 상대 메시지 말풍선 색 | - |
| | System Bubble Color | 시스템 메시지 말풍선 색 | - |
| **폰트** | Main Font | TMP 폰트 에셋 | - |
| | Message Font Size | 메시지 글자 크기 | 14 |
| | Nickname Font Size | 닉네임 글자 크기 | 12 |
| | Input Font Size | 입력 필드 글자 크기 | - |
| **레이아웃** | Message Spacing | 메시지 간 간격 | - |
| | Message Padding | 메시지 내부 여백 | - |
| | Bubble Corner Radius | 말풍선 모서리 곡률 | - |
| | Max Message Width | 최대 메시지 너비 | - |
| | Default Item Height | 기본 항목 높이 | - |
| **입력** | Placeholder Text | 입력창 placeholder | "Type a message..." |
| | Max Message Length | 최대 메시지 길이 | 500 |
| **동작** | Max Visible Messages | 표시할 최대 메시지 수 | 100 |
| | Message History Size | 히스토리 크기 | - |
| | Show Timestamps | 시간 표시 여부 | true |
| | Show User Avatars | 아바타 표시 여부 | - |
| | Auto Scroll To Bottom | 자동 스크롤 | true |
| **사운드** | Message Received Sound | 수신 사운드 | - |
| | Message Sent Sound | 전송 사운드 | - |
| | Error Sound | 에러 사운드 | - |

---

## 게임 통합 패턴

### GameChatManager 샘플

`GameChatManager`는 `ChatServiceBase<ChannelType>`을 상속한 샘플 코드입니다.
`ClassifyChannel()` 하나만 구현하면 채널 타입별 관리가 자동으로 제공됩니다.

```csharp
using UniversalChat.Core;
using UniversalChat.Game;

public class MyGame : MonoBehaviour
{
    [SerializeField] private ChatUIManager chatUI;

    private GameChatManager _chatService;

    void Awake()
    {
        _chatService = new GameChatManager();
        chatUI.SetChatService(_chatService);  // ChatUIManager에 서비스 주입
    }

    async void Start()
    {
        // 서버 연결 및 로그인
        bool success = await _chatService.ConnectAndLoginAsync(
            "localhost", 7777, "user123", nickname: "홍길동");

        if (!success) return;

        // 월드 채널 자동 배정
        await _chatService.JoinWorldChannelAsync();

        // 길드 채널 입장
        await _chatService.JoinGuildChannelAsync("abc123");

        // 채널 타입별 이벤트 구독
        _chatService.OnTypedMessageReceived += (type, msg) =>
            Debug.Log($"[{type}] {msg.SenderNickname}: {msg.Content}");
    }

    void OnDestroy() => _chatService?.Dispose();
}
```

#### GameChatManagerBehaviour (MonoBehaviour 래퍼)

Inspector에서 설정할 수 있는 MonoBehaviour 래퍼도 제공됩니다:

```csharp
using UniversalChat.Game;

// GameChatManagerBehaviour를 씬에 배치하면:
// - Inspector에서 서버 주소, 자동 연결, 자동 로그인 설정
// - ChatUIManager 자동 연결
// - Start()에서 자동으로 연결 + 월드 채널 입장
```

### 채널 타입별 관리

`GameChatManager`는 채널 ID 접두사로 타입을 자동 판별합니다:

| 접두사 | ChannelType | 예시 |
|--------|-------------|------|
| `world` / `world_` | `World` | `world`, `world_1`, `world_asia` |
| `guild_` | `Guild` | `guild_abc123` |
| `party_` | `Party` | `party_xyz` |
| 기타 | `Custom` | `lobby`, `trade` |

```csharp
var chat = new GameChatManager();

// 채널 타입별 메시지 전송 (편의 메서드)
await chat.SendWorldMessageAsync("월드 메시지");
await chat.SendGuildMessageAsync("길드 메시지");
await chat.SendPartyMessageAsync("파티 메시지");

// 채널 타입별 퇴장 (편의 메서드)
await chat.LeaveGuildChannelAsync();
await chat.LeavePartyChannelAsync();

// 현재 채널 확인 (편의 프로퍼티)
string worldChannel = chat.CurrentWorldChannelId;   // "world_1"
string guildChannel = chat.CurrentGuildChannelId;    // "guild_abc"
string partyChannel = chat.CurrentPartyChannelId;    // null (미입장)

// 채널 타입 확인 (ChatServiceBase 메서드)
bool isInGuild = chat.IsInChannelType(GameChatManager.ChannelType.Guild);
```

### 메시지 히스토리

`ChatServiceBase`가 채널별 히스토리를 자동으로 관리합니다:

```csharp
var chat = new GameChatManager();

// 채널별 히스토리 조회 (최근 N개)
List<ChannelMessage> worldHistory = chat.GetMessageHistory(
    GameChatManager.ChannelType.World, 50);
List<ChannelMessage> customHistory = chat.GetMessageHistory("lobby", 100);

// 멤버 목록 조회
List<UserInfo> worldMembers = chat.GetChannelMembers("world_1");

// 상태 전체 초기화
chat.ClearAllState();
```

---

## 이벤트 시스템

### IChatService 이벤트 (C# Action)

`IChatService`를 구현하는 모든 컴포넌트 (ChatManager, ChatServiceBase, 커스텀)에서 사용 가능합니다.

```csharp
IChatService chat = ...; // ChatManager.Instance 또는 커스텀 서비스

// 구독
chat.OnMessageReceived += HandleMessage;
chat.OnError += HandleError;

// 해제 (OnDestroy에서)
chat.OnMessageReceived -= HandleMessage;
chat.OnError -= HandleError;

void HandleMessage(ChannelMessage msg) { /* ... */ }
void HandleError(string error) { /* ... */ }
```

### Typed Channel 이벤트 (ChatServiceBase)

`ChatServiceBase<T>`에서만 사용 가능한 채널 타입별 이벤트입니다:

```csharp
var chat = new MyChatService();

// 채널 타입별 메시지 수신
chat.OnTypedMessageReceived += (type, msg) =>
    Debug.Log($"[{type}] {msg.SenderNickname}: {msg.Content}");

// 채널 타입별 입장
chat.OnTypedChannelJoined += (type, channelId, result) =>
    Debug.Log($"[{type}] 입장: {channelId}, 멤버 {result.Members.Count}명");

// 채널 타입별 퇴장
chat.OnTypedChannelLeft += (type, channelId) =>
    Debug.Log($"[{type}] 퇴장: {channelId}");

// 채널 타입별 멤버 업데이트
chat.OnTypedMembersUpdated += (type, channelId, members) =>
    Debug.Log($"[{type}] {channelId} 멤버 변경: {members.Count}명");
```

### UnityEvent 이벤트 (ChatUIManager)

`ChatUIManager`는 Inspector에서 연결 가능한 13개의 `UnityEvent`를 제공합니다.

```csharp
// 코드에서 구독 - Lifecycle
chatUIManager.OnChatReadyEvent.AddListener(OnChatReady);
chatUIManager.OnConnectedEvent.AddListener(OnConnected);
chatUIManager.OnDisconnectedEvent.AddListener(OnDisconnected);
chatUIManager.OnErrorEvent.AddListener(OnError);

// Auth
chatUIManager.OnAuthenticatedEvent.AddListener(OnAuthenticated);

// Channel
chatUIManager.OnChannelJoinedEvent.AddListener(OnChannelJoined);
chatUIManager.OnChannelJoinedCompleteEvent.AddListener(OnChannelJoinedComplete);
chatUIManager.OnChannelLeftEvent.AddListener(OnChannelLeft);

// Message
chatUIManager.OnMessageReceivedEvent.AddListener(OnMessage);
chatUIManager.OnWhisperReceivedEvent.AddListener(OnWhisper);

// Notification
chatUIManager.OnAnnouncementReceivedEvent.AddListener(OnAnnouncement);
chatUIManager.OnUserActionNotificationEvent.AddListener(OnUserAction);

// Rich Content
chatUIManager.OnLinkClickedEvent.AddListener(OnLinkClicked);

void OnChatReady() { Debug.Log("채팅 준비 완료!"); }
void OnConnected() { Debug.Log("서버 연결됨"); }
void OnDisconnected(string reason) { Debug.Log($"연결 끊김: {reason}"); }
void OnError(string error) { Debug.LogError($"에러: {error}"); }
void OnAuthenticated(bool success) { Debug.Log($"인증: {success}"); }
void OnChannelJoined(string channelId) { Debug.Log($"채널 입장: {channelId}"); }
void OnChannelJoinedComplete(ChannelJoinResult result) {
    Debug.Log($"채널 {result.ChannelId} 입장, 멤버 {result.Members.Count}명, " +
              $"히스토리 {result.RecentMessages.Count}개");
}
void OnChannelLeft(string channelId) { Debug.Log($"채널 퇴장: {channelId}"); }
void OnMessage(ChannelMessage msg) { Debug.Log($"[{msg.SenderNickname}] {msg.Content}"); }
void OnWhisper(WhisperMessage whisper) {
    Debug.Log($"[귓속말] {whisper.SenderNickname}: {whisper.Content}");
}
void OnAnnouncement(AnnouncementMessage ann) { Debug.Log($"[공지] {ann.Content}"); }
void OnUserAction(UserActionNotificationMessage notif) {
    Debug.Log($"[알림] {notif.ActorNickname}: {notif.Title}");
}
void OnLinkClicked(RichLinkData link) {
    Debug.Log($"[링크] 타입={link.LinkType}, 파라미터={link.Param1}");
}
```

Inspector에서도 직접 이벤트를 연결할 수 있습니다:

```
ChatUIManager (Inspector)
├── Lifecycle Events
│   ├── OnChatReadyEvent       → [MyScript.OnChatReady()]
│   ├── OnConnectedEvent       → [MyScript.OnConnected()]
│   ├── OnDisconnectedEvent    → [ErrorPopup.Show(string)]
│   └── OnErrorEvent           → [ErrorPopup.ShowError(string)]
├── Auth Events
│   └── OnAuthenticatedEvent   → [MyScript.OnAuthenticated(bool)]
├── Channel Events
│   ├── OnChannelJoinedEvent           → [MyScript.OnChannelJoined(string)]
│   ├── OnChannelJoinedCompleteEvent   → [MyScript.OnChannelJoinedComplete(ChannelJoinResult)]
│   └── OnChannelLeftEvent             → [MyScript.OnChannelLeft(string)]
├── Message Events
│   ├── OnMessageReceivedEvent  → [MyChatUI.DisplayMessage(ChannelMessage)]
│   └── OnWhisperReceivedEvent  → [MyChatUI.DisplayWhisper(WhisperMessage)]
├── Notification Events
│   ├── OnAnnouncementReceivedEvent     → [NoticeUI.Show(AnnouncementMessage)]
│   └── OnUserActionNotificationEvent   → [NoticeUI.ShowAction(UserActionNotificationMessage)]
└── Rich Content Events
    └── OnLinkClickedEvent     → [ItemPopup.ShowFromLink(RichLinkData)]
```

### 이벤트 전체 목록

| 컴포넌트 | 이벤트 | 파라미터 |
|----------|--------|----------|
| **IChatService** | `OnConnected` | (없음) |
| (모든 Level) | `OnDisconnected` | `string reason` |
| | `OnError` | `string error` |
| | `OnAuthenticated` | `bool success, string message` |
| | `OnMessageReceived` | `ChannelMessage` |
| | `OnChannelJoined` | `string channelId, string channelName` |
| | `OnChannelJoinedWithHistory` | `ChannelJoinResult` |
| | `OnChannelLeft` | `string channelId` |
| | `OnChannelListUpdated` | `List<ChannelInfo>` |
| | `OnUserListUpdated` | `string channelId, List<UserInfo>` |
| | `OnWhisperReceived` | `WhisperMessage` |
| | `OnAnnouncementReceived` | `AnnouncementMessage` |
| | `OnUserActionNotificationReceived` | `UserActionNotificationMessage` |
| | `OnChatReady` | (없음) |
| **ChatServiceBase** | `OnTypedMessageReceived` | `TChannelType, ChannelMessage` |
| (Level 2 전용) | `OnTypedChannelJoined` | `TChannelType, string, ChannelJoinResult` |
| | `OnTypedChannelLeft` | `TChannelType, string` |
| | `OnTypedMembersUpdated` | `TChannelType, string, List<UserInfo>` |
| **ChatUIManager** | `OnChatReadyEvent` | (없음) |
| (UnityEvent) | `OnConnectedEvent` | (없음) |
| | `OnDisconnectedEvent` | `string` |
| | `OnErrorEvent` | `string` |
| | `OnAuthenticatedEvent` | `bool` |
| | `OnChannelJoinedEvent` | `string` |
| | `OnChannelJoinedCompleteEvent` | `ChannelJoinResult` |
| | `OnChannelLeftEvent` | `string` |
| | `OnMessageReceivedEvent` | `ChannelMessage` |
| | `OnWhisperReceivedEvent` | `WhisperMessage` |
| | `OnAnnouncementReceivedEvent` | `AnnouncementMessage` |
| | `OnUserActionNotificationEvent` | `UserActionNotificationMessage` |
| | `OnLinkClickedEvent` | `RichLinkData` |

---

## 데이터 모델

모든 데이터 모델은 Protobuf 메시지를 래핑하는 C# 클래스입니다 (`DataModels.cs`).

### ChannelInfo

```csharp
public class ChannelInfo
{
    public string ChannelId { get; set; }     // "world_1"
    public string ChannelName { get; set; }   // "월드 채널 1"
    public int CurrentUsers { get; set; }     // 현재 인원
    public int MaxUsers { get; set; }         // 최대 인원
    public bool IsSystem { get; set; }        // 시스템 채널 여부
    public bool HasPassword { get; set; }     // 비밀번호 여부
}
```

### UserInfo

```csharp
public class UserInfo
{
    public string UserId { get; set; }
    public string Nickname { get; set; }
    public string ProfileImage { get; set; }  // 아바타 ID
    public string FrameImage { get; set; }    // 프레임 ID
    public string ExtraData { get; set; }     // 게임별 커스텀 데이터 (JSON)
    public long JoinedAt { get; set; }        // Unix 밀리초
}
```

### ChannelMessage

```csharp
public class ChannelMessage
{
    public string MessageId { get; set; }         // 고유 ID (Snowflake)
    public string ChannelId { get; set; }
    public string SenderId { get; set; }
    public string SenderNickname { get; set; }
    public string SenderProfileImage { get; set; }
    public string SenderFrameImage { get; set; }
    public string SenderExtraData { get; set; }
    public string Content { get; set; }           // 메시지 내용
    public long Timestamp { get; set; }           // Unix 밀리초
    public int MessageType { get; set; }          // 0: Normal, 1: System, 2: Whisper
    public DateTime DateTime { get; }             // 로컬 시간으로 변환
}
```

### ChannelJoinResult

채널 입장 완료 시 `OnChannelJoinedWithHistory` / `OnChannelJoinedCompleteEvent`로 전달되는 통합 결과 객체입니다.

```csharp
public class ChannelJoinResult
{
    public string ChannelId { get; set; }
    public string ChannelName { get; set; }
    public bool IsAutoAssign { get; set; }        // 자동 배정 여부
    public bool Success { get; set; }
    public string ErrorMessage { get; set; }
    public List<UserInfo> Members { get; set; }              // 채널 멤버 목록
    public List<ChannelMessage> RecentMessages { get; set; } // 최근 메시지 히스토리
}
```

### WhisperMessage

```csharp
public class WhisperMessage
{
    public string MessageId { get; set; }
    public string SenderId { get; set; }
    public string SenderNickname { get; set; }
    public string SenderProfileImage { get; set; }
    public string SenderFrameImage { get; set; }
    public string SenderExtraData { get; set; }
    public string Content { get; set; }
    public long Timestamp { get; set; }
    public DateTime DateTime { get; }
}
```

### AnnouncementMessage

```csharp
public class AnnouncementMessage
{
    public string AnnouncementId { get; set; }
    public string Content { get; set; }
    public AnnouncementType Type { get; set; }    // Normal, Urgent, Maintenance, Event
    public string SenderName { get; set; }
    public int DurationSeconds { get; set; }      // 표시 지속 시간
    public string TargetChannel { get; set; }
    public string ExtraData { get; set; }
    public long Timestamp { get; set; }
    public DateTime DateTime { get; }
}
```

### UserActionNotificationMessage

```csharp
public class UserActionNotificationMessage
{
    public string NotificationId { get; set; }
    public UserActionType ActionType { get; set; }  // ItemAcquire, RankingBreakthrough, etc.
    public string ActorUserId { get; set; }
    public string ActorNickname { get; set; }
    public string ActorProfileImage { get; set; }
    public string ActorFrameImage { get; set; }
    public string Title { get; set; }               // "레어 아이템 획득!"
    public string Content { get; set; }              // "홍길동님이 전설의 검을 획득했습니다"
    public string IconId { get; set; }
    public string ExtraData { get; set; }
    public long Timestamp { get; set; }
    public DateTime DateTime { get; }
}
```

### Enum 참조

```csharp
public enum AnnouncementType
{
    Normal = 0,       // 일반 공지
    Urgent = 1,       // 긴급 공지
    Maintenance = 2,  // 점검 공지
    Event = 3         // 이벤트 공지
}

public enum UserActionType
{
    ItemAcquire = 0,          // 레어 아이템 획득
    RankingBreakthrough = 1,  // 랭킹 돌파
    MissionComplete = 2,      // 최고 미션 달성
    Achievement = 3,          // 업적 달성
    Custom = 99               // 게임 커스텀
}
```

---

## Rich Content 시스템

채팅 메시지에 클릭 가능한 링크(아이템, 유저 등)를 추가할 수 있습니다.

### 빠른 시작

```csharp
using UniversalChat.RichContent;

void Start()
{
    var manager = RichContentManager.Instance;

    // Provider 등록 (링크 데이터 → 표시 텍스트)
    manager.RegisterProvider(new MyItemDataProvider());

    // Handler 등록 (클릭 이벤트 처리)
    manager.RegisterHandler(new MyItemLinkHandler());
}
```

### 태그 형식

```
[TYPE:param1:param2:...]

예시:
[ITEM:1001:5]        → "[전설의 검 +5]" (클릭 가능)
[USER:user123:홍길동] → "홍길동" (클릭 가능)
[QUEST:quest_001]    → "[퀘스트명]" (클릭 가능)
```

### 태그 생성

```csharp
string itemTag = RichTextParser.CreateTag("ITEM", "1001", "5");
await chatUI.SendMessageAsync($"이거 어때요? {itemTag}");
// 전송: "이거 어때요? [ITEM:1001:5]"
// 표시: "이거 어때요? [전설의 검 +5]"
```

### Provider 구현

```csharp
public class MyItemDataProvider : IRichContentDataProvider
{
    public string LinkType => "ITEM";

    public string GetDisplayText(RichLinkData linkData)
    {
        string itemId = linkData.Param1;
        int enhancement = linkData.GetParamAsInt(1, 0);
        string itemName = ItemDB.GetName(itemId);
        return enhancement > 0 ? $"[{itemName} +{enhancement}]" : $"[{itemName}]";
    }

    public Color? GetLinkColor(RichLinkData linkData)
    {
        int rarity = ItemDB.GetRarity(linkData.Param1);
        return RichContentManager.Instance.Config.GetRarityColor(rarity);
    }
}
```

### Handler 구현

```csharp
public class MyItemLinkHandler : IRichContentLinkHandler
{
    public string LinkType => "ITEM";

    public void OnLinkClicked(RichLinkData linkData)
    {
        string itemId = linkData.Param1;
        GameUIManager.Instance.ShowItemPopup(itemId);
    }

    public void OnLinkLongPressed(RichLinkData linkData)
    {
        GameUIManager.Instance.ShowItemQuickMenu(linkData.Param1);
    }
}
```

### 상세 가이드

Rich Content 시스템의 전체 가이드 (PopupFactory, Config, 고급 사용법, 문제 해결)는 다음 파일을 참조하세요:

**[GameIntegration/README.md](GameIntegration/README.md)**

---

## 번역 시스템

REST API 기반 다국어 번역 시스템입니다.

### 설정

```
메뉴: Assets → Create → UniversalChat → Translation Config
```

`TranslationConfig` 주요 설정:

| 항목 | 설명 | 기본값 |
|------|------|--------|
| Server Url | 번역 서버 URL | - |
| Api Key | API 키 (필요 시) | - |
| Enable Caching | 캐싱 활성화 | true |
| Cache Duration Seconds | 캐시 유지 시간 | - |
| Max Translations Per Minute | 분당 최대 번역 수 | - |

### 수동 번역

```csharp
using UniversalChat.Translation;

// 초기화
TranslationManager.Instance.Initialize(translationConfig);
// 또는
TranslationManager.Instance.Initialize("https://translate.example.com", timeout: 10f);

// 헬스 체크
bool healthy = await TranslationManager.Instance.CheckHealthAsync();

// 번역 (소스 언어 지정)
var result = await TranslationManager.Instance.TranslateAsync(
    "Hello world", "en", "ko");

if (result.Success)
    Debug.Log(result.TranslatedText); // "안녕하세요 세계"

// 번역 (소스 언어 자동 감지)
var result2 = await TranslationManager.Instance.TranslateAsync(
    "안녕하세요", "en");
```

### ChatUIManager 자동 번역

`ChatUIManager`의 Inspector에서 번역을 활성화하면 수신 메시지가 자동 번역됩니다:

```
ChatUIManager (Inspector)
└── Translation
    ├── Enable Translation: ✓
    ├── Translation Config: [TranslationConfig 에셋]
    └── Auto Translate On Receive: ✓
```

---

## 고급 사용법

### 커스텀 UI 통합

기존 게임 UI 프레임워크(Doozy UI 등)와 통합하려면 `IChatService`를 직접 사용합니다:

```csharp
using UniversalChat.Core;

public class ChatPopup : MonoBehaviour  // 또는 Doozy UIPopup 등
{
    [SerializeField] private TMP_Text messageDisplay;
    [SerializeField] private TMP_InputField inputField;

    private IChatService _chatService;

    async void Start()
    {
        // Level 1: ChatManager 사용
        _chatService = ChatManager.Instance;

        // 또는 Level 2: 커스텀 서비스 사용
        // _chatService = new MyChatService();

        // 이벤트 구독
        _chatService.OnMessageReceived += DisplayMessage;
        _chatService.OnChannelJoined += (id, name) => AddSystemMessage($"채널 [{name}] 입장");

        // 연결
        await _chatService.ConnectAsync("localhost", 7777);
        await _chatService.LoginAsync("user123", nickname: "홍길동");
        await _chatService.RequestAutoAssignChannelAsync("world");
    }

    void DisplayMessage(ChannelMessage msg)
    {
        messageDisplay.text += $"\n[{msg.SenderNickname}] {msg.Content}";
    }

    void AddSystemMessage(string text)
    {
        messageDisplay.text += $"\n<color=yellow>{text}</color>";
    }

    public async void OnSendButtonClick()
    {
        string text = inputField.text;
        if (string.IsNullOrEmpty(text)) return;

        string channelId = _chatService.CurrentChannelId;
        if (!string.IsNullOrEmpty(channelId))
            await _chatService.SendMessageAsync(channelId, text);

        inputField.text = "";
    }

    void OnDestroy()
    {
        if (_chatService != null)
            _chatService.OnMessageReceived -= DisplayMessage;
    }
}
```

### 프로필 관리

```csharp
// IChatService를 통한 프로필 업데이트 (Level 1/2/3 공통)
IChatService chat = ...;
await chat.UpdateProfileAsync(
    nickname: "새닉네임",
    profileImage: "avatar_03",
    frameImage: "frame_diamond",
    extraData: "{\"level\":99}");

// ChatClient를 통한 저수준 접근 (프로필 변경 알림 등)
var client = ChatManager.Instance.Client;
client.OnProfileChanged += profileChanged => {
    Debug.Log($"{profileChanged.UserId}의 프로필이 변경됨");
};
```

### 귓속말

3가지 레벨에서 귓속말을 사용할 수 있습니다:

```csharp
// === IChatService를 통한 귓속말 (Level 1/2/3 공통, 권장) ===
IChatService chat = ...;
await chat.SendWhisperAsync("targetUserId", "귓속말 내용입니다");

// 수신 이벤트 (Action<WhisperMessage>)
chat.OnWhisperReceived += whisper => {
    Debug.Log($"[귓속말] {whisper.SenderNickname}: {whisper.Content}");
};

// === ChatUIManager를 통한 귓속말 (Inspector 통합) ===
await chatUIManager.SendWhisperAsync("targetUserId", "귓속말 내용입니다");

// 수신 이벤트 (UnityEvent<WhisperMessage> - Inspector에서 연결 가능)
chatUIManager.OnWhisperReceivedEvent.AddListener(whisper => {
    Debug.Log($"[귓속말] {whisper.SenderNickname}: {whisper.Content}");
});

// === ChatClient를 통한 귓속말 (저수준) ===
var client = ChatManager.Instance.Client;
await client.SendWhisperAsync("targetUserId", "귓속말 내용입니다");
```

### 공지사항 수신

```csharp
IChatService chat = ...;
chat.OnAnnouncementReceived += announcement => {
    switch (announcement.Type)
    {
        case AnnouncementType.Normal:
            ShowBanner(announcement.Content);
            break;
        case AnnouncementType.Urgent:
            ShowUrgentPopup(announcement.Content);
            break;
        case AnnouncementType.Maintenance:
            ShowMaintenanceNotice(announcement.Content, announcement.DurationSeconds);
            break;
        case AnnouncementType.Event:
            ShowEventBanner(announcement.Content, announcement.ExtraData);
            break;
    }
};
```

### 유저 행동 알림 수신

서버에서 특정 유저의 특별한 행동(레어 아이템 획득 등)을 브로드캐스트합니다:

```csharp
IChatService chat = ...;
chat.OnUserActionNotificationReceived += notification => {
    switch (notification.ActionType)
    {
        case UserActionType.ItemAcquire:
            ShowItemAcquireEffect(notification);
            break;
        case UserActionType.RankingBreakthrough:
            ShowRankingNotice(notification);
            break;
        case UserActionType.Achievement:
            ShowAchievementBanner(notification);
            break;
    }
};
```

---

## 프로토콜 참조

### 패킷 헤더 구조 (8바이트)

```
[4바이트: 전체 패킷 길이 (Big-Endian)] [2바이트: 패킷 타입] [2바이트: 예약]
```

### 패킷 타입 범위

| 범위 | 카테고리 | 주요 타입 |
|------|---------|-----------|
| `0x00xx` | Connection | `0x0001` Heartbeat, `0x0002` Disconnect |
| `0x01xx` | Authentication | `0x0101` AuthRequest, `0x0102` AuthResponse, `0x0103` Logout |
| `0x02xx` | Channel | `0x0201` Create, `0x0203` Join, `0x0205` Leave, `0x0207` List, `0x020A` AutoAssign |
| `0x03xx` | Message | `0x0301` Send, `0x0302` Receive, `0x0303` Ack |
| `0x04xx` | Whisper | `0x0401` Send, `0x0402` Receive, `0x0403` Ack |
| `0x05xx` | Profile | `0x0501` UpdateRequest, `0x0502` UpdateResponse, `0x0503` Changed |
| `0x06xx` | Announcement | `0x0601` Send, `0x0602` Receive, `0x0603` Ack |
| `0x07xx` | UserAction | `0x0701` Send, `0x0702` Receive, `0x0703` Ack |
| `0xFFxx` | Error | `0xFF01` ServerError |

### Protobuf 재생성

proto 파일 수정 시 C# 클래스 재생성:

```bash
# 프로젝트 루트(UniversalChatServer/)에서
protoc --csharp_out=unity-client/Assets/Plugins/UniversalChat/Runtime/Protocol proto/chat.proto
```

---

## 문제 해결

### "서버 연결 실패"

1. **서버 실행 확인**: `chat_server`가 실행 중인지 확인
2. **IP/Port 확인**: `ConnectAsync()`에 올바른 주소/포트 전달
3. **방화벽**: 해당 포트가 열려 있는지 확인
4. **ChatManager 존재 확인**: `ChatManager.Instance`는 씬에 없으면 자동 생성되지만, `DontDestroyOnLoad` 처리 확인

### "메시지가 표시되지 않음"

1. **채널 입장 확인**: `JoinChannelAsync()` 후 `OnChannelJoined` 이벤트 수신 확인
2. **현재 채널 확인**: `IChatService.CurrentChannelId`가 올바른지 확인
3. **ChatUIConfig 연결**: ChatUIManager에 Config가 연결되어 있는지 확인
4. **VirtualizedChatPanel**: 스크롤뷰 설정 확인

### "커스텀 서비스의 이벤트가 ChatUIManager에 연결 안됨"

1. **SetChatService() 호출 확인**: 커스텀 IChatService를 사용할 때 반드시 `chatUIManager.SetChatService(service)` 호출
2. **호출 시점**: `SetChatService()`는 `Awake()` 또는 `Start()` 초반에 호출 (이벤트 구독 전에)
3. **Dispose 확인**: ChatServiceBase 사용 시 `OnDestroy()`에서 `Dispose()` 호출

### "내 메시지와 다른 메시지 구분이 안됨"

1. `LoginAsync()`에서 `userId` 파라미터가 올바르게 전달되는지 확인
2. ChatUIManager가 로그인 후 userId를 정확히 수신하는지 확인

### "이벤트가 호출되지 않음"

1. **구독 시점 확인**: 이벤트 구독이 `ConnectAsync()` 이전에 이루어지는지 확인
2. **MainThread 확인**: Unity UI 업데이트는 반드시 메인 스레드에서 (자동 처리됨)
3. **서비스 연결 확인**: ChatUIManager에 올바른 IChatService가 연결되어 있는지 확인 (SetChatService 또는 자동 감지)

### "Rich Content 링크가 클릭되지 않음"

1. `RichContentManager.Instance`가 존재하는지 확인
2. Provider/Handler가 등록되어 있는지 확인
3. `RichChatText` 컴포넌트의 `Raycast Target`이 활성화되어 있는지 확인
4. `SetRawText()`를 사용해야 함 (TMP `text` 직접 설정은 태그 변환이 안됨)

### "번역이 동작하지 않음"

1. `TranslationConfig`에 올바른 서버 URL이 설정되어 있는지 확인
2. `TranslationManager.Instance.IsAvailable`이 `true`인지 확인
3. `CheckHealthAsync()`로 서버 상태 확인
4. 레이트 리미팅에 걸리지 않았는지 확인

### "패킷 타입 불일치 에러"

패킷 타입은 서버(`PacketTypes.hpp`)와 클라이언트(`PacketType.cs`)가 반드시 동일해야 합니다. proto 파일 수정 후 양쪽 모두 재빌드하세요.

---

## 관련 문서

| 문서 | 위치 | 내용 |
|------|------|------|
| Rich Content 상세 가이드 | [GameIntegration/README.md](GameIntegration/README.md) | Provider/Handler/PopupFactory 구현 |
| 서버 CLAUDE.md | `UniversalChatServer/CLAUDE.md` | 서버 빌드/설정/아키텍처 |
| Unity 클라이언트 CLAUDE.md | `unity-client/CLAUDE.md` | 프로젝트 개요/아키텍처 |
| Protobuf 정의 | `proto/chat.proto` | 프로토콜 메시지 정의 |
| 서버 설정 | `config/server.json` | 서버 설정 파일 |
