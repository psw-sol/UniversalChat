# UniversalChat - 간편 사용 가이드

> 5분 안에 채팅을 게임에 통합하는 빠른 시작 가이드입니다.
> 전체 API 레퍼런스는 [Samples/README.md](Samples/README.md)를 참조하세요.

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

### 코드 방식

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

### ChatUIManager 방식 (Inspector 연결)

```csharp
[SerializeField] private ChatUIManager chatUI;

async void Start()
{
    await chatUI.ConnectAsync("서버IP", 7777);
    await chatUI.LoginAsync("userId", null, "닉네임");
    await chatUI.JoinChannelAsync("world");
}
```

---

## 4단계: 이벤트 연결

### Inspector에서 (드래그 앤 드롭)

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

### 코드에서

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

```csharp
var chat = ChatManager.Instance;

// 연결
await chat.ConnectAsync();
await chat.ConnectAndLoginAsync("IP", 7777, "userId", nickname: "닉네임");

// 채널
await chat.JoinChannelAsync("lobby");
await chat.JoinAutoAssignedChannelAsync("world");
await chat.LeaveChannelAsync("lobby");

// 메시지
await chat.SendMessageAsync("안녕하세요!");
await chat.SendWhisperAsync("targetUserId", "귓속말입니다");

// 상태 확인
bool connected = chat.IsConnected;
bool loggedIn = chat.IsAuthenticated;
string channel = chat.CurrentChannelId;
```

---

## 추가 기능

| 기능 | 설명 | 상세 가이드 |
|------|------|-------------|
| **Rich Content** | 메시지에 클릭 가능한 링크 삽입 | [GameIntegration/README.md](Samples/GameIntegration/README.md) |
| **번역** | REST API 기반 자동 번역 | [Samples/README.md #번역-시스템](Samples/README.md#번역-시스템) |
| **GameChatManager** | 월드/길드/파티 채널 관리 | [Samples/README.md #게임-통합-패턴](Samples/README.md#게임-통합-패턴) |
| **테마** | ScriptableObject 기반 UI 커스터마이징 | [Samples/README.md #chatuiconfig-테마-설정](Samples/README.md#chatuiconfig-테마-설정) |

---

## 문제가 생기면

| 증상 | 해결 |
|------|------|
| 서버 연결 안됨 | IP/Port 확인, 서버 실행 확인, 방화벽 확인 |
| 메시지가 안 보임 | `JoinChannelAsync()` 호출 확인 |
| 이벤트가 안 옴 | 이벤트 구독이 `ConnectAsync()` 이전인지 확인 |
| 재연결 후 동작 안됨 | ChatManager가 자동 재연결 처리 (v1.0.0) |

전체 문제 해결 가이드: [Samples/README.md #문제-해결](Samples/README.md#문제-해결)
