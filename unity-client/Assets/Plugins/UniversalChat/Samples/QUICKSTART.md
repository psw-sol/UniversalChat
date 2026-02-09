# UniversalChat - 간편 사용 가이드

> 5분 안에 채팅을 게임에 통합하는 빠른 시작 가이드입니다.
> 전체 API 레퍼런스는 [Samples/README.md](Samples/README.md)를 참조하세요.

---

## 3-Level 사용 패턴

UniversalChat은 프로젝트 요구에 맞는 3단계 사용 레벨을 제공합니다:

| Level | 이름 | 설명 | 구현량 |
|-------|------|------|--------|
| **Level 1** | Zero Code | ChatManager + ChatUIManager (Inspector만으로 사용) | 코드 없음 |
| **Level 2** | Minimal Code | `ChatServiceBase<T>` 상속, `ClassifyChannel()` 하나만 구현 | ~20줄 |
| **Level 3** | Full Control | `IChatService` 직접 구현 | 자유 |

---

## 1단계: 설치

### Unity Package Manager (권장)

`Packages/manifest.json`의 `dependencies`에 추가:

```json
"com.universalchat.client": "https://github.com/psw-sol/UniversalChat.git?path=unity-client/Assets/Plugins/UniversalChat#v1.0.0"
```

### 폴더 복사

`Assets/Plugins/UniversalChat/` 폴더를 프로젝트에 복사합니다.

> **필수 의존성**: Google.Protobuf.dll을 `Assets/Plugins/`에 배치하세요.

---

## 2단계: 씬에 채팅 UI 추가

### 방법 A: 에디터 메뉴 (가장 간단)

```
메뉴: UniversalChat → Add Chat UI to Scene
```

Canvas, ChatUIManager, 입력창, 전송 버튼이 자동으로 생성됩니다.

### 방법 B: 코드에서 생성

```csharp
using UniversalChat.UI;

var chatUI = ChatUIBuilder.Build(canvas.transform);
```

---

## 3단계: 서버 연결

### Level 1: Zero Code (ChatManager 사용)

ChatManager는 MonoBehaviour 싱글톤으로, `IChatService`를 구현합니다.
씬에 ChatUIManager를 배치하면 ChatManager를 자동으로 감지합니다.

```csharp
using UniversalChat.Core;

async void Start()
{
    var chat = ChatManager.Instance;

    // 연결 + 로그인 + 채널 입장
    await chat.ConnectAndLoginAsync("서버IP", 7777, "userId", nickname: "닉네임");
    await chat.JoinAutoAssignedChannelAsync("world");

    // 메시지 전송
    await chat.SendMessageAsync("안녕하세요!");
}
```

### Level 2: Minimal Code (ChatServiceBase 사용)

게임 전용 채팅 서비스를 최소한의 코드로 구현합니다.
**`ClassifyChannel()` 하나만 구현**하면 채널 타입별 관리, 히스토리, 재연결 등이 자동 제공됩니다.

```csharp
using UniversalChat.Core;

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
```

```csharp
// 2. MonoBehaviour에서 사용
using UniversalChat.UI;

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
        await _chatService.ConnectAndLoginAsync("서버IP", 7777, "userId", nickname: "닉네임");
        await _chatService.RequestAutoAssignChannelAsync("world");
    }

    void OnDestroy() => _chatService?.Dispose();
}
```

### Level 3: Full Control (IChatService 직접 구현)

```csharp
using UniversalChat.Core;

// IChatService를 직접 구현하여 완전한 커스터마이징
public class CustomChatService : IChatService
{
    // IChatService의 모든 프로퍼티, 메서드, 이벤트를 직접 구현
    // ... (상세 가이드: Samples/README.md 참조)
}
```

---

## 4단계: ChatUIManager 연결

### SetChatService() - 커스텀 서비스 연결

Level 2/3에서 커스텀 서비스를 사용할 때, `SetChatService()`로 ChatUIManager에 주입합니다:

```csharp
var chatService = new MyChatService();
chatUIManager.SetChatService(chatService);
```

> **하위 호환**: `SetChatService()`를 호출하지 않으면 ChatUIManager는 자동으로 `ChatManager.Instance`를 사용합니다 (Level 1 동작).

### Inspector에서 이벤트 연결 (드래그 앤 드롭)

ChatUIManager 컴포넌트의 **Events** 섹션에서 원하는 이벤트에 함수를 연결합니다:

```
ChatUIManager (Inspector)
├── Lifecycle Events
│   └── OnChatReadyEvent        → 채팅 준비 완료 시 호출
├── Message Events
│   ├── OnMessageReceivedEvent  → 메시지 수신 시 호출
│   └── OnWhisperReceivedEvent  → 귓속말 수신 시 호출
├── Notification Events
│   ├── OnAnnouncementReceivedEvent  → 공지사항 수신
│   └── OnUserActionNotificationEvent → 유저 알림 수신
└── Rich Content Events
    └── OnLinkClickedEvent      → 링크 클릭 시 호출
```

### 코드에서 이벤트 구독

```csharp
// 채팅 준비 완료
chatUI.OnChatReadyEvent.AddListener(() =>
    Debug.Log("채팅 준비 완료!"));

// 메시지 수신
chatUI.OnMessageReceivedEvent.AddListener(msg =>
    Debug.Log($"[{msg.SenderNickname}] {msg.Content}"));

// 귓속말 수신
chatUI.OnWhisperReceivedEvent.AddListener(whisper =>
    Debug.Log($"[귓속말] {whisper.SenderNickname}: {whisper.Content}"));

// 공지사항
chatUI.OnAnnouncementReceivedEvent.AddListener(ann =>
    Debug.Log($"[공지] {ann.Content}"));

// 링크 클릭 (아이템, 유저 등)
chatUI.OnLinkClickedEvent.AddListener(link =>
    Debug.Log($"링크 클릭: {link.LinkType} - {link.Param1}"));
```

---

## 전체 이벤트 목록 (13개)

| 카테고리 | 이벤트 | 파라미터 | 설명 |
|----------|--------|----------|------|
| **Lifecycle** | `OnChatReadyEvent` | - | Connect→Login→Join 완료 |
| | `OnConnectedEvent` | - | 서버 연결됨 |
| | `OnDisconnectedEvent` | string | 연결 끊김 (사유) |
| | `OnErrorEvent` | string | 에러 발생 |
| **Auth** | `OnAuthenticatedEvent` | bool | 로그인 성공/실패 |
| **Channel** | `OnChannelJoinedEvent` | string | 채널 입장 (channelId) |
| | `OnChannelJoinedCompleteEvent` | ChannelJoinResult | 채널 입장 (멤버+히스토리 포함) |
| | `OnChannelLeftEvent` | string | 채널 퇴장 |
| **Message** | `OnMessageReceivedEvent` | ChannelMessage | 채널 메시지 수신 |
| | `OnWhisperReceivedEvent` | WhisperMessage | 귓속말 수신 |
| **Notification** | `OnAnnouncementReceivedEvent` | AnnouncementMessage | 공지사항 |
| | `OnUserActionNotificationEvent` | UserActionNotificationMessage | 유저 행동 알림 |
| **RichContent** | `OnLinkClickedEvent` | RichLinkData | 링크 클릭 |

---

## Inspector 설정 한눈에 보기

```
ChatUIManager
├── Connection Settings
│   └── Connect On Start: ☐        ← 자동 연결 여부
│
├── Auto Features
│   ├── Auto Load History: ☑        ← 입장 시 최근 메시지 자동 표시
│   └── Auto Scroll To Bottom: ☑   ← 새 메시지 시 자동 스크롤
│
├── Theme
│   └── UI Config: (ChatUIConfig)   ← 테마 에셋 연결
│
├── Translation
│   ├── Enable Translation: ☐
│   └── Auto Translate: ☐           ← 수신 메시지 자동 번역
│
└── Events (13개)                   ← Inspector에서 드래그 앤 드롭
```

---

## 자주 쓰는 API

### IChatService (공통 인터페이스)

Level 1~3 모두에서 동일한 API를 사용합니다:

```csharp
IChatService chat = ...; // ChatManager.Instance 또는 커스텀 서비스

// 연결
await chat.ConnectAsync("IP", 7777);
await chat.LoginAsync("userId", nickname: "닉네임");

// 채널
await chat.JoinChannelAsync("lobby");
await chat.RequestAutoAssignChannelAsync("world");
await chat.LeaveChannelAsync("lobby");

// 메시지
await chat.SendMessageAsync("channelId", "안녕하세요!");
await chat.SendWhisperAsync("targetUserId", "귓속말입니다");

// 상태 확인
bool connected = chat.IsConnected;
bool loggedIn = chat.IsAuthenticated;
string channel = chat.CurrentChannelId;
```

### ChatManager 전용 (Level 1)

```csharp
var chat = ChatManager.Instance;

// 연결 + 로그인 한 번에
await chat.ConnectAndLoginAsync("IP", 7777, "userId", nickname: "닉네임");

// 현재 채널에 메시지 전송 (channelId 자동)
await chat.SendMessageAsync("안녕하세요!");

// 채널 목록/멤버 새로고침
await chat.RefreshChannelListAsync();
await chat.RefreshUserListAsync("lobby");
```

### ChatServiceBase 전용 (Level 2)

```csharp
var chat = new MyChatService();

// 연결 + 로그인 한 번에
await chat.ConnectAndLoginAsync("IP", 7777, "userId", nickname: "닉네임");

// 채널 타입별 메시지 전송
await chat.SendMessageToChannelTypeAsync(MyChatService.ChannelType.World, "월드 메시지");

// 히스토리 조회
var history = chat.GetMessageHistory(MyChatService.ChannelType.World, 50);

// 채널 타입별 이벤트
chat.OnTypedMessageReceived += (type, msg) =>
    Debug.Log($"[{type}] {msg.SenderNickname}: {msg.Content}");
```

---

## 추가 기능

| 기능 | 설명 | 상세 가이드 |
|------|------|-------------|
| **Rich Content** | 메시지에 클릭 가능한 링크 삽입 | [GameIntegration/README.md](Samples/GameIntegration/README.md) |
| **번역** | REST API 기반 자동 번역 | [Samples/README.md #번역-시스템](Samples/README.md#번역-시스템) |
| **GameChatManager** | 월드/길드/파티 채널 관리 샘플 | [Samples/README.md #게임-통합-패턴](Samples/README.md#게임-통합-패턴) |
| **테마** | ScriptableObject 기반 UI 커스터마이징 | [Samples/README.md #chatuiconfig-테마-설정](Samples/README.md#chatuiconfig-테마-설정) |

---

## 문제가 생기면

| 증상 | 해결 |
|------|------|
| 서버 연결 안됨 | IP/Port 확인, 서버 실행 확인, 방화벽 확인 |
| 메시지가 안 보임 | `JoinChannelAsync()` 호출 확인 |
| 이벤트가 안 옴 | 이벤트 구독이 `ConnectAsync()` 이전인지 확인 |
| 커스텀 서비스 이벤트 안 옴 | `SetChatService()` 호출 확인 |
| 재연결 후 동작 안됨 | ChatServiceBase는 자동 재연결 처리 |

전체 문제 해결 가이드: [Samples/README.md #문제-해결](Samples/README.md#문제-해결)
