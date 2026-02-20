# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

UniversalChat Unity Client는 UniversalChatServer(C++ 채팅 서버)와 통신하는 Unity 채팅 클라이언트 패키지입니다.

**기술 스택**: Unity 2020.3+, C#, Protobuf (Google.Protobuf), TCP 소켓
**프로토콜**: 8바이트 헤더 (4바이트 길이 + 2바이트 타입 + 2바이트 예약) + Protobuf 바디
**최신 버전**: v2.0.0 (DM 서브시스템 + 채널 자동생성)

## 빌드 및 테스트

### Unity 패키지 사용

```bash
# UPM Git URL로 설치
https://github.com/psw-sol/UniversalChat.git?path=unity-client/Assets/Plugins/UniversalChat#v2.0.0

# 또는 Unity Package Manager에서 추가
# Window → Package Manager → "+" → Add package from disk
# Assets/Plugins/UniversalChat/package.json 선택
```

### 콘솔 테스트 클라이언트

```bash
cd TestConsole/ChatTestClient

# 빌드
dotnet build

# 실행 (서버가 localhost:7777에서 실행 중이어야 함)
dotnet run
```

### Protobuf 재생성

proto 파일 수정 시 C# 클래스 재생성:

```bash
# 프로젝트 루트(UniversalChatServer/)에서
protoc --csharp_out=unity-client/Assets/Plugins/UniversalChat/Runtime/Protocol proto/chat.proto

# 테스트 클라이언트용
protoc --csharp_out=unity-client/TestConsole/ChatTestClient proto/chat.proto
```

## 아키텍처

### 3-Level 사용 패턴

- **Level 1 (Zero Code)**: ChatManager + ChatUIManager (Inspector만으로 사용)
- **Level 2 (Minimal Code)**: ChatServiceBase<T> 상속 → ClassifyChannel() 하나만 구현
- **Level 3 (Full Control)**: IChatService 직접 구현

### 이벤트 파이프라인

```
ChatClient (proto) → IChatService 구현체 (Domain 변환) → ChatUIManager (UnityEvent)
```

```
ChatUIManager (UI) ──→ IChatService ──→ ChatClient ──→ ChatConnection
        │               │                    │              │
        │               │                    │              ├─ TCP 소켓 연결
        │               │                    │              └─ 패킷 수신 루프
        │               │                    │
        │               │                    ├─ 이벤트 기반 응답 처리
        │               │                    └─ Heartbeat 관리
        │               │
        │               ├─ ChatManager (Level 1 싱글톤)
        │               ├─ ChatServiceBase<T> (Level 2 제네릭)
        │               └─ 커스텀 구현 (Level 3)
        │
        └─ 17개 UnityEvent 기반 UI 업데이트
```

### 핵심 컴포넌트

| 클래스 | 역할 |
|--------|------|
| `IChatService` | 채팅 서비스 추상화 인터페이스 (모든 UI가 의존) |
| `ChatServiceBase<T>` | 제네릭 기본 클래스 (채널 타입별 관리, DM, 재연결, 히스토리) |
| `ChatManager` | MonoBehaviour 싱글톤 + IChatService 구현체 (Level 1) |
| `ChatClient` | 저수준 TCP 통신, Protobuf 이벤트, DM 패킷 송수신 |
| `ChatConnection` | TCP 소켓, 패킷 송수신, MainThread 디스패치 |
| `ChatUIManager` | Plug&Play UI, IChatService 의존, 17개 UnityEvent |
| `ChatUIBuilder` | 프리팹 없이 런타임 UI 동적 생성 |
| `VirtualizedChatPanel` | 가상화 스크롤 (대량 메시지 성능) |
| `MainThreadDispatcher` | 네트워크 콜백 → 메인 스레드 디스패치 |
| `PacketSerializer` | Protobuf 직렬화/역직렬화 |

### 패킷 타입 (서버와 동기화 필요)

```
0x00xx - Connection (Heartbeat)
0x01xx - Authentication
0x02xx - Channel (List, Join, Leave, AutoAssign)
0x03xx - Message (Send, Receive, History, Announcement)
0x04xx - Whisper (온라인 전용 1:1)
0x05xx - Profile
0x08xx - DM (영속적 1:1 대화, 히스토리, 읽음확인)  [v2.0]
0xFFxx - Error
```

### DM 서브시스템 (v2.0)

- **DM vs Whisper**: Whisper(0x04xx)는 온라인 전용 유지, DM(0x08xx)은 영속+히스토리+읽음확인
- **DM 채널 ID**: `dm:{sorted_user1}:{user2}` (알파벳 정렬 고유성)
- **패킷**: DMStart, DMList, DMMessageSend/Receive, DMReadReceipt, DMHistory, DMDelete (13개)
- **IChatService DM API**: StartDMAsync, GetDMListAsync, SendDMMessageAsync, MarkDMReadAsync, LoadDMHistoryAsync, DeleteDMAsync
- **DM 이벤트**: OnDMStarted, OnDMMessageReceived, OnDMReadReceiptReceived, OnDMListUpdated
- **데이터 모델**: DMConversation (대화 요약), DMReadReceiptData (읽음 확인)
- **TaskCompletionSource 패턴**: 비동기 요청-응답 (10초 타임아웃)

### 채널 자동생성 (v2.0)

- 서버 설정: `channel.auto_create_prefixes` (예: `["guild_", "alliance_", "party_"]`)
- JoinChannel 시 prefix 매칭으로 자동 생성 (클라이언트 변경 불필요)

### ChatUIManager 17개 UnityEvent

| 카테고리 | 이벤트 | 파라미터 |
|----------|--------|----------|
| Lifecycle | OnChatReadyEvent | (없음) |
| Lifecycle | OnConnectedEvent | (없음) |
| Lifecycle | OnDisconnectedEvent | string |
| Lifecycle | OnErrorEvent | string |
| Auth | OnAuthenticatedEvent | bool |
| Channel | OnChannelJoinedEvent | string |
| Channel | OnChannelJoinedCompleteEvent | ChannelJoinResult |
| Channel | OnChannelLeftEvent | string |
| Message | OnMessageReceivedEvent | ChannelMessage |
| Message | OnWhisperReceivedEvent | WhisperMessage |
| Notification | OnAnnouncementReceivedEvent | AnnouncementMessage |
| Notification | OnUserActionNotificationEvent | UserActionNotificationMessage |
| RichContent | OnLinkClickedEvent | RichLinkData |
| DM | OnDMStartedEvent | DMConversation |
| DM | OnDMMessageReceivedEvent | ChannelMessage |
| DM | OnDMReadReceiptEvent | DMReadReceiptData |
| DM | OnDMListUpdatedEvent | List\<DMConversation\> |

## 중요 사항

- **MainThread 디스패치**: 네트워크 콜백은 `MainThreadDispatcher`를 통해 Unity 메인 스레드로 전달
- **Protobuf 네임스페이스**: `Chat.Protocol` (proto 패키지명과 일치)
- **데이터 모델**: proto 타입을 감싸는 `DataModels.cs`의 래퍼 클래스 사용
- **PacketType enum**: 서버 `PacketTypes.hpp`와 반드시 동기화
- **IChatService**: 모든 UI(ChatUIManager)는 IChatService에 의존, SetChatService()로 주입
- **범용 패키지**: 탭 UI는 패키지에 포함하지 않음 (게임 측에서 자유 구현)

## 폴더 구조

```
Assets/Plugins/UniversalChat/
├── Runtime/
│   ├── Core/           # IChatService, ChatServiceBase, ChatManager, ChatClient, DataModels
│   ├── Network/        # ChatConnection, MainThreadDispatcher
│   ├── Protocol/       # PacketType, PacketHeader, PacketSerializer, Chat.cs (생성)
│   └── UI/             # ChatUIManager, ChatUIBuilder, VirtualizedChatPanel, RichContent, Translation
├── Samples/
│   ├── Scripts/        # GameChatManager, GameChatUIExample (참고용 샘플)
│   ├── GameIntegration/ # Rich Content 가이드
│   └── README.md       # 통합 상세 가이드
└── Editor/             # 커스텀 인스펙터
```

## 사용 예시

```csharp
// Level 1: Zero Code (ChatManager 싱글톤)
var manager = ChatManager.Instance;
await manager.ConnectAsync();
await manager.LoginAsync("user123", null, "Player1");
await manager.JoinChannelAsync("world");
await manager.SendMessageAsync("Hello!");

// Level 2: Minimal Code (ChatServiceBase 상속)
public class GameChatManager : ChatServiceBase<ChannelType>
{
    protected override ChannelType ClassifyChannel(string channelId)
    {
        if (channelId.StartsWith("world")) return ChannelType.World;
        if (channelId.StartsWith("dm:")) return ChannelType.DM;
        return ChannelType.Custom;
    }
}

// DM 사용 (v2.0)
var dm = await chatService.StartDMAsync("targetUserId");
await chatService.SendDMMessageAsync(dm.DMChannelId, "Hello!");
var history = await chatService.LoadDMHistoryAsync(dm.DMChannelId, 0, 30);

// ChatUIManager DM 이벤트
chatUIManager.OnDMMessageReceivedEvent.AddListener(msg => Debug.Log(msg.Content));
chatUIManager.OnDMStartedEvent.AddListener(conv => Debug.Log(conv.PeerNickname));
```
