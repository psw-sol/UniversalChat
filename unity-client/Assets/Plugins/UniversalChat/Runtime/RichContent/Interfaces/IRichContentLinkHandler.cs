namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 링크 클릭 핸들러 인터페이스
    /// 게임에서 구현하여 링크 클릭/롱프레스 시 동작 정의
    ///
    /// 예시:
    /// - ItemLinkHandler: 아이템 클릭 → 아이템 정보 팝업
    /// - UserLinkHandler: 유저 클릭 → 프로필 팝업 또는 귓속말
    /// </summary>
    public interface IRichContentLinkHandler
    {
        /// <summary>
        /// 이 Handler가 처리하는 링크 타입
        /// </summary>
        string LinkType { get; }

        /// <summary>
        /// 링크 클릭 시 호출
        /// </summary>
        /// <param name="linkData">클릭된 링크 데이터</param>
        void OnLinkClicked(RichLinkData linkData);

        /// <summary>
        /// 링크 롱프레스 시 호출 (선택적 구현)
        /// 기본 구현: 아무 동작 없음
        /// </summary>
        /// <param name="linkData">롱프레스된 링크 데이터</param>
        void OnLinkLongPressed(RichLinkData linkData)
        {
            // 기본 구현: 아무 동작 없음
        }
    }
}
