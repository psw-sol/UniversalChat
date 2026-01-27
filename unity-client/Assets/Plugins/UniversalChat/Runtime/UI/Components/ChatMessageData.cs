using System;
using UniversalChat.Core;

namespace UniversalChat.UI
{
    /// <summary>
    /// 메시지 데이터 (GameObject 없음, 순수 데이터)
    /// 가상화 스크롤에서 데이터와 뷰를 분리하기 위해 사용
    /// </summary>
    public class ChatMessageData
    {
        #region Properties

        /// <summary>
        /// 메시지 고유 ID
        /// </summary>
        public string MessageId { get; set; }

        /// <summary>
        /// 발신자 ID
        /// </summary>
        public string SenderId { get; set; }

        /// <summary>
        /// 발신자 닉네임
        /// </summary>
        public string Nickname { get; set; }

        /// <summary>
        /// 메시지 내용
        /// </summary>
        public string Content { get; set; }

        /// <summary>
        /// 타임스탬프 (Unix milliseconds)
        /// </summary>
        public long Timestamp { get; set; }

        /// <summary>
        /// 메시지 타입 (0: Normal, 1: System, 2: Whisper)
        /// </summary>
        public int MessageType { get; set; }

        /// <summary>
        /// 발신자 프로필 이미지 URL
        /// </summary>
        public string ProfileImage { get; set; }

        /// <summary>
        /// 발신자 프레임 이미지 URL
        /// </summary>
        public string FrameImage { get; set; }

        /// <summary>
        /// 발신자 추가 데이터
        /// </summary>
        public string ExtraData { get; set; }

        /// <summary>
        /// 캐시된 높이 (-1 = 미계산)
        /// 높이 계산은 비용이 크므로 한 번 계산 후 캐싱
        /// </summary>
        public float CachedHeight { get; set; } = -1f;

        /// <summary>
        /// DateTime 변환
        /// </summary>
        public DateTime DateTime => DateTimeOffset.FromUnixTimeMilliseconds(Timestamp).LocalDateTime;

        #endregion

        #region Constructors

        public ChatMessageData() { }

        /// <summary>
        /// ChannelMessage로부터 ChatMessageData 생성
        /// </summary>
        public ChatMessageData(ChannelMessage message)
        {
            MessageId = message.MessageId;
            SenderId = message.SenderId;
            Nickname = message.SenderNickname ?? message.SenderId;
            Content = message.Content;
            Timestamp = message.Timestamp;
            MessageType = message.MessageType;
            ProfileImage = message.SenderProfileImage;
            FrameImage = message.SenderFrameImage;
            ExtraData = message.SenderExtraData;
        }

        #endregion

        #region Static Methods

        /// <summary>
        /// ChannelMessage → ChatMessageData 변환
        /// </summary>
        public static ChatMessageData FromChannelMessage(ChannelMessage message)
        {
            return new ChatMessageData(message);
        }

        #endregion
    }
}
