# UniversalChat UI

UniversalChat UI 사용 가이드입니다.

## 런타임 UI 생성 (권장)

UniversalChat은 프리팹 없이 런타임에 UI를 동적으로 생성합니다.

### 방법 1: 에디터 메뉴 사용

#### 샘플 씬 생성
```
UniversalChat → Create Sample Scene
```
완전한 채팅 UI가 포함된 새 씬이 생성됩니다.

#### 현재 씬에 UI 추가
```
UniversalChat → Add Chat UI to Scene
```
현재 씬에 채팅 UI가 추가됩니다 (우측 하단).

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

        // 위치 설정
        var rect = chatUI.GetComponent<RectTransform>();
        rect.anchorMin = new Vector2(1, 0);
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
        await chatUI.LoginAsync("user123");
        await chatUI.JoinChannelAsync("lobby");
    }
}
```

## UI 설정 (ChatUIConfig)

### Config 생성
```
Assets → Create → UniversalChat → UI Config
```

### 주요 설정 항목

| 카테고리 | 항목 | 설명 |
|----------|------|------|
| **배경색** | Background Color | 전체 배경색 |
| | Panel Background Color | 메시지 패널 배경색 |
| | Input Background Color | 입력 필드 배경색 |
| **버튼** | Send Button Color | 전송 버튼 색 |
| | Send Button Text | 전송 버튼 텍스트 ("Send") |
| **헤더** | Title Color | 제목 색 |
| | Connected Color | 연결됨 상태 색 (녹색) |
| | Disconnected Color | 연결 끊김 상태 색 (빨간색) |
| **메시지** | Message Color | 기본 메시지 색 |
| | Nickname Color | 닉네임 색 |
| | Timestamp Color | 시간 표시 색 |
| | System Text Color | 시스템 메시지 색 |
| **말풍선** | My Bubble Color | 내 메시지 말풍선 색 |
| | Other Bubble Color | 다른 사람 메시지 말풍선 색 |
| | System Bubble Color | 시스템 메시지 말풍선 색 |
| **폰트** | Message Font Size | 메시지 글자 크기 (14) |
| | Nickname Font Size | 닉네임 글자 크기 (12) |
| | Timestamp Font Size | 시간 글자 크기 (10) |
| **입력** | Placeholder Text | 입력창 placeholder ("Type a message...") |
| | Max Message Length | 최대 메시지 길이 (500) |
| **동작** | Max Visible Messages | 화면에 표시할 최대 메시지 수 (100) |
| | Show Timestamps | 시간 표시 여부 |
| | Auto Scroll To Bottom | 자동 스크롤 |

## 코드에서 사용하기

### 기본 사용

```csharp
using UniversalChat.UI;

public class MyChatController : MonoBehaviour
{
    [SerializeField] private ChatUIManager chatUI;

    async void Start()
    {
        // 서버 연결
        await chatUI.ConnectAsync("localhost", 7777);

        // 로그인
        await chatUI.LoginAsync("user123");

        // 채널 입장
        await chatUI.JoinChannelAsync("lobby");
    }

    public async void SendMessage(string text)
    {
        await chatUI.SendMessageAsync(text);
    }

    public void AddNotice(string text)
    {
        chatUI.AddSystemMessage(text);
    }
}
```

### 이벤트 구독

```csharp
// Inspector에서 UnityEvent 연결 또는 코드에서 구독
chatUI.OnConnectedEvent.AddListener(OnConnected);
chatUI.OnDisconnectedEvent.AddListener(OnDisconnected);
chatUI.OnMessageReceivedEvent.AddListener(OnMessage);
chatUI.OnErrorEvent.AddListener(OnError);

void OnConnected()
{
    Debug.Log("서버 연결됨");
}

void OnDisconnected(string reason)
{
    Debug.Log($"연결 끊김: {reason}");
}

void OnMessage(ChannelMessage msg)
{
    Debug.Log($"[{msg.SenderNickname}] {msg.Content}");
}

void OnError(string error)
{
    Debug.LogError($"에러: {error}");
}
```

## UI 구조

```
Chat UI (ChatUIManager)
├── Header
│   ├── Title (Text) - "Chat"
│   └── Status (Text) - 연결 상태
├── Chat Panel (ChatPanel, ScrollRect)
│   └── Viewport
│       └── Content (VerticalLayoutGroup)
│           └── [Chat Message Item] - 동적 생성
└── Input Area (ChatInputField)
    ├── Input Background (InputField)
    │   └── Text Area
    │       ├── Placeholder
    │       └── Text
    └── Send Button
```

## 다른 프로젝트 UI 프레임워크 통합

### Doozy UI 통합 예시 (RentaHero)

```csharp
using PopupEx;
using UI.Popup.Common;
using UniversalChat.Core;

public class ChatPopup : UIPopupCommon
{
    // Doozy Popup 패턴으로 구현
    // Assets/0_InGame/Scripts/Chat/UI/ChatPopup.cs 참조
}
```

### 커스텀 UI 빌더 확장

```csharp
using UniversalChat.UI;

public class MyChatUI : MonoBehaviour
{
    [SerializeField] private ChatUIConfig config;

    void Start()
    {
        // 메시지 아이템만 생성
        var messageItem = ChatUIBuilder.BuildMessageItem(transform, config);

        // 또는 전체 UI 생성
        var chatUI = ChatUIBuilder.Build(transform, config);
    }
}
```

## 문제 해결

### "서버 연결 실패"
- 서버 IP/Port 확인
- 방화벽 설정 확인
- ChatManager가 씬에 있는지 확인 (없으면 자동 생성됨)

### "메시지가 표시되지 않음"
- ChatUIConfig가 연결되어 있는지 확인
- ChatPanel의 Content RectTransform 확인
- 스크롤뷰 설정 확인

### "내 메시지와 다른 메시지 구분이 안됨"
- 로그인 후 ChatPanel에 userId가 설정되는지 확인
- ChatUIManager.LoginAsync() 호출 확인

---

## Rich Content 시스템

채팅 메시지에 클릭 가능한 링크(아이템, 유저 등)를 추가할 수 있습니다.

### 빠른 시작

```
Unity Editor → UniversalChat → Add Chat UI to Scene
```

이 메뉴는 Rich Content 시스템이 포함된 Chat UI를 추가합니다.

### 태그 형식

```
[TYPE:param1:param2:...]
```

예시:
- `[ITEM:1001:5]` → "[전설의 검 +5]" (클릭 가능)
- `[USER:user123:홍길동]` → "홍길동" (클릭 가능)

### Provider/Handler 등록

```csharp
using UniversalChat.RichContent;

void Start()
{
    var manager = RichContentManager.Instance;

    // Provider: 링크 데이터 → 표시 텍스트
    manager.RegisterProvider(new MyItemDataProvider());

    // Handler: 클릭 이벤트 처리
    manager.RegisterHandler(new MyItemLinkHandler());
}
```

### Provider 구현 예시

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

### Handler 구현 예시

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
        // 모바일: 길게 누르기 시 퀵 메뉴
        GameUIManager.Instance.ShowItemQuickMenu(linkData.Param1);
    }
}
```

### 링크 태그 생성

```csharp
// 채팅 메시지에 아이템 링크 포함
string itemTag = RichTextParser.CreateTag("ITEM", "1001", "5");
await chatUI.SendMessageAsync($"이거 어때요? {itemTag}");
// 전송: "이거 어때요? [ITEM:1001:5]"
// 표시: "이거 어때요? [전설의 검 +5]"
```

### 상세 가이드

Rich Content 시스템의 전체 가이드는 다음 파일을 참조하세요:

📖 **[GameIntegration/README.md](GameIntegration/README.md)**

- 아키텍처 설명
- 태그 형식 상세
- Provider/Handler 구현 패턴
- PopupFactory 구현
- RichContentConfig 설정
- 고급 사용법
- 문제 해결
