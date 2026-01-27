# 가상화 채팅 스크롤 시스템 설계서

**작성일**: 2025-01-21
**버전**: 1.0
**상태**: 구현 완료

---

## 1. 개요

### 1.1 배경
현재 ChatPanel은 모든 메시지를 실제 GameObject로 생성하여 관리합니다. 메시지가 100개면 100개의 GameObject가 활성화되어 있어 성능 문제가 발생합니다.

### 1.2 목표
- 화면에 보이는 아이템만 렌더링하는 가상화 스크롤 구현
- 메시지 길이에 따른 동적 높이 지원
- 1000개 이상 메시지에서도 안정적인 성능 유지

### 1.3 범위
- VirtualizedChatPanel 컴포넌트 신규 개발
- ChatMessageView (재활용 가능한 뷰) 컴포넌트 개발
- ChatMessageData (순수 데이터) 클래스 개발
- 기존 ChatPanel과의 호환성 유지

---

## 2. 현재 문제 분석

### 2.1 현재 구조

```
ChatPanel
├── _messageItems: List<ChatMessageItem>  (100개 = 100개 GameObject)
├── _messagePool: Queue<ChatMessageItem>  (비활성화된 아이템)
└── ScrollRect
    └── Content
        ├── ChatMessageItem 0   ← 화면 밖
        ├── ChatMessageItem 1   ← 화면 밖
        ├── ...
        ├── ChatMessageItem 95  ← 보임
        ├── ChatMessageItem 96  ← 보임
        ├── ChatMessageItem 97  ← 보임
        ├── ChatMessageItem 98  ← 보임
        └── ChatMessageItem 99  ← 보임
```

### 2.2 문제점

| 문제 | 설명 | 영향 |
|------|------|------|
| **과다 GameObject** | 100개 메시지 = 100개 활성 GameObject | 메모리 낭비 |
| **Draw Call 증가** | 각 아이템이 별도 배칭 | GPU 부하 |
| **레이아웃 비용** | 메시지 추가 시 전체 재계산 | CPU 스파이크 |
| **고정 높이** | preferredHeight=70 고정 | 긴 메시지 잘림 |

### 2.3 성능 측정 (예상)

| 메시지 수 | 현재 방식 | 가상화 방식 |
|-----------|-----------|-------------|
| 100개 | 100 GameObject, ~17ms 레이아웃 | ~10 GameObject, ~1ms |
| 500개 | 500 GameObject, ~85ms 레이아웃 | ~10 GameObject, ~1ms |
| 1000개 | 1000 GameObject, ~170ms (프레임 드랍) | ~10 GameObject, ~1ms |

---

## 3. 설계 솔루션

### 3.1 아키텍처 개요

```
┌─────────────────────────────────────────────────────────────┐
│                    VirtualizedChatPanel                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              ChatMessageDataList                     │    │
│  │  [Data0, Data1, Data2, ... DataN]                   │    │
│  │  (메시지 데이터만 보관, GameObject 없음)              │    │
│  │  최대 500개 유지, 초과 시 오래된 것 삭제             │    │
│  └─────────────────────────────────────────────────────┘    │
│                           │                                  │
│                           ▼                                  │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              HeightCache (누적 높이)                 │    │
│  │  [60, 140, 260, 320, ...]                           │    │
│  │  이진 탐색으로 O(log n) 위치 계산                    │    │
│  └─────────────────────────────────────────────────────┘    │
│                           │                                  │
│                           ▼                                  │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              ViewPool (~15개)                        │    │
│  │  [View0] [View1] ... [View14]                       │    │
│  │  화면 높이 / 평균 아이템 높이 + 버퍼                 │    │
│  │  재활용되며 데이터만 교체                            │    │
│  └─────────────────────────────────────────────────────┘    │
│                           │                                  │
│                           ▼                                  │
│  ┌─────────────────────────────────────────────────────┐    │
│  │              ScrollRect (Unity UI)                   │    │
│  │  ┌───────────────────────────────────────────────┐  │    │
│  │  │  Content                                      │  │    │
│  │  │  height = Σ(모든 메시지 높이)                 │  │    │
│  │  │                                               │  │    │
│  │  │  ════════════════════════════                 │  │    │
│  │  │    [View5] ← 데이터 95                        │  │    │
│  │  │    [View6] ← 데이터 96      ◄── Viewport     │  │    │
│  │  │    [View7] ← 데이터 97                        │  │    │
│  │  │    [View0] ← 데이터 98                        │  │    │
│  │  │  ════════════════════════════                 │  │    │
│  │  │                                               │  │    │
│  │  └───────────────────────────────────────────────┘  │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### 3.2 핵심 원리

1. **데이터와 뷰 분리**: 메시지 데이터는 리스트로 무제한 보관, 뷰는 화면에 보이는 것만 생성
2. **뷰 재활용**: 스크롤 시 화면 밖으로 나간 뷰를 회수하여 새 데이터에 바인딩
3. **높이 캐싱**: 각 메시지의 높이를 계산 후 캐싱하여 재계산 방지
4. **누적 높이**: 이진 탐색으로 O(log n) 시간에 스크롤 위치 → 데이터 인덱스 변환

---

## 4. 컴포넌트 설계

### 4.1 ChatMessageData (데이터 클래스)

**목적**: 순수 메시지 데이터 보관 (GameObject 없음)

```csharp
namespace UniversalChat.UI
{
    /// <summary>
    /// 메시지 데이터 (GameObject 없음, 순수 데이터)
    /// </summary>
    public class ChatMessageData
    {
        // 메시지 정보
        public string MessageId { get; set; }
        public string SenderId { get; set; }
        public string Nickname { get; set; }
        public string Content { get; set; }
        public long Timestamp { get; set; }
        public int MessageType { get; set; }

        // 높이 캐시 (-1 = 미계산)
        public float CachedHeight { get; set; } = -1f;

        // ChannelMessage → ChatMessageData 변환
        public static ChatMessageData FromChannelMessage(ChannelMessage msg);
    }
}
```

**메모리**: 약 200 bytes/메시지 (1000개 = 200KB)

### 4.2 ChatMessageView (뷰 컴포넌트)

**목적**: 재활용 가능한 UI 뷰, 데이터 바인딩 담당

```csharp
namespace UniversalChat.UI
{
    /// <summary>
    /// 재활용 가능한 메시지 뷰 (실제 GameObject)
    /// </summary>
    public class ChatMessageView : MonoBehaviour
    {
        // UI 참조
        [SerializeField] private Text _nicknameText;
        [SerializeField] private Text _contentText;
        [SerializeField] private Text _timestampText;
        [SerializeField] private Image _bubbleBackground;
        [SerializeField] private LayoutElement _layoutElement;
        [SerializeField] private RectTransform _rectTransform;

        // 바인딩된 데이터 인덱스 (-1 = 미사용)
        public int DataIndex { get; private set; } = -1;

        // 메서드
        public void Bind(ChatMessageData data, int dataIndex, ChatUIConfig config, string currentUserId);
        public float CalculateHeight(ChatMessageData data, ChatUIConfig config);
        public void SetPositionY(float y);
        public void Unbind();
    }
}
```

**특징**:
- `Bind()`: 데이터를 뷰에 바인딩하고 높이 계산
- `SetPositionY()`: 절대 Y 위치 설정 (LayoutGroup 미사용)
- `Unbind()`: 풀 반환 시 상태 초기화

### 4.3 VirtualizedChatPanel (메인 컨트롤러)

**목적**: 가상화 로직 총괄, 뷰 풀 관리

```csharp
namespace UniversalChat.UI
{
    /// <summary>
    /// 가상화 스크롤을 지원하는 채팅 패널
    /// </summary>
    public class VirtualizedChatPanel : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private ScrollRect _scrollRect;
        [SerializeField] private RectTransform _viewport;
        [SerializeField] private RectTransform _content;

        [Header("Settings")]
        [SerializeField] private int _bufferCount = 3;      // 화면 위아래 여유 아이템 수
        [SerializeField] private int _maxDataCount = 500;   // 최대 메시지 보관 수
        [SerializeField] private float _defaultItemHeight = 70f;

        #endregion

        #region Fields

        // 데이터 관리
        private readonly List<ChatMessageData> _dataList;
        private readonly List<float> _cumulativeHeights;  // 누적 높이 캐시

        // 뷰 관리
        private readonly List<ChatMessageView> _viewPool;
        private readonly Dictionary<int, ChatMessageView> _activeViews;  // dataIndex → view

        // 상태
        private int _visibleStartIndex;
        private int _visibleEndIndex;
        private float _totalContentHeight;
        private bool _isNearBottom;
        private bool _needsRebuild;

        #endregion

        #region Public Methods

        public void Initialize(ScrollRect scrollRect, RectTransform content, ChatUIConfig config);
        public void SetCurrentUserId(string userId);
        public void AddMessage(ChannelMessage message);
        public void Clear();
        public void ScrollToBottom();

        #endregion

        #region Private Methods

        // 스크롤 처리
        private void OnScrollChanged(Vector2 normalizedPosition);
        private void RebuildVisibleViews();
        private void CalculateVisibleRange(float scrollY, out int startIndex, out int endIndex);

        // 뷰 풀 관리
        private void CreateViewPool(int count);
        private ChatMessageView GetViewFromPool();
        private void ReturnViewToPool(ChatMessageView view);

        // 위치 계산
        private float GetItemTopPosition(int index);
        private float EstimateHeight(ChatMessageData data);

        #endregion
    }
}
```

---

## 5. 핵심 알고리즘

### 5.1 보이는 범위 계산 (이진 탐색)

```csharp
private void CalculateVisibleRange(float scrollY, out int startIndex, out int endIndex)
{
    // 시작 인덱스: scrollY 위치에 해당하는 아이템
    // _cumulativeHeights에서 scrollY보다 큰 첫 번째 인덱스
    startIndex = BinarySearchLowerBound(_cumulativeHeights, scrollY);

    // 끝 인덱스: scrollY + viewportHeight 위치에 해당하는 아이템
    float bottomY = scrollY + _viewportHeight;
    endIndex = BinarySearchLowerBound(_cumulativeHeights, bottomY);

    // 버퍼 적용
    startIndex = Mathf.Max(0, startIndex - _bufferCount);
    endIndex = Mathf.Min(_dataList.Count - 1, endIndex + _bufferCount);
}
```

**시간 복잡도**: O(log n)

### 5.2 뷰 재활용

```csharp
private void RebuildVisibleViews()
{
    // 1. 범위 밖 뷰 회수
    foreach (var kvp in _activeViews)
    {
        if (kvp.Key < startIndex || kvp.Key > endIndex)
        {
            ReturnViewToPool(kvp.Value);
            _activeViews.Remove(kvp.Key);
        }
    }

    // 2. 필요한 뷰 생성/바인딩
    for (int i = startIndex; i <= endIndex; i++)
    {
        if (!_activeViews.ContainsKey(i))
        {
            var view = GetViewFromPool();
            view.Bind(_dataList[i], i, _config, _currentUserId);
            view.SetPositionY(GetItemTopPosition(i));
            _activeViews[i] = view;
        }
    }
}
```

### 5.3 동적 높이 계산

```csharp
public float CalculateHeight(ChatMessageData data, ChatUIConfig config)
{
    // 캐시된 높이가 있으면 사용
    if (data.CachedHeight > 0)
        return data.CachedHeight;

    // 텍스트 높이 계산
    _contentText.text = data.Content;
    float textHeight = _contentText.preferredHeight;

    // 고정 요소 높이
    float nicknameHeight = (config?.NicknameFontSize ?? 12) + 4f;
    float timestampHeight = (config?.TimestampFontSize ?? 10) + 4f;
    float padding = 24f;
    float spacing = 8f;

    float totalHeight = nicknameHeight + textHeight + timestampHeight + padding + spacing;

    // 제한
    return Mathf.Clamp(totalHeight, 50f, 400f);
}
```

---

## 6. 레이아웃 구조

### 6.1 기존 구조 (LayoutGroup 기반)

```
Content (VerticalLayoutGroup)
├── ChatMessageItem (LayoutElement)
├── ChatMessageItem (LayoutElement)
└── ...
```

- LayoutGroup이 모든 자식 위치 계산
- 아이템 추가 시 전체 재계산

### 6.2 신규 구조 (절대 위치)

```
Content (sizeDelta.y = 전체 높이)
├── ChatMessageView (anchoredPosition.y = 계산된 Y)
├── ChatMessageView (anchoredPosition.y = 계산된 Y)
└── ...
```

- LayoutGroup 미사용
- 각 뷰의 Y 위치를 직접 설정
- 아이템 추가 시 새 아이템만 계산

---

## 7. 파일 구조

### 7.1 신규 파일

```
Assets/Plugins/UniversalChat/Runtime/UI/
├── Components/
│   ├── ChatMessageData.cs        ← 신규 (데이터 클래스)
│   ├── ChatMessageView.cs        ← 신규 (뷰 컴포넌트)
│   └── VirtualizedChatPanel.cs   ← 신규 (메인 컨트롤러)
└── ChatUIBuilder.cs              ← 수정 (BuildMessageView 추가)
```

### 7.2 수정 파일

| 파일 | 변경 내용 |
|------|-----------|
| `ChatUIBuilder.cs` | `BuildMessageView()` 메서드 추가 |
| `ChatUIManager.cs` | VirtualizedChatPanel 사용으로 변경 |

### 7.3 유지 파일 (하위 호환)

| 파일 | 상태 |
|------|------|
| `ChatPanel.cs` | 유지 (기존 사용자용) |
| `ChatMessageItem.cs` | 유지 (기존 사용자용) |

---

## 8. 성능 비교

### 8.1 메모리

| 항목 | 기존 (100개) | 가상화 (1000개) |
|------|--------------|-----------------|
| GameObject | 100개 (~10MB) | ~15개 (~1.5MB) |
| 데이터 | 내장 | 200KB |
| **총합** | ~10MB | ~1.7MB |

### 8.2 CPU (메시지 추가 시)

| 항목 | 기존 | 가상화 |
|------|------|--------|
| 레이아웃 계산 | O(n) 전체 | O(1) 새 아이템만 |
| 위치 계산 | LayoutGroup 의존 | O(log n) 이진 탐색 |
| 뷰 생성 | 새 GameObject | 풀에서 재사용 |

### 8.3 프레임 타임

| 메시지 수 | 기존 | 가상화 |
|-----------|------|--------|
| 100개 | ~17ms | ~1ms |
| 500개 | ~85ms | ~1ms |
| 1000개 | ~170ms (프레임 드랍) | ~1ms |

---

## 9. 구현 순서

| 단계 | 작업 | 예상 시간 |
|------|------|-----------|
| 1 | ChatMessageData 클래스 생성 | 15분 |
| 2 | ChatMessageView 컴포넌트 생성 | 30분 |
| 3 | VirtualizedChatPanel 구현 | 1시간 |
| 4 | ChatUIBuilder에 BuildMessageView 추가 | 20분 |
| 5 | ChatUIManager 연동 수정 | 20분 |
| 6 | 테스트 및 디버깅 | 30분 |

**총 예상 시간**: 약 3시간

---

## 10. 테스트 계획

### 10.1 기능 테스트

| 테스트 | 기대 결과 |
|--------|-----------|
| 메시지 추가 | 정상 표시, 동적 높이 적용 |
| 스크롤 | 부드러운 스크롤, 뷰 재활용 |
| 자동 스크롤 | 맨 아래 유지 시 자동 스크롤 |
| 긴 메시지 | 높이 자동 조절, 잘림 없음 |

### 10.2 성능 테스트

| 테스트 | 목표 |
|--------|------|
| 1000개 메시지 추가 | 프레임 드랍 없음 |
| 빠른 스크롤 | 60fps 유지 |
| 메모리 | 10MB 이하 |

### 10.3 엣지 케이스

| 케이스 | 처리 |
|--------|------|
| 빈 메시지 | 최소 높이 적용 |
| 매우 긴 메시지 | 최대 높이 제한 (400px) |
| 뷰포트 리사이즈 | 자동 재계산 |

---

## 11. 참고 자료

- Unity UI Best Practices: https://unity.com/how-to/unity-ui-optimization-tips
- Object Pooling Pattern: https://gameprogrammingpatterns.com/object-pool.html
- Virtual Scrolling: https://bvaughn.github.io/react-virtualized/

---

## 12. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|-----------|
| 1.0 | 2025-01-21 | 초안 작성 |
| 1.1 | 2025-01-21 | 구현 완료 - ChatMessageData, ChatMessageView, VirtualizedChatPanel 생성, ChatUIBuilder/ChatUIManager 수정 |
