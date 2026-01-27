using System;

namespace UniversalChat.Protocol
{
    /// <summary>
    /// 패킷 헤더 구조 (8 bytes)
    /// [0-3] Length (uint32, little-endian) - 전체 패킷 길이 (헤더 포함)
    /// [4-5] Type (uint16, little-endian) - 패킷 타입
    /// [6-7] Reserved (uint16) - 예약됨
    /// </summary>
    public struct PacketHeader
    {
        public const int Size = 8;

        public uint Length;      // 전체 패킷 길이 (헤더 + 바디)
        public PacketType Type;  // 패킷 타입
        public ushort Reserved;  // 예약 필드

        public PacketHeader(uint length, PacketType type)
        {
            Length = length;
            Type = type;
            Reserved = 0;
        }

        public byte[] ToBytes()
        {
            var bytes = new byte[Size];

            // Length (4 bytes, little-endian)
            bytes[0] = (byte)(Length & 0xFF);
            bytes[1] = (byte)((Length >> 8) & 0xFF);
            bytes[2] = (byte)((Length >> 16) & 0xFF);
            bytes[3] = (byte)((Length >> 24) & 0xFF);

            // Type (2 bytes, little-endian)
            ushort typeValue = (ushort)Type;
            bytes[4] = (byte)(typeValue & 0xFF);
            bytes[5] = (byte)((typeValue >> 8) & 0xFF);

            // Reserved (2 bytes)
            bytes[6] = 0;
            bytes[7] = 0;

            return bytes;
        }

        public static PacketHeader FromBytes(byte[] data)
        {
            if (data == null || data.Length < Size)
            {
                throw new ArgumentException("Invalid header data");
            }

            return new PacketHeader
            {
                Length = (uint)(data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24)),
                Type = (PacketType)(data[4] | (data[5] << 8)),
                Reserved = (ushort)(data[6] | (data[7] << 8))
            };
        }
    }
}
