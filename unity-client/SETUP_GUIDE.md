# UniversalChat Unity 클라이언트 적용 가이드

## 1. 설치 방법

### 방법 A: 폴더 직접 복사 (권장)

1. `Assets/Plugins/UniversalChat/` 폴더를 Unity 프로젝트의 `Assets/Plugins/` 경로에 복사

```
YourProject/
└── Assets/
    └── Plugins/
        └── UniversalChat/   ← 이 폴더 전체 복사
            ├── Runtime/
            ├── Editor/
            ├── Resources/
            ├── Prefabs/
            └── package.json
```

### 방법 B: Unity Package Manager (로컬 패키지)

1. Unity 에디터에서 `Window → Package Manager` 열기
2. `+` 버튼 → `Add package from disk...` 선택
3. `unity-client/Assets/Plugins/UniversalChat/package.json` 선택

---

## 2. 필수 의존성 설치

### Google.Protobuf NuGet 패키지

UniversalChat은 Protobuf를 사용합니다. 다음 중 하나의 방법으로 설치하세요:

#### 방법 A: NuGetForUnity 사용 (권장)

1. [NuGetForUnity](https://github.com/GlitchEnzo/NuGetForUnity) 설치
2. Unity 메뉴: `NuGet → Manage NuGet Packages`
3. `Google.Protobuf` 검색 후 설치 (버전 3.21.x 이상)

#### 방법 B: DLL 직접 추가

1. [NuGet Gallery](https://www.nuget.org/packages/Google.Protobuf/)에서 Google.Protobuf 다운로드
2. `.nupkg` 파일을 `.zip`으로 변경 후 압축 해제
3. `lib/netstandard2.0/Google.Protobuf.dll`을 `Assets/Plugins/` 폴더에 복사

---

## 3. 기본 사용법

### 3.1 싱글톤으로 접근

```csharp
using UniversalChat.Core;

// ChatManager는 싱글톤으로 자동 생성됨
var chatManager = ChatManager.Instance;
```

### 3.2 씬에 직접 배치

1. 빈 GameObject 생성 (예: `[ChatSystem]`)
2. `ChatManager` 컴포넌트 추가
3. Inspector에서 서버 정보 설정:
   - **Server Host**: `localhost` 또는 서버 IP
   - **Server Port**: `7777` (기본값)
   - **Auto Reconnect**: 연결 끊김 시 자동 재연결

---

## 4. 연결 및 로그인

### 기본 연결 플로우

```csharp
using UniversalChat.Core;
using UnityEngine;

public class ChatExample : MonoBehaviour
{
    async void Start()
    {
        var chat = ChatManager.Instance;

        // 서버 주소 설정 (선택사항 - Inspector에서도 설정 가능)
        chat.Configure("localhost", 7777);

        // 이벤트 구독
        chat.OnConnected += OnConnected;
        chat.OnAuthenticated += OnAuthenticated;
        chat.OnMessageReceived += OnMessageReceived;
        chat.OnError += OnError;

        // 연결 및 로그인 (한 번에)
        bool success = await chat.ConnectAndLoginAsync("player123", "MyNickname");

        if (success)
        {
            Debug.Log("로그인 성공!");
        }
    }

    void OnConnected()
    {
        Debug.Log("서버 연결됨");
    }

    void OnAuthenticated(bool success, string message)
    {
        if (success)
        {
            Debug.Log("인증 성공");
            // 채널 목록 요청
            _ = ChatManager.Instance.RefreshChannelListAsync();
        }
        else
        {
            Debug.LogError($"인증 실패: {message}");
        }
    }

    void OnMessageReceived(ChannelMessage msg)
    {
        Debug.Log($"[{msg.ChannelId}] {msg.SenderNickname}: {msg.Content}");
    }

    void OnError(string error)
    {
        Debug.LogError($"에러: {error}");
    }

    void OnDestroy()
    {
        // 이벤트 구독 해제
        var chat = ChatManager.Instance;
        chat.OnConnected -= OnConnected;
        chat.OnAuthenticated -= OnAuthenticated;
        chat.OnMessageReceived -= OnMessageReceived;
        chat.OnError -= OnError;
    }
}
```

---

## 5. 채널 관리

### 채널 목록 조회

```csharp
ChatManager.Instance.OnChannelListUpdated += (channels) =>
{
    foreach (var ch in channels)
    {
        Debug.Log($"채널: {ch.Name} ({ch.MemberCount}/{ch.MaxMembers})");
    }
};

await ChatManager.Instance.RefreshChannelListAsync();
```

### 채널 입장

```csharp
ChatManager.Instance.OnChannelJoined += (channelId, channelName) =>
{
    Debug.Log($"채널 입장: {channelName}");
};

await ChatManager.Instance.JoinChannelAsync("world");
// 비밀번호가 있는 채널
await ChatManager.Instance.JoinChannelAsync("vip-room", "password123");
```

### 채널 퇴장

```csharp
await ChatManager.Instance.LeaveChannelAsync("world");
```

---

## 6. 메시지 전송/수신

### 메시지 수신 이벤트

```csharp
ChatManager.Instance.OnMessageReceived += (message) =>
{
    // message.ChannelId   - 채널 ID
    // message.SenderId    - 발신자 ID
    // message.SenderNickname - 발신자 닉네임
    // message.Content     - 메시지 내용
    // message.Timestamp   - 전송 시간

    AddChatMessage(message);
};
```

### 현재 채널에 메시지 전송

```csharp
// 현재 입장한 채널에 전송
await ChatManager.Instance.SendMessageAsync("안녕하세요!");
```

### 특정 채널에 메시지 전송

```csharp
// 채널 ID 지정하여 전송
await ChatManager.Instance.SendMessageToChannelAsync("world", "월드 채팅입니다!");
```

---

## 7. 이벤트 목록

| 이벤트 | 설명 | 파라미터 |
|--------|------|----------|
| `OnConnected` | 서버 연결 성공 | 없음 |
| `OnDisconnected` | 서버 연결 끊김 | `string reason` |
| `OnError` | 에러 발생 | `string errorMessage` |
| `OnAuthenticated` | 인증 결과 | `bool success, string message` |
| `OnMessageReceived` | 메시지 수신 | `ChannelMessage message` |
| `OnChannelJoined` | 채널 입장 | `string channelId, string channelName` |
| `OnChannelLeft` | 채널 퇴장 | `string channelId` |
| `OnChannelListUpdated` | 채널 목록 갱신 | `List<ChannelInfo> channels` |
| `OnUserListUpdated` | 유저 목록 갱신 | `string channelId, List<UserInfo> users` |

---

## 8. Inspector 설정 항목

| 설정 | 설명 | 기본값 |
|------|------|--------|
| Server Host | 서버 주소 | localhost |
| Server Port | 서버 포트 | 7777 |
| Connection Timeout Ms | 연결 타임아웃 (밀리초) | 5000 |
| Heartbeat Interval | 하트비트 간격 (초) | 30 |
| Auto Reconnect | 자동 재연결 활성화 | true |
| Reconnect Delay | 재연결 대기 시간 (초) | 5 |
| Max Reconnect Attempts | 최대 재연결 시도 횟수 | 3 |
| Enable Logging | 디버그 로그 출력 | true |

---

## 9. 전체 예제

```csharp
using UniversalChat.Core;
using UnityEngine;
using UnityEngine.UI;
using System.Collections.Generic;

public class SimpleChatUI : MonoBehaviour
{
    [SerializeField] private InputField messageInput;
    [SerializeField] private Button sendButton;
    [SerializeField] private Text chatLog;
    [SerializeField] private string userId = "TestPlayer";

    private List<string> messages = new List<string>();

    async void Start()
    {
        var chat = ChatManager.Instance;

        // 이벤트 구독
        chat.OnConnected += () => AddSystemMessage("서버 연결됨");
        chat.OnDisconnected += (reason) => AddSystemMessage($"연결 끊김: {reason}");
        chat.OnAuthenticated += OnAuthenticated;
        chat.OnMessageReceived += OnMessageReceived;
        chat.OnChannelJoined += (id, name) => AddSystemMessage($"[{name}] 채널에 입장했습니다.");
        chat.OnError += (error) => AddSystemMessage($"[에러] {error}");

        // 버튼 이벤트
        sendButton.onClick.AddListener(OnSendButtonClicked);

        // 서버 연결 및 로그인
        await chat.ConnectAndLoginAsync(userId);
    }

    async void OnAuthenticated(bool success, string message)
    {
        if (success)
        {
            AddSystemMessage("로그인 성공!");
            // 기본 채널 입장
            await ChatManager.Instance.JoinChannelAsync("world");
        }
        else
        {
            AddSystemMessage($"로그인 실패: {message}");
        }
    }

    void OnMessageReceived(ChannelMessage msg)
    {
        AddChatMessage($"{msg.SenderNickname}: {msg.Content}");
    }

    async void OnSendButtonClicked()
    {
        if (string.IsNullOrWhiteSpace(messageInput.text)) return;

        await ChatManager.Instance.SendMessageAsync(messageInput.text);
        messageInput.text = "";
        messageInput.Select();
    }

    void AddSystemMessage(string text)
    {
        AddChatMessage($"<color=yellow>[시스템] {text}</color>");
    }

    void AddChatMessage(string text)
    {
        messages.Add(text);
        if (messages.Count > 100) messages.RemoveAt(0); // 최대 100개 유지
        chatLog.text = string.Join("\n", messages);
    }

    void OnDestroy()
    {
        ChatManager.Instance?.Disconnect();
    }
}
```

---

## 10. 문제 해결

### 연결이 안 되는 경우

1. 서버가 실행 중인지 확인
2. 방화벽에서 포트 7777 허용 여부 확인
3. 서버 주소가 올바른지 확인 (localhost vs 실제 IP)

### Protobuf 관련 오류

```
The type or namespace name 'Google' could not be found
```

→ Google.Protobuf.dll이 설치되지 않음. [2. 필수 의존성 설치](#2-필수-의존성-설치) 참고

### Assembly Definition 충돌

기존 프로젝트에 Assembly Definition이 있는 경우:
1. `UniversalChat.Runtime.asmdef`에서 필요한 참조 추가
2. 또는 `.asmdef` 파일을 삭제하고 전역 스크립트로 사용

---

## 11. 파일 구조

```
Assets/Plugins/UniversalChat/
├── Runtime/
│   ├── Core/
│   │   ├── ChatClient.cs        # 저수준 TCP 클라이언트
│   │   └── ChatManager.cs       # Unity MonoBehaviour 래퍼
│   ├── Network/
│   │   ├── ChatConnection.cs    # TCP 소켓 연결 관리
│   │   └── MainThreadDispatcher.cs  # 메인 스레드 콜백
│   ├── Protocol/
│   │   ├── Chat.cs              # Protobuf 생성 클래스
│   │   ├── PacketType.cs        # 패킷 타입 enum
│   │   ├── PacketHeader.cs      # 패킷 헤더 구조체
│   │   └── PacketSerializer.cs  # Protobuf 직렬화/역직렬화
│   ├── UI/
│   │   ├── Components/          # UI 컴포넌트
│   │   └── Themes/              # UI 설정
│   └── UniversalChat.Runtime.asmdef
├── Editor/
│   ├── ChatUIManagerEditor.cs   # 커스텀 인스펙터
│   └── UniversalChat.Editor.asmdef
├── Prefabs/                     # 프리팹 (선택)
├── Resources/                   # 리소스 (선택)
└── package.json
```

---

## 12. 서버 실행 (테스트용)

```bash
# 서버 빌드 (이미 빌드된 경우 생략)
cd C:/repo/UniversalChatServer
mkdir build && cd build
cmake ..
cmake --build . --config Release

# 서버 실행
./Release/ChatServer.exe
# 또는
./bin/Release/ChatServer.exe
```

서버가 포트 7777에서 실행됩니다.
