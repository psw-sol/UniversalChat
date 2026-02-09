using UnityEngine;
using UnityEngine.UI;
using TMPro;
using UniversalChat.RichContent;

namespace UniversalChat.UI
{
    /// <summary>
    /// 런타임에 채팅 UI를 동적으로 생성하는 빌더 클래스
    /// 프리팹 없이 코드로 완전한 채팅 UI를 생성합니다.
    /// </summary>
    public static class ChatUIBuilder
    {
        #region Public Methods

        /// <summary>
        /// 완전한 채팅 UI를 생성합니다.
        /// </summary>
        /// <param name="parent">부모 Transform (Canvas 권장)</param>
        /// <param name="config">UI 설정 (null이면 기본값 사용)</param>
        /// <returns>생성된 ChatUIManager</returns>
        public static ChatUIManager Build(Transform parent, ChatUIConfig config = null)
        {
            // Root 생성
            var root = CreateUIObject("Chat UI", parent);
            var rootRect = root.GetComponent<RectTransform>();
            rootRect.sizeDelta = new Vector2(420, 600);

            var rootImage = root.AddComponent<Image>();
            rootImage.color = config?.BackgroundColor ?? new Color(0.15f, 0.15f, 0.15f, 1f);

            var rootLayout = root.AddComponent<VerticalLayoutGroup>();
            rootLayout.childForceExpandWidth = true;
            rootLayout.childForceExpandHeight = false;
            rootLayout.childControlWidth = true;
            rootLayout.childControlHeight = true;
            rootLayout.spacing = 5;
            rootLayout.padding = new RectOffset(10, 10, 10, 10);

            // Header 생성
            var (titleText, statusText) = CreateHeader(root.transform, config);

            // ChatUIManager 추가
            var uiManager = root.AddComponent<ChatUIManager>();

            // VirtualizedChatPanel 생성
            var virtualizedPanel = CreateVirtualizedChatPanel(root.transform, config);

            // Input Area 생성
            var (chatInputField, sendButton) = CreateInputArea(root.transform, config);

            uiManager.Initialize(virtualizedPanel, chatInputField, sendButton, statusText, config);

            return uiManager;
        }

        /// <summary>
        /// 가상화 스크롤용 메시지 뷰를 생성합니다.
        /// </summary>
        /// <param name="parent">부모 Transform</param>
        /// <param name="config">UI 설정</param>
        /// <returns>생성된 ChatMessageView</returns>
        public static ChatMessageView BuildMessageView(Transform parent, ChatUIConfig config = null)
        {
            // === Root (Chat Message View) ===
            var root = CreateUIObject("Chat Message View", parent);
            var rootRect = root.GetComponent<RectTransform>();

            // 절대 위치 사용을 위한 앵커 설정 (부모 너비에 맞춤)
            rootRect.anchorMin = new Vector2(0f, 1f);
            rootRect.anchorMax = new Vector2(1f, 1f);
            rootRect.pivot = new Vector2(0.5f, 1f);
            // 부모(Content) 너비에 맞춤 - offset으로 설정
            rootRect.offsetMin = new Vector2(0f, 0f);
            rootRect.offsetMax = new Vector2(0f, 0f);

            // LayoutElement로 높이 제어
            var layoutElement = root.AddComponent<LayoutElement>();
            layoutElement.minHeight = 50;
            layoutElement.preferredHeight = config?.DefaultItemHeight ?? 70f;
            layoutElement.flexibleWidth = 1;

            // Root에 패딩용 이미지 (투명)
            var rootImage = root.AddComponent<Image>();
            rootImage.color = Color.clear;

            // === Background (Bubble) - Root의 직접 자식 ===
            var bubble = CreateUIObject("Bubble", root.transform);
            var bubbleRect = bubble.GetComponent<RectTransform>();

            // 버블 기본 위치: 왼쪽 상단 (다른 사람 메시지 기본)
            bubbleRect.anchorMin = new Vector2(0f, 1f);
            bubbleRect.anchorMax = new Vector2(0f, 1f);
            bubbleRect.pivot = new Vector2(0f, 1f);
            bubbleRect.anchoredPosition = new Vector2(8f, -4f);  // 패딩

            var bubbleImage = bubble.AddComponent<Image>();
            bubbleImage.color = config?.OtherBubbleColor ?? new Color(0.25f, 0.25f, 0.25f, 1f);

            // Bubble에 ContentSizeFitter 추가 - 내용에 맞게 크기 조절
            var bubbleFitter = bubble.AddComponent<ContentSizeFitter>();
            bubbleFitter.horizontalFit = ContentSizeFitter.FitMode.PreferredSize;
            bubbleFitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            // Bubble에 VerticalLayoutGroup 추가
            var bubbleLayout = bubble.AddComponent<VerticalLayoutGroup>();
            bubbleLayout.childForceExpandWidth = false;
            bubbleLayout.childForceExpandHeight = false;
            bubbleLayout.childControlWidth = true;
            bubbleLayout.childControlHeight = true;
            bubbleLayout.padding = new RectOffset(12, 12, 8, 8);
            bubbleLayout.spacing = 2;

            // === Nickname Text ===
            var nickname = CreateTextObject("Nickname", bubble.transform, "Username",
                config?.NicknameFontSize ?? 14, TextAnchor.UpperLeft);
            var nicknameText = nickname.GetComponent<Text>();
            nicknameText.color = config?.NicknameColor ?? new Color(0.7f, 0.7f, 0.7f);
            nicknameText.fontStyle = FontStyle.Bold;

            var nicknameLayout = nickname.AddComponent<LayoutElement>();
            nicknameLayout.minHeight = 18;
            nicknameLayout.preferredHeight = 18;
            nicknameLayout.minWidth = 50;

            // === Message Text ===
            var message = CreateTextObject("Message", bubble.transform, "Message content",
                config?.MessageFontSize ?? 14, TextAnchor.UpperLeft);
            var messageText = message.GetComponent<Text>();
            messageText.color = config?.MessageColor ?? Color.white;
            messageText.horizontalOverflow = HorizontalWrapMode.Wrap;
            messageText.verticalOverflow = VerticalWrapMode.Overflow;

            var messageLayout = message.AddComponent<LayoutElement>();
            messageLayout.minHeight = 18;
            messageLayout.minWidth = 50;
            messageLayout.preferredWidth = 250;  // 최대 텍스트 너비

            // === Timestamp Text ===
            var timestamp = CreateTextObject("Timestamp", bubble.transform, "12:00",
                config?.TimestampFontSize ?? 11, TextAnchor.LowerRight);
            var timestampText = timestamp.GetComponent<Text>();
            timestampText.color = config?.TimestampColor ?? new Color(0.5f, 0.5f, 0.5f);

            var timestampLayout = timestamp.AddComponent<LayoutElement>();
            timestampLayout.minHeight = 14;
            timestampLayout.preferredHeight = 14;

            // ChatMessageView 컴포넌트 추가
            var messageView = root.AddComponent<ChatMessageView>();
            messageView.Initialize(
                nicknameText,
                messageText,
                timestampText,
                bubbleImage,
                layoutElement,
                bubbleRect  // Bubble RectTransform 전달
            );

            return messageView;
        }

        /// <summary>
        /// 가상화 스크롤을 사용하는 채팅 패널을 생성합니다.
        /// </summary>
        /// <param name="parent">부모 Transform</param>
        /// <param name="config">UI 설정</param>
        /// <returns>생성된 VirtualizedChatPanel</returns>
        public static VirtualizedChatPanel CreateVirtualizedChatPanel(Transform parent, ChatUIConfig config = null)
        {
            var chatPanelObj = CreateUIObject("Virtualized Chat Panel", parent);

            var chatPanelLayoutElement = chatPanelObj.AddComponent<LayoutElement>();
            chatPanelLayoutElement.flexibleHeight = 1;

            var scrollRect = chatPanelObj.AddComponent<ScrollRect>();
            scrollRect.horizontal = false;
            scrollRect.vertical = true;
            scrollRect.movementType = ScrollRect.MovementType.Elastic;
            scrollRect.elasticity = 0.1f;

            var panelImage = chatPanelObj.AddComponent<Image>();
            panelImage.color = config?.PanelBackgroundColor ?? new Color(0.1f, 0.1f, 0.1f, 0.95f);

            // Viewport
            var viewport = CreateUIObject("Viewport", chatPanelObj.transform);
            var viewportRect = viewport.GetComponent<RectTransform>();
            viewportRect.anchorMin = Vector2.zero;
            viewportRect.anchorMax = Vector2.one;
            viewportRect.sizeDelta = Vector2.zero;
            viewportRect.pivot = new Vector2(0, 1);

            var viewportImage = viewport.AddComponent<Image>();
            viewportImage.color = Color.white;
            viewport.AddComponent<Mask>().showMaskGraphic = false;

            scrollRect.viewport = viewportRect;

            // Content (LayoutGroup 미사용 - 절대 위치 사용)
            var content = CreateUIObject("Content", viewport.transform);
            var contentRect = content.GetComponent<RectTransform>();
            contentRect.anchorMin = new Vector2(0, 1);
            contentRect.anchorMax = new Vector2(1, 1);
            contentRect.pivot = new Vector2(0.5f, 1);
            contentRect.sizeDelta = new Vector2(0, 0);

            scrollRect.content = contentRect;

            // VirtualizedChatPanel 컴포넌트 추가
            var virtualizedPanel = chatPanelObj.AddComponent<VirtualizedChatPanel>();
            virtualizedPanel.Initialize(scrollRect, contentRect, config);

            return virtualizedPanel;
        }

        #endregion

        #region Private Methods - UI Creation

        private static (Text titleText, Text statusText) CreateHeader(Transform parent, ChatUIConfig config)
        {
            var header = CreateUIObject("Header", parent);
            var headerLayoutElement = header.AddComponent<LayoutElement>();
            headerLayoutElement.minHeight = 40;
            headerLayoutElement.preferredHeight = 40;
            headerLayoutElement.flexibleHeight = 0;

            var headerHLayout = header.AddComponent<HorizontalLayoutGroup>();
            headerHLayout.childForceExpandWidth = false;
            headerHLayout.childForceExpandHeight = true;
            headerHLayout.childControlWidth = true;
            headerHLayout.childControlHeight = true;
            headerHLayout.spacing = 10;

            // Title
            var title = CreateTextObject("Title", header.transform, "Chat",
                config?.TitleFontSize ?? 18, TextAnchor.MiddleLeft);
            title.GetComponent<Text>().fontStyle = FontStyle.Bold;
            title.GetComponent<Text>().color = config?.TitleColor ?? Color.white;
            var titleLayout = title.AddComponent<LayoutElement>();
            titleLayout.flexibleWidth = 1;

            // Connection Status
            var status = CreateTextObject("Status", header.transform, "Disconnected",
                config?.StatusFontSize ?? 12, TextAnchor.MiddleRight);
            status.GetComponent<Text>().color = config?.DisconnectedColor ?? Color.red;
            var statusLayout = status.AddComponent<LayoutElement>();
            statusLayout.minWidth = 100;

            return (title.GetComponent<Text>(), status.GetComponent<Text>());
        }

        private static (ChatInputField chatInputField, Button sendButton) CreateInputArea(Transform parent, ChatUIConfig config)
        {
            var inputArea = CreateUIObject("Input Area", parent);
            var inputAreaLayout = inputArea.AddComponent<LayoutElement>();
            inputAreaLayout.minHeight = 50;
            inputAreaLayout.preferredHeight = 50;
            inputAreaLayout.flexibleHeight = 0;

            var inputAreaHLayout = inputArea.AddComponent<HorizontalLayoutGroup>();
            inputAreaHLayout.childForceExpandWidth = false;
            inputAreaHLayout.childForceExpandHeight = true;
            inputAreaHLayout.childControlWidth = true;
            inputAreaHLayout.childControlHeight = true;
            inputAreaHLayout.spacing = 10;
            inputAreaHLayout.padding = new RectOffset(10, 10, 5, 5);

            // Input Field Background
            var inputBg = CreateUIObject("Input Background", inputArea.transform);
            var inputBgImage = inputBg.AddComponent<Image>();
            inputBgImage.color = config?.InputBackgroundColor ?? new Color(0.2f, 0.2f, 0.2f, 1f);

            var inputBgLayout = inputBg.AddComponent<LayoutElement>();
            inputBgLayout.flexibleWidth = 1;
            inputBgLayout.minHeight = 30;

            var inputField = inputBg.AddComponent<InputField>();
            inputField.lineType = InputField.LineType.SingleLine;
            inputField.characterLimit = config?.MaxMessageLength ?? 500;

            // Text Area
            var textArea = CreateUIObject("Text Area", inputBg.transform);
            var textAreaRect = textArea.GetComponent<RectTransform>();
            textAreaRect.anchorMin = Vector2.zero;
            textAreaRect.anchorMax = Vector2.one;
            textAreaRect.offsetMin = new Vector2(10, 5);
            textAreaRect.offsetMax = new Vector2(-10, -5);

            var placeholder = CreateTextObject("Placeholder", textArea.transform,
                config?.PlaceholderText ?? "Type a message...",
                config?.InputFontSize ?? 14, TextAnchor.MiddleLeft);
            var placeholderRect = placeholder.GetComponent<RectTransform>();
            placeholderRect.anchorMin = Vector2.zero;
            placeholderRect.anchorMax = Vector2.one;
            placeholderRect.sizeDelta = Vector2.zero;
            placeholder.GetComponent<Text>().color = config?.PlaceholderColor ?? new Color(0.5f, 0.5f, 0.5f);

            var inputText = CreateTextObject("Text", textArea.transform, "",
                config?.InputFontSize ?? 14, TextAnchor.MiddleLeft);
            var inputTextRect = inputText.GetComponent<RectTransform>();
            inputTextRect.anchorMin = Vector2.zero;
            inputTextRect.anchorMax = Vector2.one;
            inputTextRect.sizeDelta = Vector2.zero;
            inputText.GetComponent<Text>().color = config?.InputTextColor ?? Color.white;
            inputText.GetComponent<Text>().supportRichText = false;

            inputField.textComponent = inputText.GetComponent<Text>();
            inputField.placeholder = placeholder.GetComponent<Text>();

            // Send Button
            var sendBtn = CreateUIObject("Send Button", inputArea.transform);
            var sendBtnImage = sendBtn.AddComponent<Image>();
            sendBtnImage.color = config?.SendButtonColor ?? new Color(0.3f, 0.5f, 0.8f, 1f);

            var sendBtnLayout = sendBtn.AddComponent<LayoutElement>();
            sendBtnLayout.minWidth = 60;
            sendBtnLayout.preferredWidth = 60;

            var sendButton = sendBtn.AddComponent<Button>();
            sendButton.targetGraphic = sendBtnImage;

            var sendBtnText = CreateTextObject("Text", sendBtn.transform,
                config?.SendButtonText ?? "Send",
                config?.SendButtonFontSize ?? 14, TextAnchor.MiddleCenter);
            var sendBtnTextRect = sendBtnText.GetComponent<RectTransform>();
            sendBtnTextRect.anchorMin = Vector2.zero;
            sendBtnTextRect.anchorMax = Vector2.one;
            sendBtnTextRect.sizeDelta = Vector2.zero;
            sendBtnText.GetComponent<Text>().color = config?.SendButtonTextColor ?? Color.white;

            // ChatInputField 컴포넌트 추가
            var chatInputField = inputArea.AddComponent<ChatInputField>();
            chatInputField.Initialize(inputField);

            return (chatInputField, sendButton);
        }

        #endregion

        #region Rich Content Methods

        /// <summary>
        /// Rich Content 지원 메시지 아이템 생성 (TextMeshPro 사용)
        /// </summary>
        /// <param name="parent">부모 Transform</param>
        /// <param name="config">UI 설정</param>
        /// <returns>생성된 RichChatMessageItem</returns>
        public static RichChatMessageItem BuildRichMessageItem(Transform parent, ChatUIConfig config = null)
        {
            // === Root (Rich Chat Message Item) ===
            var root = CreateUIObject("Rich Chat Message Item", parent);
            var rootRect = root.GetComponent<RectTransform>();

            // LayoutElement로 부모 레이아웃에 크기 보고
            var layoutElement = root.AddComponent<LayoutElement>();
            layoutElement.minHeight = 50;
            layoutElement.preferredHeight = config?.DefaultItemHeight ?? 70f;
            layoutElement.flexibleWidth = 1;

            // Root에 VerticalLayoutGroup 추가
            var rootLayout = root.AddComponent<VerticalLayoutGroup>();
            rootLayout.childForceExpandWidth = true;
            rootLayout.childForceExpandHeight = true;
            rootLayout.childControlWidth = true;
            rootLayout.childControlHeight = true;
            rootLayout.padding = new RectOffset(5, 5, 2, 2);

            // === Background (Bubble) ===
            var bubble = CreateUIObject("Bubble", root.transform);

            var bubbleImage = bubble.AddComponent<Image>();
            bubbleImage.color = config?.OtherBubbleColor ?? new Color(0.25f, 0.25f, 0.25f, 1f);
            var bubbleRect = bubble.GetComponent<RectTransform>();

            // Bubble에 VerticalLayoutGroup 추가
            var bubbleLayout = bubble.AddComponent<VerticalLayoutGroup>();
            bubbleLayout.childForceExpandWidth = true;
            bubbleLayout.childForceExpandHeight = true;
            bubbleLayout.childControlWidth = true;
            bubbleLayout.childControlHeight = true;
            bubbleLayout.padding = new RectOffset(10, 10, 8, 8);

            // === Content Container ===
            var content = CreateUIObject("Content", bubble.transform);
            var contentRect = content.GetComponent<RectTransform>();

            var contentLayout = content.AddComponent<VerticalLayoutGroup>();
            contentLayout.childForceExpandWidth = true;
            contentLayout.childForceExpandHeight = false;
            contentLayout.childControlWidth = true;
            contentLayout.childControlHeight = true;
            contentLayout.spacing = 4;

            // === Nickname Text (TMP + RichChatText) ===
            var nickname = CreateTMPTextObject("Nickname", content.transform, "Username",
                config?.NicknameFontSize ?? 12, TextAlignmentOptions.TopLeft);
            var nicknameTMP = nickname.GetComponent<TextMeshProUGUI>();
            nicknameTMP.color = config?.NicknameColor ?? new Color(0.7f, 0.7f, 0.7f);
            nicknameTMP.fontStyle = FontStyles.Bold;
            var nicknameRich = nickname.AddComponent<RichChatText>();

            var nicknameLayout = nickname.AddComponent<LayoutElement>();
            nicknameLayout.minHeight = 16;
            nicknameLayout.preferredHeight = 16;

            // === Message Text (TMP + RichChatText) ===
            var message = CreateTMPTextObject("Message", content.transform, "Message content",
                config?.MessageFontSize ?? 14, TextAlignmentOptions.TopLeft);
            var messageTMP = message.GetComponent<TextMeshProUGUI>();
            messageTMP.color = config?.MessageColor ?? Color.white;
            messageTMP.enableWordWrapping = true;
            messageTMP.overflowMode = TextOverflowModes.Overflow;
            var messageRich = message.AddComponent<RichChatText>();

            var messageLayout = message.AddComponent<LayoutElement>();
            messageLayout.minHeight = 18;
            messageLayout.flexibleWidth = 1;

            // === Timestamp Text (일반 TMP) ===
            var timestamp = CreateTMPTextObject("Timestamp", content.transform, "12:00",
                config?.TimestampFontSize ?? 10, TextAlignmentOptions.BottomRight);
            var timestampTMP = timestamp.GetComponent<TextMeshProUGUI>();
            timestampTMP.color = config?.TimestampColor ?? new Color(0.5f, 0.5f, 0.5f);

            var timestampLayout = timestamp.AddComponent<LayoutElement>();
            timestampLayout.minHeight = 14;
            timestampLayout.preferredHeight = 14;

            // RichChatMessageItem 컴포넌트 추가
            var richMessageItem = root.AddComponent<RichChatMessageItem>();
            richMessageItem.Initialize(
                nicknameRich,
                messageRich,
                timestampTMP,
                bubbleImage,
                layoutElement,
                bubbleRect
            );
            richMessageItem.SetChatConfig(config);

            return richMessageItem;
        }

        /// <summary>
        /// Rich Content 지원 가상화 메시지 뷰 생성
        /// </summary>
        public static RichChatMessageView BuildRichMessageView(Transform parent, ChatUIConfig config = null)
        {
            // === Root (Rich Chat Message View) ===
            var root = CreateUIObject("Rich Chat Message View", parent);
            var rootRect = root.GetComponent<RectTransform>();

            // 절대 위치 사용을 위한 앵커 설정
            rootRect.anchorMin = new Vector2(0f, 1f);
            rootRect.anchorMax = new Vector2(1f, 1f);
            rootRect.pivot = new Vector2(0.5f, 1f);

            // LayoutElement
            var layoutElement = root.AddComponent<LayoutElement>();
            layoutElement.minHeight = 50;
            layoutElement.preferredHeight = config?.DefaultItemHeight ?? 70f;
            layoutElement.flexibleWidth = 1;

            // Root에 HorizontalLayoutGroup 추가 (버블 좌/우 정렬용)
            var rootLayout = root.AddComponent<HorizontalLayoutGroup>();
            rootLayout.childForceExpandWidth = false;
            rootLayout.childForceExpandHeight = true;
            rootLayout.childControlWidth = true;
            rootLayout.childControlHeight = true;
            rootLayout.childAlignment = TextAnchor.UpperLeft;  // 기본: 좌측 정렬 (다른 사람)
            rootLayout.padding = new RectOffset(5, 5, 2, 2);

            // === Bubble Background ===
            var bubble = CreateUIObject("Bubble", root.transform);
            var bubbleRect = bubble.GetComponent<RectTransform>();

            var bubbleImage = bubble.AddComponent<Image>();
            bubbleImage.color = config?.OtherBubbleColor ?? new Color(0.25f, 0.25f, 0.25f, 1f);

            // Bubble에 LayoutElement 추가 (최대 너비 제한)
            var bubbleLayoutElement = bubble.AddComponent<LayoutElement>();
            bubbleLayoutElement.preferredWidth = 300;  // 최대 너비
            bubbleLayoutElement.flexibleWidth = 0;

            // Bubble에 ContentSizeFitter 추가 (Content 크기에 맞춤)
            var bubbleFitter = bubble.AddComponent<ContentSizeFitter>();
            bubbleFitter.horizontalFit = ContentSizeFitter.FitMode.PreferredSize;
            bubbleFitter.verticalFit = ContentSizeFitter.FitMode.PreferredSize;

            // Bubble에 VerticalLayoutGroup 추가 (내부 Content 배치)
            var bubbleLayout = bubble.AddComponent<VerticalLayoutGroup>();
            bubbleLayout.childForceExpandWidth = false;
            bubbleLayout.childForceExpandHeight = false;
            bubbleLayout.childControlWidth = true;
            bubbleLayout.childControlHeight = true;
            bubbleLayout.padding = new RectOffset(10, 10, 8, 8);

            // === Content Container ===
            var content = CreateUIObject("Content", bubble.transform);
            var contentRect = content.GetComponent<RectTransform>();

            var contentLayout = content.AddComponent<VerticalLayoutGroup>();
            contentLayout.childForceExpandWidth = false;
            contentLayout.childForceExpandHeight = false;
            contentLayout.childControlWidth = true;
            contentLayout.childControlHeight = true;
            contentLayout.spacing = 4;

            // === Nickname Text ===
            var nickname = CreateTMPTextObject("Nickname", content.transform, "Username",
                config?.NicknameFontSize ?? 12, TextAlignmentOptions.TopLeft);
            var nicknameTMP = nickname.GetComponent<TextMeshProUGUI>();
            nicknameTMP.color = config?.NicknameColor ?? new Color(0.7f, 0.7f, 0.7f);
            nicknameTMP.fontStyle = FontStyles.Bold;
            var nicknameRich = nickname.AddComponent<RichChatText>();

            var nicknameLayout = nickname.AddComponent<LayoutElement>();
            nicknameLayout.minHeight = 16;
            nicknameLayout.preferredHeight = 16;

            // === Message Text ===
            var message = CreateTMPTextObject("Message", content.transform, "Message content",
                config?.MessageFontSize ?? 14, TextAlignmentOptions.TopLeft);
            var messageTMP = message.GetComponent<TextMeshProUGUI>();
            messageTMP.color = config?.MessageColor ?? Color.white;
            messageTMP.enableWordWrapping = true;
            messageTMP.overflowMode = TextOverflowModes.Overflow;
            var messageRich = message.AddComponent<RichChatText>();

            var messageLayout = message.AddComponent<LayoutElement>();
            messageLayout.minHeight = 18;
            messageLayout.preferredWidth = 280;  // 최대 메시지 너비 (버블 패딩 고려)

            // === Timestamp Text ===
            var timestamp = CreateTMPTextObject("Timestamp", content.transform, "12:00",
                config?.TimestampFontSize ?? 10, TextAlignmentOptions.BottomRight);
            var timestampTMP = timestamp.GetComponent<TextMeshProUGUI>();
            timestampTMP.color = config?.TimestampColor ?? new Color(0.5f, 0.5f, 0.5f);

            var timestampLayout = timestamp.AddComponent<LayoutElement>();
            timestampLayout.minHeight = 14;
            timestampLayout.preferredHeight = 14;

            // RichChatMessageView 컴포넌트 추가
            var richMessageView = root.AddComponent<RichChatMessageView>();
            richMessageView.Initialize(
                nicknameRich,
                messageRich,
                timestampTMP,
                bubbleImage,
                layoutElement,
                rootLayout
            );
            richMessageView.SetChatConfig(config);

            return richMessageView;
        }

        /// <summary>
        /// RichContentManager가 씬에 없으면 자동 생성
        /// </summary>
        public static void EnsureRichContentManager()
        {
            // Instance 접근 시 자동 생성됨
            var _ = RichContentManager.Instance;
        }

        #endregion

        #region Helper Methods

        private static GameObject CreateUIObject(string name, Transform parent)
        {
            var go = new GameObject(name, typeof(RectTransform));
            if (parent != null)
            {
                go.transform.SetParent(parent, false);
                // 부모의 레이어 상속 (UI Canvas는 보통 레이어 5)
                go.layer = parent.gameObject.layer;
            }
            else
            {
                // 기본 UI 레이어 (5)
                go.layer = 5;
            }
            return go;
        }

        private static GameObject CreateTextObject(string name, Transform parent, string text, int fontSize, TextAnchor alignment)
        {
            var go = CreateUIObject(name, parent);
            var textComp = go.AddComponent<Text>();
            textComp.text = text;
            textComp.fontSize = fontSize;
            textComp.alignment = alignment;
            textComp.color = Color.white;
            textComp.font = Resources.GetBuiltinResource<Font>("LegacyRuntime.ttf");
            textComp.alignByGeometry = true;
            textComp.raycastTarget = false;
            return go;
        }

        /// <summary>
        /// TextMeshPro 텍스트 오브젝트 생성
        /// </summary>
        private static GameObject CreateTMPTextObject(string name, Transform parent, string text,
            int fontSize, TextAlignmentOptions alignment)
        {
            var go = CreateUIObject(name, parent);
            var tmp = go.AddComponent<TextMeshProUGUI>();
            tmp.text = text;
            tmp.fontSize = fontSize;
            tmp.alignment = alignment;
            tmp.color = Color.white;
            // TMP 기본 폰트 사용 (프로젝트에 설정된 폰트 에셋)
            return go;
        }

        #endregion
    }
}
