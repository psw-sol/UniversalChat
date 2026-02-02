using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UniversalChat.Core;

namespace UniversalChat.UI
{
    /// <summary>
    /// 가상화 스크롤을 지원하는 채팅 패널
    /// 화면에 보이는 아이템만 렌더링하여 성능 최적화
    /// </summary>
    public class VirtualizedChatPanel : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private ScrollRect _scrollRect;
        [SerializeField] private RectTransform _viewport;
        [SerializeField] private RectTransform _content;

        [Header("Prefab (Optional)")]
        [Tooltip("메시지 뷰 프리팹. 설정하지 않으면 코드로 자동 생성됩니다.")]
        [SerializeField] private ChatMessageView _messageViewPrefab;

        [Header("Settings")]
        [SerializeField] private int _bufferCount = 3;
        [SerializeField] private int _maxDataCount = 500;
        [SerializeField] private int _initialPoolSize = 15;
        [SerializeField] private bool _autoScroll = true;

        #endregion

        #region Fields

        // 데이터 관리
        private readonly List<ChatMessageData> _dataList = new List<ChatMessageData>();
        private readonly List<float> _cumulativeHeights = new List<float>();

        // 뷰 관리
        private readonly List<ChatMessageView> _viewPool = new List<ChatMessageView>();
        private readonly Dictionary<int, ChatMessageView> _activeViews = new Dictionary<int, ChatMessageView>();

        // 상태
        private int _visibleStartIndex;
        private int _visibleEndIndex;
        private float _totalContentHeight;
        private bool _isNearBottom = true;
        private float _viewportHeight;

        // 설정
        private ChatUIConfig _config;
        private string _currentUserId;

        // 최적화를 위한 캐시
        private float _lastScrollPosition = -1f;
        private bool _isDirty = false;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_scrollRect != null)
            {
                _scrollRect.onValueChanged.AddListener(OnScrollChanged);
            }
        }

        private void OnDestroy()
        {
            if (_scrollRect != null)
            {
                _scrollRect.onValueChanged.RemoveListener(OnScrollChanged);
            }
        }

        private void LateUpdate()
        {
            if (_isDirty)
            {
                RebuildVisibleViews();
                _isDirty = false;
            }
        }

        #endregion

        #region Public Methods

        /// <summary>
        /// 패널 초기화
        /// </summary>
        public void Initialize(ScrollRect scrollRect, RectTransform content, ChatUIConfig config)
        {
            _scrollRect = scrollRect;
            _content = content;
            _config = config;

            if (_scrollRect != null)
            {
                _viewport = _scrollRect.viewport;
                _scrollRect.onValueChanged.RemoveListener(OnScrollChanged);
                _scrollRect.onValueChanged.AddListener(OnScrollChanged);
            }

            if (config != null)
            {
                _maxDataCount = config.MessageHistorySize;
                _autoScroll = config.AutoScrollToBottom;
            }

            // Content RectTransform 설정 (절대 위치 사용을 위해)
            SetupContentRectTransform();

            // 뷰 풀 생성
            CreateViewPool(_initialPoolSize);

            UpdateViewportHeight();
        }

        /// <summary>
        /// 현재 사용자 ID 설정
        /// </summary>
        public void SetCurrentUserId(string userId)
        {
            _currentUserId = userId;
        }

        /// <summary>
        /// 메시지 추가
        /// </summary>
        public void AddMessage(ChannelMessage message)
        {
            var data = ChatMessageData.FromChannelMessage(message);
            AddMessageData(data);
        }

        /// <summary>
        /// 메시지 데이터 직접 추가
        /// </summary>
        public void AddMessageData(ChatMessageData data)
        {
            // 데이터 리스트에 추가
            _dataList.Add(data);

            // 높이 계산 및 누적 높이 업데이트
            float height = EstimateHeight(data);
            data.CachedHeight = height;

            float prevCumulativeHeight = _cumulativeHeights.Count > 0
                ? _cumulativeHeights[_cumulativeHeights.Count - 1]
                : 0f;
            _cumulativeHeights.Add(prevCumulativeHeight + height);

            // 총 높이 업데이트
            _totalContentHeight = _cumulativeHeights[_cumulativeHeights.Count - 1];
            UpdateContentSize();

            // 최대 개수 초과 시 오래된 데이터 제거
            while (_dataList.Count > _maxDataCount)
            {
                RemoveOldestMessage();
            }

            // 자동 스크롤
            if (_autoScroll && _isNearBottom)
            {
                ScrollToBottom();
            }
            else
            {
                _isDirty = true;
            }
        }

        /// <summary>
        /// 모든 메시지 삭제
        /// </summary>
        public void Clear()
        {
            // 모든 활성 뷰 반환
            foreach (var kvp in _activeViews)
            {
                ReturnViewToPool(kvp.Value);
            }
            _activeViews.Clear();

            // 데이터 초기화
            _dataList.Clear();
            _cumulativeHeights.Clear();
            _totalContentHeight = 0f;

            UpdateContentSize();
        }

        /// <summary>
        /// 맨 아래로 스크롤
        /// </summary>
        public void ScrollToBottom()
        {
            if (_scrollRect != null)
            {
                Canvas.ForceUpdateCanvases();
                _scrollRect.verticalNormalizedPosition = 0f;
                _isNearBottom = true;
                _isDirty = true;
            }
        }

        /// <summary>
        /// 맨 위로 스크롤
        /// </summary>
        public void ScrollToTop()
        {
            if (_scrollRect != null)
            {
                _scrollRect.verticalNormalizedPosition = 1f;
                _isDirty = true;
            }
        }

        /// <summary>
        /// 현재 메시지 수 반환
        /// </summary>
        public int MessageCount => _dataList.Count;

        #endregion

        #region Private Methods - Scroll Handling

        private void OnScrollChanged(Vector2 normalizedPosition)
        {
            // 하단 근처 여부 체크 (10% 이내)
            _isNearBottom = normalizedPosition.y < 0.1f;

            // 스크롤 위치가 변경되었으면 뷰 갱신 예약
            _isDirty = true;
        }

        private void RebuildVisibleViews()
        {
            if (_dataList.Count == 0)
                return;

            UpdateViewportHeight();

            // 현재 스크롤 위치 계산
            float scrollY = GetScrollY();

            // 보이는 범위 계산
            CalculateVisibleRange(scrollY, out int startIndex, out int endIndex);

            // 범위 밖 뷰 회수
            var keysToRemove = new List<int>();
            foreach (var kvp in _activeViews)
            {
                if (kvp.Key < startIndex || kvp.Key > endIndex)
                {
                    ReturnViewToPool(kvp.Value);
                    keysToRemove.Add(kvp.Key);
                }
            }
            foreach (var key in keysToRemove)
            {
                _activeViews.Remove(key);
            }

            // 필요한 뷰 생성/바인딩
            for (int i = startIndex; i <= endIndex; i++)
            {
                if (!_activeViews.ContainsKey(i))
                {
                    var view = GetViewFromPool();
                    if (view != null)
                    {
                        view.Bind(_dataList[i], i, _config, _currentUserId);

                        // 정확한 높이 계산 (캐시 미스 시)
                        if (_dataList[i].CachedHeight <= 0)
                        {
                            float height = view.CalculateHeight(_dataList[i], _config);
                            _dataList[i].CachedHeight = height;
                            RecalculateCumulativeHeights(i);
                        }

                        view.SetPositionY(GetItemTopPosition(i));
                        _activeViews[i] = view;
                    }
                }
            }

            _visibleStartIndex = startIndex;
            _visibleEndIndex = endIndex;
        }

        private void CalculateVisibleRange(float scrollY, out int startIndex, out int endIndex)
        {
            if (_dataList.Count == 0)
            {
                startIndex = 0;
                endIndex = -1;
                return;
            }

            // 시작 인덱스: scrollY 위치에 해당하는 아이템 (이진 탐색)
            startIndex = BinarySearchLowerBound(_cumulativeHeights, scrollY);

            // 끝 인덱스: scrollY + viewportHeight 위치에 해당하는 아이템
            float bottomY = scrollY + _viewportHeight;
            endIndex = BinarySearchLowerBound(_cumulativeHeights, bottomY);

            // 버퍼 적용
            startIndex = Mathf.Max(0, startIndex - _bufferCount);
            endIndex = Mathf.Min(_dataList.Count - 1, endIndex + _bufferCount);
        }

        private int BinarySearchLowerBound(List<float> heights, float target)
        {
            if (heights.Count == 0)
                return 0;

            int left = 0;
            int right = heights.Count;

            while (left < right)
            {
                int mid = (left + right) / 2;
                if (heights[mid] < target)
                    left = mid + 1;
                else
                    right = mid;
            }

            return Mathf.Clamp(left, 0, heights.Count - 1);
        }

        private float GetScrollY()
        {
            if (_scrollRect == null || _content == null)
                return 0f;

            // Content의 anchoredPosition.y가 스크롤 위치
            // 위에서 아래로 스크롤할수록 값이 커짐
            float maxScroll = Mathf.Max(0f, _totalContentHeight - _viewportHeight);
            float normalizedY = _scrollRect.verticalNormalizedPosition;

            // 반전: normalizedPosition 1 = 맨 위, 0 = 맨 아래
            return (1f - normalizedY) * maxScroll;
        }

        #endregion

        #region Private Methods - View Pool

        private void CreateViewPool(int count)
        {
            for (int i = 0; i < count; i++)
            {
                var view = CreateMessageView();
                if (view != null)
                {
                    view.Unbind();
                    _viewPool.Add(view);
                }
            }
        }

        private ChatMessageView GetViewFromPool()
        {
            // 비활성 뷰 찾기
            foreach (var view in _viewPool)
            {
                if (!view.gameObject.activeSelf)
                {
                    return view;
                }
            }

            // 풀에 여유가 없으면 새로 생성
            var newView = CreateMessageView();
            if (newView != null)
            {
                _viewPool.Add(newView);
            }
            return newView;
        }

        /// <summary>
        /// 메시지 뷰 생성 (프리팹 우선, 없으면 코드 생성)
        /// </summary>
        private ChatMessageView CreateMessageView()
        {
            if (_messageViewPrefab != null)
            {
                // 프리팹으로 생성
                var view = Instantiate(_messageViewPrefab, _content);
                view.name = "Chat Message View";
                return view;
            }
            else
            {
                // 코드로 생성
                return ChatUIBuilder.BuildMessageView(_content, _config);
            }
        }

        private void ReturnViewToPool(ChatMessageView view)
        {
            if (view != null)
            {
                view.Unbind();
            }
        }

        #endregion

        #region Private Methods - Height Calculation

        private float EstimateHeight(ChatMessageData data)
        {
            // 캐시된 높이가 있으면 사용
            if (data.CachedHeight > 0)
                return data.CachedHeight;

            // 간단한 추정 (실제 렌더링 전)
            float baseHeight = _config?.DefaultItemHeight ?? 70f;

            // 메시지 길이에 따른 대략적인 높이 추정
            if (!string.IsNullOrEmpty(data.Content))
            {
                int charCount = data.Content.Length;
                int estimatedLines = Mathf.CeilToInt(charCount / 40f); // 대략 한 줄에 40자

                if (estimatedLines > 1)
                {
                    float lineHeight = _config?.MessageFontSize ?? 14f;
                    float additionalHeight = (estimatedLines - 1) * (lineHeight + 2f);
                    baseHeight += additionalHeight;
                }
            }

            return Mathf.Clamp(baseHeight, 50f, 400f);
        }

        private float GetItemTopPosition(int index)
        {
            if (index == 0)
                return 0f;

            if (index > 0 && index <= _cumulativeHeights.Count)
            {
                return _cumulativeHeights[index - 1];
            }

            return 0f;
        }

        private void RecalculateCumulativeHeights(int fromIndex)
        {
            for (int i = fromIndex; i < _dataList.Count; i++)
            {
                float prevHeight = i > 0 ? _cumulativeHeights[i - 1] : 0f;
                float itemHeight = _dataList[i].CachedHeight > 0
                    ? _dataList[i].CachedHeight
                    : EstimateHeight(_dataList[i]);

                if (i < _cumulativeHeights.Count)
                {
                    _cumulativeHeights[i] = prevHeight + itemHeight;
                }
                else
                {
                    _cumulativeHeights.Add(prevHeight + itemHeight);
                }
            }

            if (_cumulativeHeights.Count > 0)
            {
                _totalContentHeight = _cumulativeHeights[_cumulativeHeights.Count - 1];
                UpdateContentSize();
            }
        }

        #endregion

        #region Private Methods - Helpers

        private void SetupContentRectTransform()
        {
            if (_content != null)
            {
                // 앵커를 상단으로 설정 (위에서 아래로 배치)
                _content.anchorMin = new Vector2(0f, 1f);
                _content.anchorMax = new Vector2(1f, 1f);
                _content.pivot = new Vector2(0.5f, 1f);
            }
        }

        private void UpdateContentSize()
        {
            if (_content != null)
            {
                var sizeDelta = _content.sizeDelta;
                sizeDelta.y = _totalContentHeight;
                _content.sizeDelta = sizeDelta;
            }
        }

        private void UpdateViewportHeight()
        {
            if (_viewport != null)
            {
                _viewportHeight = _viewport.rect.height;
            }
            else if (_scrollRect != null && _scrollRect.viewport != null)
            {
                _viewportHeight = _scrollRect.viewport.rect.height;
            }
            else
            {
                _viewportHeight = 400f; // 기본값
            }
        }

        private void RemoveOldestMessage()
        {
            if (_dataList.Count == 0)
                return;

            // 첫 번째 아이템에 대한 활성 뷰가 있으면 회수
            if (_activeViews.TryGetValue(0, out var view))
            {
                ReturnViewToPool(view);
                _activeViews.Remove(0);
            }

            // 데이터 제거
            float removedHeight = _dataList[0].CachedHeight > 0
                ? _dataList[0].CachedHeight
                : EstimateHeight(_dataList[0]);

            _dataList.RemoveAt(0);
            _cumulativeHeights.RemoveAt(0);

            // 누적 높이 재계산 (모든 값에서 제거된 높이 차감)
            for (int i = 0; i < _cumulativeHeights.Count; i++)
            {
                _cumulativeHeights[i] -= removedHeight;
            }

            // 활성 뷰의 인덱스 조정
            var newActiveViews = new Dictionary<int, ChatMessageView>();
            foreach (var kvp in _activeViews)
            {
                if (kvp.Key > 0)
                {
                    newActiveViews[kvp.Key - 1] = kvp.Value;
                    kvp.Value.Bind(_dataList[kvp.Key - 1], kvp.Key - 1, _config, _currentUserId);
                }
            }
            _activeViews.Clear();
            foreach (var kvp in newActiveViews)
            {
                _activeViews[kvp.Key] = kvp.Value;
            }

            _totalContentHeight -= removedHeight;
            UpdateContentSize();
        }

        #endregion
    }
}
