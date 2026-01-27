using UnityEngine;

namespace UniversalChat.UI
{
    /// <summary>
    /// 채팅 UI 테마 설정을 위한 ScriptableObject
    /// 런타임 UI 빌더에서 사용됩니다.
    /// </summary>
    [CreateAssetMenu(fileName = "ChatUIConfig", menuName = "UniversalChat/UI Config")]
    public class ChatUIConfig : ScriptableObject
    {
        [Header("Background Colors")]
        public Color BackgroundColor = new Color(0.15f, 0.15f, 0.15f, 1f);
        public Color PanelBackgroundColor = new Color(0.1f, 0.1f, 0.1f, 0.95f);
        public Color InputBackgroundColor = new Color(0.2f, 0.2f, 0.2f, 1f);

        [Header("Button Colors")]
        public Color SendButtonColor = new Color(0.3f, 0.5f, 0.8f, 1f);
        public Color SendButtonHoverColor = new Color(0.4f, 0.6f, 0.9f, 1f);
        public Color SendButtonTextColor = Color.white;

        [Header("Header")]
        public Color TitleColor = Color.white;
        public Color ConnectedColor = Color.green;
        public Color DisconnectedColor = Color.red;
        public int TitleFontSize = 18;
        public int StatusFontSize = 12;

        [Header("Text Colors")]
        public Color MessageColor = Color.white;
        public Color NicknameColor = new Color(0.7f, 0.7f, 0.7f);
        public Color TimestampColor = new Color(0.5f, 0.5f, 0.5f);
        public Color SystemTextColor = new Color(1f, 0.8f, 0.2f, 1f);
        public Color ErrorTextColor = new Color(1f, 0.3f, 0.3f, 1f);
        public Color InputTextColor = Color.white;
        public Color PlaceholderColor = new Color(0.5f, 0.5f, 0.5f);

        [Header("Message Bubble Colors")]
        public Color MyBubbleColor = new Color(0.2f, 0.4f, 0.6f, 1f);
        public Color OtherBubbleColor = new Color(0.25f, 0.25f, 0.25f, 1f);
        public Color SystemBubbleColor = new Color(0.3f, 0.3f, 0.2f, 1f);

        [Header("Fonts")]
        public Font MainFont;
        public int MessageFontSize = 14;
        public int NicknameFontSize = 12;
        public int TimestampFontSize = 10;
        public int InputFontSize = 14;
        public int SendButtonFontSize = 14;

        [Header("Layout")]
        public float MessageSpacing = 5f;
        public float MessagePadding = 10f;
        public float BubbleCornerRadius = 8f;
        public float MaxMessageWidth = 300f;
        public float DefaultItemHeight = 70f;

        [Header("Input Field")]
        public string PlaceholderText = "Type a message...";
        public string SendButtonText = "Send";
        public int MaxMessageLength = 500;

        [Header("Animation")]
        public float FadeInDuration = 0.2f;
        public float ScrollAnimationDuration = 0.1f;

        [Header("Behavior")]
        public int MaxVisibleMessages = 100;
        public int MessageHistorySize = 500;
        public bool ShowTimestamps = true;
        public bool ShowUserAvatars = false;
        public bool EnableSoundEffects = true;
        public bool AutoScrollToBottom = true;

        [Header("Sound Effects")]
        public AudioClip MessageReceivedSound;
        public AudioClip MessageSentSound;
        public AudioClip ErrorSound;
    }
}
