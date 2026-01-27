using UnityEngine;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 팝업 팩토리 인터페이스
    /// 게임에서 구현하여 링크 관련 팝업 UI 생성/표시
    ///
    /// 예시:
    /// - ShowPopup("ITEM", linkData, pos) → 아이템 정보 팝업
    /// - ShowPopup("USER", linkData, pos) → 유저 프로필 팝업
    /// </summary>
    public interface IRichContentPopupFactory
    {
        /// <summary>
        /// 팝업 표시
        /// </summary>
        /// <param name="popupType">팝업 타입 (보통 링크 타입과 동일)</param>
        /// <param name="linkData">링크 데이터</param>
        /// <param name="screenPosition">팝업을 표시할 화면 위치</param>
        void ShowPopup(string popupType, RichLinkData linkData, Vector2 screenPosition);

        /// <summary>
        /// 현재 표시 중인 팝업 닫기
        /// </summary>
        void HidePopup();

        /// <summary>
        /// 현재 팝업이 표시 중인지 확인
        /// </summary>
        bool IsPopupVisible { get; }
    }
}
