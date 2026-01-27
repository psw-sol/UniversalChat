using System;
using System.Collections.Generic;
using UnityEngine;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 시스템 중앙 관리자 (싱글톤)
    /// 게임에서 Provider/Handler를 등록하면 자동으로 연동
    /// </summary>
    public class RichContentManager : MonoBehaviour
    {
        private static RichContentManager _instance;

        /// <summary>
        /// 싱글톤 인스턴스 (없으면 자동 생성)
        /// </summary>
        public static RichContentManager Instance
        {
            get
            {
                if (_instance == null)
                {
                    _instance = FindFirstObjectByType<RichContentManager>();

                    if (_instance == null)
                    {
                        var go = new GameObject("[RichContentManager]");
                        _instance = go.AddComponent<RichContentManager>();
                        DontDestroyOnLoad(go);
                    }
                }
                return _instance;
            }
        }

        /// <summary>
        /// 인스턴스가 존재하는지 확인 (자동 생성 없이)
        /// </summary>
        public static bool HasInstance => _instance != null;

        [SerializeField]
        private RichContentConfig _config;

        /// <summary>
        /// 현재 설정
        /// </summary>
        public RichContentConfig Config => _config;

        // 등록된 Provider (Type -> Provider)
        private readonly Dictionary<string, IRichContentDataProvider> _providers
            = new Dictionary<string, IRichContentDataProvider>(StringComparer.OrdinalIgnoreCase);

        // 등록된 Handler (Type -> Handler)
        private readonly Dictionary<string, IRichContentLinkHandler> _handlers
            = new Dictionary<string, IRichContentLinkHandler>(StringComparer.OrdinalIgnoreCase);

        // 팝업 팩토리
        private IRichContentPopupFactory _popupFactory;

        // 오디오 소스 (클릭 사운드용)
        private AudioSource _audioSource;

        /// <summary>
        /// 링크 클릭 이벤트 (추가 처리가 필요한 경우 구독)
        /// </summary>
        public event Action<RichLinkData> OnLinkClicked;

        /// <summary>
        /// 링크 롱프레스 이벤트
        /// </summary>
        public event Action<RichLinkData, Vector2> OnLinkLongPressed;

        #region Unity Lifecycle

        private void Awake()
        {
            if (_instance != null && _instance != this)
            {
                Destroy(gameObject);
                return;
            }

            _instance = this;
            DontDestroyOnLoad(gameObject);

            // 오디오 소스 초기화
            _audioSource = gameObject.AddComponent<AudioSource>();
            _audioSource.playOnAwake = false;
        }

        private void OnDestroy()
        {
            if (_instance == this)
            {
                _instance = null;
            }
        }

        #endregion

        #region 설정 API

        /// <summary>
        /// 설정 변경
        /// </summary>
        public void SetConfig(RichContentConfig config)
        {
            _config = config;
        }

        #endregion

        #region Provider 등록 API

        /// <summary>
        /// Data Provider 등록
        /// </summary>
        public void RegisterProvider(IRichContentDataProvider provider)
        {
            if (provider == null)
            {
                Debug.LogWarning("[RichContent] Cannot register null provider");
                return;
            }

            string type = provider.LinkType?.ToUpperInvariant();
            if (string.IsNullOrEmpty(type))
            {
                Debug.LogWarning("[RichContent] Provider has null or empty LinkType");
                return;
            }

            _providers[type] = provider;
            Debug.Log($"[RichContent] Provider registered for type: {type}");
        }

        /// <summary>
        /// Data Provider 해제
        /// </summary>
        public void UnregisterProvider(string linkType)
        {
            if (string.IsNullOrEmpty(linkType))
                return;

            _providers.Remove(linkType.ToUpperInvariant());
        }

        /// <summary>
        /// 특정 타입의 Provider 가져오기
        /// </summary>
        public IRichContentDataProvider GetProvider(string linkType)
        {
            if (string.IsNullOrEmpty(linkType))
                return null;

            _providers.TryGetValue(linkType.ToUpperInvariant(), out var provider);
            return provider;
        }

        #endregion

        #region Handler 등록 API

        /// <summary>
        /// Link Handler 등록
        /// </summary>
        public void RegisterHandler(IRichContentLinkHandler handler)
        {
            if (handler == null)
            {
                Debug.LogWarning("[RichContent] Cannot register null handler");
                return;
            }

            string type = handler.LinkType?.ToUpperInvariant();
            if (string.IsNullOrEmpty(type))
            {
                Debug.LogWarning("[RichContent] Handler has null or empty LinkType");
                return;
            }

            _handlers[type] = handler;
            Debug.Log($"[RichContent] Handler registered for type: {type}");
        }

        /// <summary>
        /// Link Handler 해제
        /// </summary>
        public void UnregisterHandler(string linkType)
        {
            if (string.IsNullOrEmpty(linkType))
                return;

            _handlers.Remove(linkType.ToUpperInvariant());
        }

        /// <summary>
        /// 특정 타입의 Handler 가져오기
        /// </summary>
        public IRichContentLinkHandler GetHandler(string linkType)
        {
            if (string.IsNullOrEmpty(linkType))
                return null;

            _handlers.TryGetValue(linkType.ToUpperInvariant(), out var handler);
            return handler;
        }

        #endregion

        #region Popup Factory API

        /// <summary>
        /// Popup Factory 설정
        /// </summary>
        public void SetPopupFactory(IRichContentPopupFactory factory)
        {
            _popupFactory = factory;
        }

        /// <summary>
        /// 팝업 표시
        /// </summary>
        public void ShowPopup(string popupType, RichLinkData linkData, Vector2 screenPosition)
        {
            _popupFactory?.ShowPopup(popupType, linkData, screenPosition);
        }

        /// <summary>
        /// 팝업 닫기
        /// </summary>
        public void HidePopup()
        {
            _popupFactory?.HidePopup();
        }

        #endregion

        #region 메시지 처리 API

        /// <summary>
        /// 원본 메시지를 TMP Rich Text로 변환
        /// </summary>
        /// <param name="rawMessage">원본 메시지 (태그 포함)</param>
        /// <returns>TMP Rich Text로 변환된 메시지</returns>
        public string ProcessMessage(string rawMessage)
        {
            if (string.IsNullOrEmpty(rawMessage))
                return rawMessage;

            if (!RichTextParser.ContainsLinks(rawMessage))
                return rawMessage;

            return RichTextParser.ConvertToTMPRichText(
                rawMessage,
                GetDisplayText,
                GetLinkColor,
                _config?.DefaultLinkColor
            );
        }

        /// <summary>
        /// 링크 클릭 처리 (RichChatText에서 호출)
        /// </summary>
        public void HandleLinkClick(string linkId)
        {
            var linkData = RichTextParser.DecodeLinkId(linkId);
            if (!linkData.HasValue)
            {
                Debug.LogWarning($"[RichContent] Failed to decode link ID: {linkId}");
                return;
            }

            var data = linkData.Value;

            // 사운드 재생
            PlayClickSound();

            // 이벤트 발생
            OnLinkClicked?.Invoke(data);

            // 등록된 핸들러 호출
            if (_handlers.TryGetValue(data.Type.ToUpperInvariant(), out var handler))
            {
                handler.OnLinkClicked(data);
            }
            else
            {
                Debug.LogWarning($"[RichContent] No handler registered for type: {data.Type}");
            }
        }

        /// <summary>
        /// 링크 롱프레스 처리 (RichChatText에서 호출)
        /// </summary>
        public void HandleLinkLongPress(string linkId, Vector2 screenPosition)
        {
            var linkData = RichTextParser.DecodeLinkId(linkId);
            if (!linkData.HasValue)
            {
                Debug.LogWarning($"[RichContent] Failed to decode link ID: {linkId}");
                return;
            }

            var data = linkData.Value;

            // 햅틱 피드백
            if (_config?.EnableHapticFeedback == true)
            {
#if UNITY_ANDROID || UNITY_IOS
                Handheld.Vibrate();
#endif
            }

            // 이벤트 발생
            OnLinkLongPressed?.Invoke(data, screenPosition);

            // 등록된 핸들러 호출
            if (_handlers.TryGetValue(data.Type.ToUpperInvariant(), out var handler))
            {
                handler.OnLinkLongPressed(data);
            }
        }

        #endregion

        #region Internal Methods

        private string GetDisplayText(RichLinkData linkData)
        {
            if (_providers.TryGetValue(linkData.Type.ToUpperInvariant(), out var provider))
            {
                try
                {
                    return provider.GetDisplayText(linkData);
                }
                catch (Exception ex)
                {
                    Debug.LogError($"[RichContent] Provider.GetDisplayText error: {ex.Message}");
                }
            }

            // Provider가 없거나 에러 시 원본 태그 반환
            return linkData.OriginalTag;
        }

        private Color? GetLinkColor(RichLinkData linkData)
        {
            if (_providers.TryGetValue(linkData.Type.ToUpperInvariant(), out var provider))
            {
                try
                {
                    var color = provider.GetLinkColor(linkData);
                    if (color.HasValue)
                        return color;
                }
                catch (Exception ex)
                {
                    Debug.LogError($"[RichContent] Provider.GetLinkColor error: {ex.Message}");
                }
            }

            // Provider가 색상을 지정하지 않은 경우 Config에서 타입별 기본 색상 반환
            return _config?.GetDefaultColorForType(linkData.Type);
        }

        private void PlayClickSound()
        {
            if (_config?.EnableClickSound != true)
                return;

            if (_config.LinkClickSound != null && _audioSource != null)
            {
                _audioSource.PlayOneShot(_config.LinkClickSound);
            }
        }

        #endregion

        #region Utility Methods

        /// <summary>
        /// 모든 Provider/Handler 초기화 (테스트용)
        /// </summary>
        public void ClearAll()
        {
            _providers.Clear();
            _handlers.Clear();
            _popupFactory = null;
        }

        /// <summary>
        /// 등록된 Provider 타입 목록
        /// </summary>
        public IEnumerable<string> GetRegisteredProviderTypes()
        {
            return _providers.Keys;
        }

        /// <summary>
        /// 등록된 Handler 타입 목록
        /// </summary>
        public IEnumerable<string> GetRegisteredHandlerTypes()
        {
            return _handlers.Keys;
        }

        #endregion
    }
}
