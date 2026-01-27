using UnityEngine;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 데이터 제공자 인터페이스
    /// 게임에서 구현하여 링크 데이터를 표시용 텍스트로 변환
    ///
    /// 예시:
    /// - ItemDataProvider: [ITEM:1001:5] → "전설의 검 +5"
    /// - UserDataProvider: [USER:12345:홍길동] → "홍길동"
    /// </summary>
    public interface IRichContentDataProvider
    {
        /// <summary>
        /// 이 Provider가 처리하는 링크 타입
        /// 예: "ITEM", "USER", "GUILD", "QUEST"
        /// </summary>
        string LinkType { get; }

        /// <summary>
        /// 링크 데이터를 표시용 텍스트로 변환
        /// </summary>
        /// <param name="linkData">파싱된 링크 데이터</param>
        /// <returns>화면에 표시될 텍스트 (예: "[전설의 검 +5]")</returns>
        string GetDisplayText(RichLinkData linkData);

        /// <summary>
        /// 링크에 적용할 색상 반환
        /// </summary>
        /// <param name="linkData">파싱된 링크 데이터</param>
        /// <returns>색상 (null이면 기본 링크 색상 사용)</returns>
        Color? GetLinkColor(RichLinkData linkData);
    }
}
