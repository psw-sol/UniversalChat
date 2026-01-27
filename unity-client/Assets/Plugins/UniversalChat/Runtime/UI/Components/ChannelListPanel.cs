using System;
using System.Collections.Generic;
using UnityEngine;
using UnityEngine.UI;
using UniversalChat.Core;

namespace UniversalChat.UI
{
    /// <summary>
    /// 채널 목록을 표시하는 패널
    /// </summary>
    public class ChannelListPanel : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private ScrollRect _scrollRect;
        [SerializeField] private RectTransform _content;
        [SerializeField] private ChannelListItem _itemPrefab;
        [SerializeField] private Button _refreshButton;
        [SerializeField] private Text _emptyText;

        #endregion

        #region Events

        public event Action<string> OnChannelSelected;
        public event Action OnRefreshRequested;

        #endregion

        #region Fields

        private readonly List<ChannelListItem> _items = new List<ChannelListItem>();
        private readonly Queue<ChannelListItem> _itemPool = new Queue<ChannelListItem>();
        private string _selectedChannelId;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_refreshButton != null)
            {
                _refreshButton.onClick.AddListener(OnRefreshClicked);
            }
        }

        private void OnDestroy()
        {
            if (_refreshButton != null)
            {
                _refreshButton.onClick.RemoveListener(OnRefreshClicked);
            }
        }

        #endregion

        #region Public Methods

        public void UpdateChannelList(List<ChannelInfo> channels)
        {
            // Return existing items to pool
            foreach (var item in _items)
            {
                ReturnToPool(item);
            }
            _items.Clear();

            // Show/hide empty text
            if (_emptyText != null)
            {
                _emptyText.gameObject.SetActive(channels == null || channels.Count == 0);
            }

            if (channels == null || channels.Count == 0)
            {
                return;
            }

            // Create items
            foreach (var channel in channels)
            {
                var item = GetOrCreateItem();
                item.Setup(channel, channel.ChannelId == _selectedChannelId);
                item.OnClicked += HandleItemClicked;
                _items.Add(item);
            }
        }

        public void SelectChannel(string channelId)
        {
            _selectedChannelId = channelId;

            foreach (var item in _items)
            {
                item.SetSelected(item.ChannelId == channelId);
            }
        }

        public void ClearSelection()
        {
            _selectedChannelId = null;

            foreach (var item in _items)
            {
                item.SetSelected(false);
            }
        }

        #endregion

        #region Event Handlers

        private void OnRefreshClicked()
        {
            OnRefreshRequested?.Invoke();
        }

        private void HandleItemClicked(string channelId)
        {
            SelectChannel(channelId);
            OnChannelSelected?.Invoke(channelId);
        }

        #endregion

        #region Object Pooling

        private ChannelListItem GetOrCreateItem()
        {
            ChannelListItem item;

            if (_itemPool.Count > 0)
            {
                item = _itemPool.Dequeue();
                item.gameObject.SetActive(true);
            }
            else if (_itemPrefab != null)
            {
                item = Instantiate(_itemPrefab, _content);
            }
            else
            {
                var go = new GameObject("ChannelItem", typeof(RectTransform), typeof(ChannelListItem));
                go.transform.SetParent(_content, false);
                item = go.GetComponent<ChannelListItem>();
            }

            return item;
        }

        private void ReturnToPool(ChannelListItem item)
        {
            item.OnClicked -= HandleItemClicked;
            item.gameObject.SetActive(false);
            _itemPool.Enqueue(item);
        }

        #endregion
    }

    /// <summary>
    /// 채널 목록의 개별 아이템
    /// </summary>
    public class ChannelListItem : MonoBehaviour
    {
        #region Inspector

        [Header("References")]
        [SerializeField] private Text _nameText;
        [SerializeField] private Text _userCountText;
        [SerializeField] private Image _lockIcon;
        [SerializeField] private Image _background;
        [SerializeField] private Button _button;

        [Header("Colors")]
        [SerializeField] private Color _normalColor = new Color(0.2f, 0.2f, 0.2f, 1f);
        [SerializeField] private Color _selectedColor = new Color(0.3f, 0.5f, 0.8f, 1f);
        [SerializeField] private Color _hoverColor = new Color(0.25f, 0.25f, 0.25f, 1f);

        #endregion

        #region Events

        public event Action<string> OnClicked;

        #endregion

        #region Properties

        public string ChannelId { get; private set; }
        public string ChannelName { get; private set; }

        #endregion

        #region Fields

        private bool _isSelected;

        #endregion

        #region Unity Lifecycle

        private void Awake()
        {
            if (_button == null)
            {
                _button = GetComponent<Button>();
            }

            if (_button != null)
            {
                _button.onClick.AddListener(HandleClicked);
            }
        }

        private void OnDestroy()
        {
            if (_button != null)
            {
                _button.onClick.RemoveListener(HandleClicked);
            }
        }

        #endregion

        #region Public Methods

        public void Setup(ChannelInfo channel, bool isSelected)
        {
            ChannelId = channel.ChannelId;
            ChannelName = channel.ChannelName;

            if (_nameText != null)
            {
                _nameText.text = channel.ChannelName;
            }

            if (_userCountText != null)
            {
                _userCountText.text = $"{channel.CurrentUsers}/{channel.MaxUsers}";
            }

            if (_lockIcon != null)
            {
                _lockIcon.gameObject.SetActive(channel.HasPassword);
            }

            SetSelected(isSelected);
        }

        public void SetSelected(bool selected)
        {
            _isSelected = selected;
            UpdateBackground();
        }

        #endregion

        #region Event Handlers

        private void HandleClicked()
        {
            OnClicked?.Invoke(ChannelId);
        }

        #endregion

        #region Visual Updates

        private void UpdateBackground()
        {
            if (_background != null)
            {
                _background.color = _isSelected ? _selectedColor : _normalColor;
            }
        }

        #endregion
    }
}
