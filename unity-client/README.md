# Universal Chat Unity Client

UniversalChatServer와 통신하는 Unity 채팅 클라이언트 에셋입니다.

## 특징

- **Plug & Play**: Inspector에서 간단히 설정 가능
- **커스터마이징 가능한 UI**: ScriptableObject 기반 테마 시스템
- **이벤트 기반 아키텍처**: UnityEvent로 게임 로직과 쉽게 통합
- **자동 재연결**: 연결 끊김 시 자동으로 재연결 시도
- **오브젝트 풀링**: 메시지 아이템에 오브젝트 풀링 적용

## 설치

### Unity Package Manager (권장)

1. Window → Package Manager 열기
2. "+" 버튼 클릭 → "Add package from disk..."
3. `unity-client/Assets/Plugins/UniversalChat/package.json` 선택

### 수동 설치

`Assets/Plugins/UniversalChat` 폴더를 프로젝트로 복사합니다.

## 빠른 시작

### 1. ChatUIManager 컴포넌트 추가

```csharp
// 씬에 빈 GameObject 생성 후 ChatUIManager 컴포넌트 추가
// Inspector에서 서버 주소 설정
```

### 2. 코드에서 사용

```csharp
using UniversalChat.Core;
using UniversalChat.UI;

public class GameChatIntegration : MonoBehaviour
{
    [SerializeField] private ChatUIManager _chatUI;

    private async void Start()
    {
        // 이벤트 구독
        _chatUI.OnMessageReceivedEvent.AddListener(OnMessageReceived);
        _chatUI.OnConnectedEvent.AddListener(OnConnected);

        // 연결
        await _chatUI.ConnectAsync("localhost", 7777);

        // 로그인
        await _chatUI.LoginAsync("player123");

        // 채널 입장
        await _chatUI.JoinChannelAsync("general");
    }

    private void OnConnected()
    {
        Debug.Log("채팅 서버 연결됨!");
    }

    private void OnMessageReceived(ChannelMessage message)
    {
        Debug.Log($"메시지 수신: {message.Content}");
    }
}
```

### 3. Inspector에서 설정

ChatUIManager 컴포넌트의 Inspector에서:

- **Server Host**: 서버 주소 (기본: localhost)
- **Server Port**: 서버 포트 (기본: 7777)
- **Connect On Start**: 시작 시 자동 연결
- **Auto Login User ID**: 자동 로그인 사용자 ID

## 폴더 구조

```
Assets/Plugins/UniversalChat/
├── Runtime/
│   ├── Core/
│   │   ├── ChatClient.cs        # 저수준 채팅 클라이언트
│   │   └── ChatManager.cs       # MonoBehaviour 래퍼
│   ├── Network/
│   │   ├── ChatConnection.cs    # TCP 연결 관리
│   │   └── MainThreadDispatcher.cs
│   ├── Protocol/
│   │   ├── PacketType.cs        # 패킷 타입 정의
│   │   ├── PacketHeader.cs      # 패킷 헤더 구조
│   │   └── PacketSerializer.cs  # 직렬화/역직렬화
│   └── UI/
│       ├── Components/
│       │   ├── ChatUIManager.cs     # 메인 UI 관리자
│       │   ├── ChatPanel.cs         # 메시지 표시 패널
│       │   ├── ChatMessageItem.cs   # 메시지 아이템
│       │   ├── ChatInputField.cs    # 입력 필드
│       │   └── ChannelListPanel.cs  # 채널 목록
│       └── Themes/
│           └── ChatUIConfig.cs      # UI 설정 SO
├── Editor/
│   └── ChatUIManagerEditor.cs   # 커스텀 인스펙터
├── Resources/
├── Prefabs/
└── package.json
```

## UI 커스터마이징

### ChatUIConfig 생성

1. Project 창에서 우클릭
2. Create → UniversalChat → UI Config
3. ChatUIManager의 UI Config 필드에 할당

### 설정 가능한 항목

- 색상 (배경, 패널, 버튼, 텍스트 등)
- 폰트 및 크기
- 레이아웃 (간격, 패딩, 최대 너비)
- 동작 (타임스탬프 표시, 자동 스크롤 등)
- 사운드 효과

## 이벤트

| 이벤트 | 설명 |
|--------|------|
| OnConnectedEvent | 서버 연결 성공 |
| OnDisconnectedEvent | 연결 끊김 (사유 포함) |
| OnErrorEvent | 에러 발생 |
| OnAuthenticatedEvent | 인증 결과 |
| OnMessageReceivedEvent | 메시지 수신 |
| OnChannelJoinedEvent | 채널 입장 |
| OnChannelLeftEvent | 채널 퇴장 |

## 요구 사항

- Unity 2020.3 이상
- .NET Standard 2.1 또는 .NET 4.x

## 서버 호환성

이 클라이언트는 UniversalChatServer와 호환됩니다:

- 프로토콜: TCP (Port 7777)
- 패킷 형식: 8바이트 헤더 + JSON 바디
- 인증: 사용자 ID + 선택적 비밀번호

## 라이선스

MIT License
