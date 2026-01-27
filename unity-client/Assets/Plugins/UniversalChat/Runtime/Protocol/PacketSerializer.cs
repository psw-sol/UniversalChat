using System;
using Google.Protobuf;
using Chat.Protocol;

namespace UniversalChat.Protocol
{
    /// <summary>
    /// Protobuf 기반 패킷 직렬화/역직렬화 클래스
    /// </summary>
    public class PacketSerializer
    {
        /// <summary>
        /// Protobuf 메시지를 패킷 바이트 배열로 직렬화
        /// </summary>
        public byte[] Serialize(PacketType type, IMessage message)
        {
            byte[] bodyBytes = message.ToByteArray();
            uint totalLength = (uint)(PacketHeader.Size + bodyBytes.Length);

            byte[] packet = new byte[totalLength];

            // Write header
            var header = new PacketHeader(totalLength, type);
            Array.Copy(header.ToBytes(), 0, packet, 0, PacketHeader.Size);

            // Write body
            Array.Copy(bodyBytes, 0, packet, PacketHeader.Size, bodyBytes.Length);

            return packet;
        }

        /// <summary>
        /// 바이트 배열을 Protobuf 메시지로 역직렬화
        /// </summary>
        public T Deserialize<T>(byte[] data) where T : IMessage<T>, new()
        {
            if (data == null || data.Length == 0)
            {
                return default;
            }

            var parser = new MessageParser<T>(() => new T());
            return parser.ParseFrom(data);
        }

        /// <summary>
        /// 패킷 타입에 따라 적절한 메시지로 역직렬화
        /// </summary>
        public IMessage DeserializeByType(PacketType type, byte[] data)
        {
            if (data == null || data.Length == 0)
            {
                return null;
            }

            return type switch
            {
                // Connection
                PacketType.Heartbeat => Heartbeat.Parser.ParseFrom(data),
                PacketType.HeartbeatAck => HeartbeatAck.Parser.ParseFrom(data),

                // Authentication
                PacketType.AuthRequest => AuthRequest.Parser.ParseFrom(data),
                PacketType.AuthResponse => AuthResponse.Parser.ParseFrom(data),

                // Channel
                PacketType.ChannelListRequest => ChannelListRequest.Parser.ParseFrom(data),
                PacketType.ChannelListResponse => ChannelListResponse.Parser.ParseFrom(data),
                PacketType.ChannelJoin => ChannelJoin.Parser.ParseFrom(data),
                PacketType.ChannelJoinAck => ChannelJoinAck.Parser.ParseFrom(data),
                PacketType.ChannelLeave => ChannelLeave.Parser.ParseFrom(data),
                PacketType.ChannelLeaveAck => ChannelLeaveAck.Parser.ParseFrom(data),
                PacketType.ChannelMemberUpdate => ChannelMemberUpdate.Parser.ParseFrom(data),
                PacketType.ChannelAutoAssign => ChannelAutoAssign.Parser.ParseFrom(data),
                PacketType.ChannelAutoAssignAck => ChannelAutoAssignAck.Parser.ParseFrom(data),

                // Message
                PacketType.MessageSend => MessageSend.Parser.ParseFrom(data),
                PacketType.MessageReceive => MessageReceive.Parser.ParseFrom(data),
                PacketType.MessageAck => MessageAck.Parser.ParseFrom(data),

                // Whisper
                PacketType.WhisperSend => WhisperSend.Parser.ParseFrom(data),
                PacketType.WhisperReceive => WhisperReceive.Parser.ParseFrom(data),
                PacketType.WhisperAck => WhisperAck.Parser.ParseFrom(data),

                // Profile
                PacketType.ProfileUpdateRequest => ProfileUpdateRequest.Parser.ParseFrom(data),
                PacketType.ProfileUpdateResponse => ProfileUpdateResponse.Parser.ParseFrom(data),
                PacketType.ProfileChanged => ProfileChanged.Parser.ParseFrom(data),

                // Error
                PacketType.Error => Error.Parser.ParseFrom(data),

                _ => null
            };
        }
    }
}
