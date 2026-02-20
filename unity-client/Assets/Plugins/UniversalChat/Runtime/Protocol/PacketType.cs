namespace UniversalChat.Protocol
{
    /// <summary>
    /// 서버와 통신하는 패킷 타입 정의
    /// 서버의 PacketTypes.hpp와 동기화됨
    /// </summary>
    public enum PacketType : ushort
    {
        // === Connection (0x00xx) ===
        Heartbeat           = 0x0001,
        HeartbeatAck        = 0x0002,
        Disconnect          = 0x0003,

        // === Authentication (0x01xx) ===
        AuthRequest         = 0x0101,
        AuthResponse        = 0x0102,
        Logout              = 0x0103,
        LogoutAck           = 0x0104,

        // === Channel (0x02xx) ===
        ChannelListRequest  = 0x0201,
        ChannelListResponse = 0x0202,
        ChannelJoin         = 0x0203,
        ChannelJoinAck      = 0x0204,
        ChannelLeave        = 0x0205,
        ChannelLeaveAck     = 0x0206,
        ChannelMemberUpdate = 0x0207,
        ChannelCreate       = 0x0208,
        ChannelCreateAck    = 0x0209,
        ChannelAutoAssign   = 0x020A,  // 자동 채널 배정 요청
        ChannelAutoAssignAck = 0x020B, // 자동 채널 배정 응답

        // === Message (0x03xx) ===
        MessageSend         = 0x0301,
        MessageReceive      = 0x0302,
        MessageAck          = 0x0303,

        // === Whisper (0x04xx) ===
        WhisperSend         = 0x0401,
        WhisperReceive      = 0x0402,
        WhisperAck          = 0x0403,

        // === Profile (0x05xx) ===
        ProfileUpdateRequest  = 0x0501,
        ProfileUpdateResponse = 0x0502,
        ProfileChanged        = 0x0503,

        // === Announcement (0x06xx) ===
        AnnouncementSend      = 0x0601,  // 게임서버/관리자 -> 채팅서버
        AnnouncementReceive   = 0x0602,  // 채팅서버 -> 클라이언트
        AnnouncementAck       = 0x0603,  // 채팅서버 -> 게임서버

        // === UserAction (0x07xx) ===
        UserActionNotificationSend    = 0x0701,  // 게임서버 -> 채팅서버
        UserActionNotificationReceive = 0x0702,  // 채팅서버 -> 클라이언트
        UserActionNotificationAck     = 0x0703,  // 채팅서버 -> 게임서버

        // === DM (0x08xx) ===
        DMStart              = 0x0801,
        DMStartResponse      = 0x0802,
        DMListRequest        = 0x0803,
        DMListResponse       = 0x0804,
        DMMessageSend        = 0x0805,
        DMMessageReceive     = 0x0806,
        DMMessageAck         = 0x0807,
        DMReadReceipt        = 0x0808,
        DMReadReceiptNotify  = 0x0809,
        DMHistoryRequest     = 0x080A,
        DMHistoryResponse    = 0x080B,
        DMDeleteRequest      = 0x080C,
        DMDeleteResponse     = 0x080D,

        // === Error (0xFFxx) ===
        Error               = 0xFF01,

        // === Unknown ===
        Unknown             = 0xFFFF,
    }
}
