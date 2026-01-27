# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

UniversalChat Unity Client는 UniversalChatServer(C++ 채팅 서버)와 통신하는 Unity 채팅 클라이언트 패키지입니다.

**기술 스택**: Unity 2020.3+, C#, Protobuf (Google.Protobuf), TCP 소켓
**프로토콜**: 8바이트 헤더 (4바이트 길이 + 2바이트 타입 + 2바이트 예약) + Protobuf 바디

## 빌드 및 테스트

### Unity 패키지 사용

```bash
# Unity Package Manager에서 추가
# Window → Package Manager → "+" → Add package from disk
# Assets/Plugins/UniversalChat/package.json 선택

# 또는 Assets/Plugins/UniversalChat 폴더를 프로젝트에 복사
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

```
ChatUIManager (UI) ──→ ChatManager (싱글톤) ──→ ChatClient ──→ ChatConnection
        │                    │                      │              │
        │                    │                      │              ├─ TCP 소켓 연결
        │                    │                      │              └─ 패킷 수신 루프
        │                    │                      │
        │                    │                      ├─ 이벤트 기반 응답 처리
        │                    │                      └─ Heartbeat 관리
        │                    │
        │                    └─ 자동 재연결, 상태 관리
        │
        └─ UnityEvent 기반 UI 업데이트
```

### 핵심 컴포넌트

| 클래스 | 역할 |
|--------|------|
| `ChatConnection` | TCP 소켓, 패킷 송수신, MainThread 디스패치 |
| `ChatClient` | 비즈니스 로직, 이벤트, Heartbeat |
| `ChatManager` | 싱글톤, Unity 생명주기 통합, 자동 재연결 |
| `ChatUIManager` | Plug&Play UI, Inspector 설정, UnityEvent |
| `PacketSerializer` | Protobuf 직렬화/역직렬화 |

### 패킷 타입 (서버와 동기화 필요)

```
0x00xx - Connection (Heartbeat)
0x01xx - Authentication
0x02xx - Channel (List, Join, Leave)
0x03xx - Message (Send, Receive)
0x04xx - Whisper
0x05xx - Profile
0xFFxx - Error
```

## 중요 사항

- **MainThread 디스패치**: 네트워크 콜백은 `MainThreadDispatcher`를 통해 Unity 메인 스레드로 전달
- **Protobuf 네임스페이스**: `Chat.Protocol` (proto 패키지명과 일치)
- **데이터 모델**: proto 타입을 감싸는 `DataModels.cs`의 래퍼 클래스 사용
- **PacketType enum**: 서버 `PacketTypes.hpp`와 반드시 동기화

## 폴더 구조

```
Assets/Plugins/UniversalChat/
├── Runtime/
│   ├── Core/           # ChatClient, ChatManager, DataModels
│   ├── Network/        # ChatConnection, MainThreadDispatcher
│   ├── Protocol/       # PacketType, PacketHeader, PacketSerializer, Chat.cs (생성)
│   └── UI/             # ChatUIManager, ChatPanel, 테마
└── Editor/             # 커스텀 인스펙터
```

## 사용 예시

```csharp
// 기본 사용법
var manager = ChatManager.Instance;
await manager.ConnectAsync();
await manager.LoginAsync("user123");
await manager.JoinChannelAsync("world");
await manager.SendMessageAsync("Hello!");

// UI 매니저 사용 (Inspector 설정 기반)
chatUIManager.OnMessageReceivedEvent.AddListener(msg => Debug.Log(msg.Content));
await chatUIManager.ConnectAsync("localhost", 7777);
```
