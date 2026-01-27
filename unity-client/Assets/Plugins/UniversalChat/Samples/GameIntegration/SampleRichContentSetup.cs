using UnityEngine;
using UniversalChat.RichContent;

namespace UniversalChat.Samples
{
    /// <summary>
    /// Rich Content 시스템 초기 설정 예제
    /// 게임 시작 시 호출하여 Provider/Handler를 등록
    ///
    /// 사용 방법:
    /// 1. 이 컴포넌트를 씬에 추가하거나
    /// 2. 게임 초기화 코드에서 SetupRichContent() 호출
    /// </summary>
    public class SampleRichContentSetup : MonoBehaviour
    {
        [Header("Settings")]
        [Tooltip("Start()에서 자동으로 설정")]
        [SerializeField] private bool _setupOnStart = true;

        private void Start()
        {
            if (_setupOnStart)
            {
                SetupRichContent();
            }
        }

        /// <summary>
        /// Rich Content Provider/Handler 등록
        /// </summary>
        public void SetupRichContent()
        {
            var manager = RichContentManager.Instance;

            // Provider 등록 (링크 데이터 → 표시 텍스트)
            manager.RegisterProvider(new SampleItemDataProvider());
            manager.RegisterProvider(new SampleUserDataProvider());

            // Handler 등록 (링크 클릭 시 동작)
            manager.RegisterHandler(new SampleItemLinkHandler());
            manager.RegisterHandler(new SampleUserLinkHandler());

            Debug.Log("[Sample] Rich Content providers and handlers registered.");
        }
    }

    /// <summary>
    /// 아이템 데이터 Provider 예제
    /// 실제 게임에서는 아이템 DB/ScriptableObject에서 데이터 조회
    /// </summary>
    public class SampleItemDataProvider : IRichContentDataProvider
    {
        public string LinkType => "ITEM";

        public string GetDisplayText(RichLinkData linkData)
        {
            // [ITEM:itemId:enhancement] 형식
            string itemId = linkData.Param1;
            int enhancement = linkData.GetParamAsInt(1, 0);

            // 실제 구현: 아이템 테이블에서 이름 조회
            string itemName = GetItemName(itemId);

            if (enhancement > 0)
            {
                return $"[{itemName} +{enhancement}]";
            }
            return $"[{itemName}]";
        }

        public Color? GetLinkColor(RichLinkData linkData)
        {
            // 아이템 등급에 따른 색상 반환
            string itemId = linkData.Param1;
            int rarity = GetItemRarity(itemId);

            // RichContentConfig의 등급 색상 사용
            var config = RichContentManager.Instance?.Config;
            if (config != null)
            {
                return config.GetRarityColor(rarity);
            }

            // Config가 없으면 기본 색상
            return rarity switch
            {
                4 => new Color(1f, 0.5f, 0f),     // 전설 (오렌지)
                3 => new Color(0.6f, 0.2f, 0.8f), // 에픽 (보라)
                2 => new Color(0.2f, 0.5f, 1f),   // 레어 (파랑)
                1 => new Color(0.2f, 0.8f, 0.2f), // 고급 (녹색)
                _ => null  // 기본 색상 사용
            };
        }

        // ============================================
        // 아래 메서드들은 실제 게임에서 구현 필요
        // ============================================

        /// <summary>
        /// 아이템 ID로 이름 조회 (더미 구현)
        /// 실제: ItemDatabase.GetItem(itemId)?.Name
        /// </summary>
        private string GetItemName(string itemId)
        {
            // 더미 데이터
            return itemId switch
            {
                "1001" => "전설의 검",
                "1002" => "영웅의 방패",
                "2001" => "체력 물약",
                "3001" => "마법 지팡이",
                _ => $"아이템_{itemId}"
            };
        }

        /// <summary>
        /// 아이템 ID로 등급 조회 (더미 구현)
        /// 실제: ItemDatabase.GetItem(itemId)?.Rarity
        /// </summary>
        private int GetItemRarity(string itemId)
        {
            // 더미: ID 첫 자리로 등급 결정
            if (itemId?.StartsWith("1") == true) return 4; // 전설
            if (itemId?.StartsWith("2") == true) return 2; // 레어
            if (itemId?.StartsWith("3") == true) return 3; // 에픽
            return 0; // 일반
        }
    }

    /// <summary>
    /// 유저 데이터 Provider 예제
    /// </summary>
    public class SampleUserDataProvider : IRichContentDataProvider
    {
        public string LinkType => "USER";

        public string GetDisplayText(RichLinkData linkData)
        {
            // [USER:userId:displayName] 형식
            // displayName이 있으면 사용, 없으면 userId 사용
            return linkData.Param2 ?? linkData.Param1 ?? "Unknown";
        }

        public Color? GetLinkColor(RichLinkData linkData)
        {
            // Config에서 유저 링크 색상 사용
            return RichContentManager.Instance?.Config?.UserLinkColor;
        }
    }

    /// <summary>
    /// 아이템 링크 클릭 Handler 예제
    /// </summary>
    public class SampleItemLinkHandler : IRichContentLinkHandler
    {
        public string LinkType => "ITEM";

        public void OnLinkClicked(RichLinkData linkData)
        {
            string itemId = linkData.Param1;
            int enhancement = linkData.GetParamAsInt(1, 0);

            Debug.Log($"[Sample] Item clicked: ID={itemId}, Enhancement=+{enhancement}");

            // ============================================
            // 실제 게임에서 구현 예시:
            // ============================================
            // 방법 1: 팝업 매니저 사용
            // GameUIManager.Instance.ShowItemInfoPopup(itemId, enhancement);

            // 방법 2: RichContent PopupFactory 사용
            // RichContentManager.Instance.ShowPopup("ITEM", linkData, Input.mousePosition);

            // 방법 3: 이벤트 발생
            // EventBus.Publish(new ItemLinkClickedEvent(itemId, enhancement));
        }

        public void OnLinkLongPressed(RichLinkData linkData)
        {
            string itemId = linkData.Param1;
            Debug.Log($"[Sample] Item long-pressed: ID={itemId}");

            // 실제 구현: 아이템 미리보기 또는 액션 메뉴 표시
            // GameUIManager.Instance.ShowItemQuickMenu(itemId);
        }
    }

    /// <summary>
    /// 유저 링크 클릭 Handler 예제
    /// </summary>
    public class SampleUserLinkHandler : IRichContentLinkHandler
    {
        public string LinkType => "USER";

        public void OnLinkClicked(RichLinkData linkData)
        {
            string oderId = linkData.Param1;
            string displayName = linkData.Param2;

            Debug.Log($"[Sample] User clicked: ID={oderId}, Name={displayName}");

            // ============================================
            // 실제 게임에서 구현 예시:
            // ============================================
            // 방법 1: 유저 프로필 팝업
            // GameUIManager.Instance.ShowUserProfile(oderId);

            // 방법 2: 귓속말 시작
            // ChatManager.Instance.StartWhisper(oderId, displayName);

            // 방법 3: 액션 메뉴 (프로필 보기, 귓속말, 친구 추가 등)
            // GameUIManager.Instance.ShowUserActionMenu(oderId, displayName);
        }

        public void OnLinkLongPressed(RichLinkData linkData)
        {
            string oderId = linkData.Param1;
            Debug.Log($"[Sample] User long-pressed: ID={oderId}");

            // 실제 구현: 유저 액션 메뉴 표시
        }
    }
}
