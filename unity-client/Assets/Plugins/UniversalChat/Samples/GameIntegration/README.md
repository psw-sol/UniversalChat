# Rich Content System 사용 가이드

UniversalChat의 Rich Content 시스템을 사용하면 채팅 메시지에 클릭 가능한 링크(아이템, 유저 등)를 추가할 수 있습니다.

## 목차

1. [개요](#개요)
2. [빠른 시작](#빠른-시작)
3. [아키텍처](#아키텍처)
4. [태그 형식](#태그-형식)
5. [Provider 구현](#provider-구현)
6. [Handler 구현](#handler-구현)
7. [PopupFactory 구현](#popupfactory-구현)
8. [설정 (RichContentConfig)](#설정-richcontentconfig)
9. [고급 사용법](#고급-사용법)
10. [문제 해결](#문제-해결)

---

## 개요

Rich Content 시스템은 채팅 메시지 내 특수 태그를 파싱하여 클릭 가능한 링크로 변환합니다.

### 주요 기능

- **아이템 링크**: `[ITEM:1001:5]` → "[전설의 검 +5]" (클릭 시 아이템 정보 표시)
- **유저 링크**: `[USER:user123:홍길동]` → "홍길동" (클릭 시 프로필/귓속말)
- **커스텀 링크**: 게임에 맞는 커스텀 타입 추가 가능
- **롱프레스 지원**: 모바일 환경에서 길게 누르기 이벤트 지원

### 설계 원칙

- **서버 검증 불필요**: 클라이언트에서 표시 목적으로만 사용
- **확장 가능**: Provider/Handler 패턴으로 게임별 커스터마이징
- **성능 최적화**: TextMeshPro 네이티브 링크 기능 활용

---

## 빠른 시작

### 1. 에디터에서 UI 추가

```
Unity Editor → UniversalChat → Add Chat UI to Scene
```

이 메뉴는 자동으로:
- Canvas에 Chat UI 추가
- RichContentManager 생성
- RichContentConfig 연결

### 2. Provider/Handler 등록

게임 시작 시 Provider와 Handler를 등록합니다:

```csharp
using UnityEngine;
using UniversalChat.RichContent;

public class GameChatSetup : MonoBehaviour
{
    void Start()
    {
        var manager = RichContentManager.Instance;

        // Provider 등록 (링크 데이터 → 표시 텍스트)
        manager.RegisterProvider(new MyItemDataProvider());
        manager.RegisterProvider(new MyUserDataProvider());

        // Handler 등록 (링크 클릭 시 동작)
        manager.RegisterHandler(new MyItemLinkHandler());
        manager.RegisterHandler(new MyUserLinkHandler());
    }
}
```

### 3. 채팅에서 링크 사용

```csharp
// 아이템 링크 포함 메시지 전송
string itemTag = RichTextParser.CreateTag("ITEM", "1001", "5");
await chatUI.SendMessageAsync($"이 아이템 어때요? {itemTag}");
// 결과: "이 아이템 어때요? [ITEM:1001:5]"
// 표시: "이 아이템 어때요? [전설의 검 +5]"

// 유저 멘션
string userTag = RichTextParser.CreateTag("USER", "user123", "홍길동");
await chatUI.SendMessageAsync($"{userTag}님 안녕하세요!");
```

---

## 아키텍처

```
채팅 메시지 입력
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                    RichTextParser                            │
│  "[ITEM:1001:5]" → RichLinkData { Type="ITEM", Params=...}  │
└─────────────────────────────────────────────────────────────┘
       │
       ▼
┌─────────────────────────────────────────────────────────────┐
│                  RichContentManager                          │
│  ┌─────────────────────┐  ┌─────────────────────┐          │
│  │ IRichContentData    │  │ IRichContentLink    │          │
│  │ Provider (ITEM)     │  │ Handler (ITEM)      │          │
│  │                     │  │                     │          │
│  │ GetDisplayText()    │  │ OnLinkClicked()     │          │
│  │ GetLinkColor()      │  │ OnLinkLongPressed() │          │
│  └─────────────────────┘  └─────────────────────┘          │
└─────────────────────────────────────────────────────────────┘
       │                              │
       ▼                              ▼
┌──────────────────┐          ┌──────────────────┐
│   RichChatText   │          │  IRichContent    │
│   (TMP 표시)     │          │  PopupFactory    │
│                  │          │  (팝업 생성)      │
│  <link="...">    │          │                  │
│  [전설의 검 +5]  │          │  ShowItemPopup() │
│  </link>         │          │                  │
└──────────────────┘          └──────────────────┘
```

### 핵심 컴포넌트

| 컴포넌트 | 역할 |
|---------|------|
| `RichTextParser` | 태그 파싱 및 TMP 링크 변환 |
| `RichContentManager` | Provider/Handler 등록 및 관리 |
| `IRichContentDataProvider` | 링크 데이터 → 표시 텍스트/색상 |
| `IRichContentLinkHandler` | 클릭/롱프레스 이벤트 처리 |
| `IRichContentPopupFactory` | 팝업 UI 생성 |
| `RichChatText` | TMP 링크 클릭 감지 |
| `RichContentConfig` | 색상 및 설정 |

---

## 태그 형식

### 기본 형식

```
[TYPE:param1:param2:param3:...]
```

- **TYPE**: 대문자와 언더스코어 (예: `ITEM`, `USER`, `GUILD_MEMBER`)
- **params**: 콜론으로 구분된 파라미터들

### 예시

| 태그 | 설명 |
|------|------|
| `[ITEM:1001]` | 아이템 ID 1001 |
| `[ITEM:1001:5]` | 아이템 ID 1001, 강화 +5 |
| `[USER:abc123:홍길동]` | 유저 ID abc123, 표시명 홍길동 |
| `[QUEST:quest_001]` | 퀘스트 링크 |
| `[LOCATION:100:200:던전입구]` | 좌표 및 이름 |

### 태그 생성 헬퍼

```csharp
using UniversalChat.RichContent;

// 단일 파라미터
string tag1 = RichTextParser.CreateTag("ITEM", "1001");
// 결과: "[ITEM:1001]"

// 다중 파라미터
string tag2 = RichTextParser.CreateTag("ITEM", "1001", "5");
// 결과: "[ITEM:1001:5]"

// 배열로 전달
string tag3 = RichTextParser.CreateTag("USER", new[] { "user123", "홍길동" });
// 결과: "[USER:user123:홍길동]"
```

---

## Provider 구현

Provider는 링크 데이터를 표시 텍스트와 색상으로 변환합니다.

### 기본 구조

```csharp
using UnityEngine;
using UniversalChat.RichContent;

public class MyItemDataProvider : IRichContentDataProvider
{
    // 처리할 링크 타입
    public string LinkType => "ITEM";

    // 표시 텍스트 반환
    public string GetDisplayText(RichLinkData linkData)
    {
        string itemId = linkData.Param1;
        int enhancement = linkData.GetParamAsInt(1, 0);

        // 실제 게임: 아이템 DB에서 이름 조회
        string itemName = ItemDatabase.GetItem(itemId)?.Name ?? "알 수 없는 아이템";

        if (enhancement > 0)
            return $"[{itemName} +{enhancement}]";
        return $"[{itemName}]";
    }

    // 링크 색상 반환 (null이면 기본색 사용)
    public Color? GetLinkColor(RichLinkData linkData)
    {
        string itemId = linkData.Param1;
        var item = ItemDatabase.GetItem(itemId);

        if (item == null) return null;

        // 등급별 색상
        return item.Rarity switch
        {
            ItemRarity.Legendary => new Color(1f, 0.5f, 0f),    // 오렌지
            ItemRarity.Epic => new Color(0.6f, 0.2f, 0.8f),     // 보라
            ItemRarity.Rare => new Color(0.2f, 0.5f, 1f),       // 파랑
            ItemRarity.Uncommon => new Color(0.2f, 0.8f, 0.2f), // 녹색
            _ => null  // 기본색
        };
    }
}
```

### RichLinkData 접근

```csharp
public string GetDisplayText(RichLinkData linkData)
{
    // 타입
    string type = linkData.Type;  // "ITEM"

    // 파라미터 접근 (인덱스)
    string param1 = linkData.Param1;  // 첫 번째 파라미터
    string param2 = linkData.Param2;  // 두 번째 파라미터
    string param3 = linkData.Param3;  // 세 번째 파라미터

    // 안전한 파라미터 접근
    string value = linkData.GetParam(0);  // 인덱스로 접근
    string valueOrDefault = linkData.GetParam(5, "default");  // 기본값 지정

    // 숫자 변환
    int intValue = linkData.GetParamAsInt(1, 0);     // int로 변환
    long longValue = linkData.GetParamAsLong(1, 0L); // long으로 변환

    // 원본 태그
    string original = linkData.OriginalTag;  // "[ITEM:1001:5]"

    return "...";
}
```

### Config 색상 사용

```csharp
public Color? GetLinkColor(RichLinkData linkData)
{
    var config = RichContentManager.Instance?.Config;
    if (config == null) return null;

    // Config에 정의된 등급 색상 사용
    int rarity = GetItemRarity(linkData.Param1);
    return config.GetRarityColor(rarity);
}
```

---

## Handler 구현

Handler는 링크 클릭 및 롱프레스 이벤트를 처리합니다.

### 기본 구조

```csharp
using UnityEngine;
using UniversalChat.RichContent;

public class MyItemLinkHandler : IRichContentLinkHandler
{
    // 처리할 링크 타입
    public string LinkType => "ITEM";

    // 클릭 이벤트
    public void OnLinkClicked(RichLinkData linkData)
    {
        string itemId = linkData.Param1;
        int enhancement = linkData.GetParamAsInt(1, 0);

        Debug.Log($"아이템 클릭: {itemId}, 강화: +{enhancement}");

        // 방법 1: UI 매니저로 팝업 표시
        GameUIManager.Instance.ShowItemInfoPopup(itemId, enhancement);

        // 방법 2: RichContent PopupFactory 사용
        // RichContentManager.Instance.ShowPopup("ITEM", linkData, Input.mousePosition);

        // 방법 3: 이벤트 발행
        // EventBus.Publish(new ItemLinkClickedEvent(itemId, enhancement));
    }

    // 롱프레스 이벤트 (선택 구현 - 기본 구현은 빈 메서드)
    public void OnLinkLongPressed(RichLinkData linkData)
    {
        string itemId = linkData.Param1;

        // 빠른 액션 메뉴 표시
        GameUIManager.Instance.ShowItemQuickMenu(itemId);
    }
}
```

### 유저 링크 Handler 예시

```csharp
public class MyUserLinkHandler : IRichContentLinkHandler
{
    public string LinkType => "USER";

    public void OnLinkClicked(RichLinkData linkData)
    {
        string userId = linkData.Param1;
        string displayName = linkData.Param2;

        // 유저 액션 메뉴 표시
        var options = new List<UserActionOption>
        {
            new("프로필 보기", () => ShowProfile(userId)),
            new("귓속말", () => StartWhisper(userId, displayName)),
            new("친구 추가", () => AddFriend(userId)),
            new("차단", () => BlockUser(userId))
        };

        GameUIManager.Instance.ShowActionMenu(options);
    }

    private void ShowProfile(string userId) { /* ... */ }
    private void StartWhisper(string userId, string displayName) { /* ... */ }
    private void AddFriend(string userId) { /* ... */ }
    private void BlockUser(string userId) { /* ... */ }
}
```

---

## PopupFactory 구현

PopupFactory는 링크 클릭 시 표시할 팝업을 생성합니다.

### 기본 구조

```csharp
using UnityEngine;
using UniversalChat.RichContent;

public class MyPopupFactory : IRichContentPopupFactory
{
    // 지원하는 링크 타입들
    public string[] SupportedTypes => new[] { "ITEM", "USER" };

    // 팝업 표시
    public void ShowPopup(RichLinkData linkData, Vector2 screenPosition)
    {
        switch (linkData.Type)
        {
            case "ITEM":
                ShowItemPopup(linkData, screenPosition);
                break;
            case "USER":
                ShowUserPopup(linkData, screenPosition);
                break;
        }
    }

    private void ShowItemPopup(RichLinkData linkData, Vector2 position)
    {
        string itemId = linkData.Param1;
        int enhancement = linkData.GetParamAsInt(1, 0);

        // 아이템 정보 팝업 생성
        var popup = Instantiate(itemPopupPrefab);
        popup.transform.position = position;
        popup.Setup(itemId, enhancement);
    }

    private void ShowUserPopup(RichLinkData linkData, Vector2 position)
    {
        string userId = linkData.Param1;

        // 유저 미니 프로필 팝업 생성
        var popup = Instantiate(userPopupPrefab);
        popup.transform.position = position;
        popup.Setup(userId);
    }
}
```

### PopupFactory 등록

```csharp
void Start()
{
    var manager = RichContentManager.Instance;

    // PopupFactory 등록
    manager.RegisterPopupFactory(new MyPopupFactory());
}
```

### Handler에서 PopupFactory 사용

```csharp
public void OnLinkClicked(RichLinkData linkData)
{
    // PopupFactory를 통해 팝업 표시
    RichContentManager.Instance.ShowPopup(
        linkData.Type,
        linkData,
        Input.mousePosition
    );
}
```

---

## 설정 (RichContentConfig)

### Config 생성

```
Unity Editor → Assets → Create → UniversalChat → Rich Content Config
```

또는

```
Unity Editor → UniversalChat → Create Rich Content Config
```

### 설정 항목

#### 기본 링크 색상

| 항목 | 설명 | 기본값 |
|------|------|--------|
| Default Link Color | 기본 링크 색상 | #4A90D9 (파랑) |
| Item Link Color | 아이템 링크 기본색 | #FFD700 (금색) |
| User Link Color | 유저 링크 색상 | #00BFFF (하늘색) |
| System Link Color | 시스템 링크 색상 | #98FB98 (연녹색) |

#### 등급별 색상

| 등급 | 기본 색상 |
|------|----------|
| Rarity 0 (Common) | #FFFFFF (흰색) |
| Rarity 1 (Uncommon) | #1EFF00 (녹색) |
| Rarity 2 (Rare) | #0070DD (파랑) |
| Rarity 3 (Epic) | #A335EE (보라) |
| Rarity 4 (Legendary) | #FF8000 (오렌지) |

#### 동작 설정

| 항목 | 설명 | 기본값 |
|------|------|--------|
| Long Press Duration | 롱프레스 인식 시간 (초) | 0.5 |
| Enable Long Press | 롱프레스 기능 활성화 | true |
| Link Underline | 링크에 밑줄 표시 | false |

### 코드에서 Config 접근

```csharp
var config = RichContentManager.Instance.Config;

// 등급 색상 가져오기
Color legendaryColor = config.GetRarityColor(4);

// 링크 색상 가져오기
Color itemColor = config.ItemLinkColor;
Color userColor = config.UserLinkColor;

// 롱프레스 시간
float longPressDuration = config.LongPressDuration;
```

---

## 고급 사용법

### 닉네임 자동 링크 변환

`RichChatMessageItem`은 닉네임을 자동으로 USER 링크로 변환합니다:

```csharp
// 내부 구현
public void SetData(ChannelMessage message, ChatUIConfig config)
{
    // 닉네임을 클릭 가능한 유저 링크로 변환
    string userTag = RichTextParser.CreateTag("USER", message.SenderId, message.SenderNickname);
    _nicknameText.SetRawText(userTag);

    // 메시지 내용도 Rich Content 처리
    _messageText.SetRawText(message.Content);
}
```

### 커스텀 링크 타입 추가

새로운 링크 타입을 추가하려면 Provider와 Handler만 구현하면 됩니다:

```csharp
// 길드 링크 Provider
public class GuildDataProvider : IRichContentDataProvider
{
    public string LinkType => "GUILD";

    public string GetDisplayText(RichLinkData linkData)
    {
        string guildId = linkData.Param1;
        var guild = GuildManager.Instance.GetGuild(guildId);
        return $"<{guild?.Name ?? "알 수 없는 길드"}>";
    }

    public Color? GetLinkColor(RichLinkData linkData)
    {
        return new Color(0.8f, 0.6f, 0.2f); // 갈색
    }
}

// 길드 링크 Handler
public class GuildLinkHandler : IRichContentLinkHandler
{
    public string LinkType => "GUILD";

    public void OnLinkClicked(RichLinkData linkData)
    {
        string guildId = linkData.Param1;
        GuildUIManager.Instance.ShowGuildInfo(guildId);
    }
}

// 등록
void Start()
{
    var manager = RichContentManager.Instance;
    manager.RegisterProvider(new GuildDataProvider());
    manager.RegisterHandler(new GuildLinkHandler());
}
```

### 링크 입력 도우미

`RichLinkInputHelper`를 사용하면 버튼 클릭으로 링크를 입력할 수 있습니다:

```csharp
// Inspector에서 설정
[SerializeField] private ChatInputField inputField;
[SerializeField] private Button itemLinkButton;

void Start()
{
    var helper = itemLinkButton.gameObject.AddComponent<RichLinkInputHelper>();
    helper.Initialize(inputField);
    helper.SetLinkInfo("ITEM", GetSelectedItemId);

    itemLinkButton.onClick.AddListener(() => helper.InsertLink());
}

private string[] GetSelectedItemId()
{
    var selectedItem = InventoryUI.SelectedItem;
    if (selectedItem == null) return null;

    return new[] { selectedItem.Id, selectedItem.Enhancement.ToString() };
}
```

### 수동 파싱 및 변환

```csharp
using UniversalChat.RichContent;

// 태그 파싱만 수행
List<RichLinkData> links = RichTextParser.ParseLinks("[ITEM:1001:5] 팝니다!");
foreach (var link in links)
{
    Debug.Log($"Type: {link.Type}, Param1: {link.Param1}");
}

// TMP 리치 텍스트로 변환
string tmpText = RichTextParser.ConvertToTMPRichText(
    "[ITEM:1001:5] 팝니다!",
    RichContentManager.Instance
);
// 결과: "<link="BASE64_ENCODED_DATA"><color=#FFD700>[전설의 검 +5]</color></link> 팝니다!"
```

### 가상화 스크롤에서 사용

대량의 메시지를 처리할 때는 `VirtualizedChatPanel`과 `RichChatMessageView`를 사용합니다:

```csharp
// ChatUIManager에서 자동 처리됨
// _useVirtualizedScroll = true 설정 시 자동으로 RichChatMessageView 사용

// 또는 수동으로 빌드
var virtualizedPanel = ChatUIBuilder.BuildVirtualizedChatPanel(parent, config);
```

---

## 문제 해결

### "링크가 클릭되지 않음"

1. **RichContentManager 확인**
   ```csharp
   if (RichContentManager.Instance == null)
   {
       Debug.LogError("RichContentManager가 없습니다!");
   }
   ```

2. **Handler 등록 확인**
   ```csharp
   // 등록된 핸들러 확인
   var handler = RichContentManager.Instance.GetHandler("ITEM");
   if (handler == null)
   {
       Debug.LogError("ITEM 핸들러가 등록되지 않았습니다!");
   }
   ```

3. **RichChatText 컴포넌트 확인**
   - Text 오브젝트에 `RichChatText` 컴포넌트가 있는지 확인
   - `Raycast Target`이 활성화되어 있는지 확인

### "표시 텍스트가 변환되지 않음"

1. **Provider 등록 확인**
   ```csharp
   var provider = RichContentManager.Instance.GetProvider("ITEM");
   if (provider == null)
   {
       Debug.LogError("ITEM 프로바이더가 등록되지 않았습니다!");
   }
   ```

2. **태그 형식 확인**
   - 대괄호 `[]` 사용
   - 타입은 대문자: `ITEM`, `USER`
   - 파라미터는 콜론 `:` 으로 구분

3. **SetRawText 사용 확인**
   ```csharp
   // ❌ 잘못된 방법
   tmpText.text = "[ITEM:1001]";

   // ✅ 올바른 방법
   richChatText.SetRawText("[ITEM:1001]");
   ```

### "색상이 적용되지 않음"

1. **Config 연결 확인**
   - RichContentManager의 Config 필드가 설정되어 있는지 확인

2. **Provider의 GetLinkColor 반환값 확인**
   - `null` 반환 시 기본 색상 사용
   - `Color` 반환 시 해당 색상 적용

### "롱프레스가 작동하지 않음"

1. **Config 설정 확인**
   - `Enable Long Press`가 `true`인지 확인
   - `Long Press Duration` 값 확인 (기본 0.5초)

2. **Handler 구현 확인**
   - `OnLinkLongPressed` 메서드가 구현되어 있는지 확인

### "에디터에서 메뉴가 보이지 않음"

1. **Editor 폴더 확인**
   - `RichContentEditorMenu.cs`가 `Editor` 폴더에 있는지 확인

2. **컴파일 에러 확인**
   - Console에 컴파일 에러가 없는지 확인

---

## 샘플 파일

`SampleRichContentSetup.cs` 파일에서 전체 구현 예시를 확인할 수 있습니다:

- `SampleItemDataProvider` - 아이템 Provider 예시
- `SampleUserDataProvider` - 유저 Provider 예시
- `SampleItemLinkHandler` - 아이템 Handler 예시
- `SampleUserLinkHandler` - 유저 Handler 예시

```csharp
// 게임 시작 시 샘플 Provider/Handler 등록
var setup = gameObject.AddComponent<SampleRichContentSetup>();
// 또는 Inspector에서 _setupOnStart = true 설정
```

---

## 관련 파일

| 파일 | 위치 | 설명 |
|------|------|------|
| RichLinkData.cs | Runtime/RichContent/ | 링크 데이터 구조 |
| IRichContentDataProvider.cs | Runtime/RichContent/ | Provider 인터페이스 |
| IRichContentLinkHandler.cs | Runtime/RichContent/ | Handler 인터페이스 |
| IRichContentPopupFactory.cs | Runtime/RichContent/ | PopupFactory 인터페이스 |
| RichTextParser.cs | Runtime/RichContent/ | 태그 파서 |
| RichContentManager.cs | Runtime/RichContent/ | 매니저 싱글톤 |
| RichContentConfig.cs | Runtime/RichContent/ | 설정 ScriptableObject |
| RichChatText.cs | Runtime/UI/Components/ | TMP 링크 컴포넌트 |
| RichChatMessageItem.cs | Runtime/UI/Components/ | 메시지 아이템 |
| RichChatMessageView.cs | Runtime/UI/Components/ | 가상화 메시지 뷰 |
| SampleRichContentSetup.cs | Samples/GameIntegration/ | 샘플 구현 |
