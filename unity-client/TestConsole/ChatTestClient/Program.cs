using System;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Google.Protobuf;
using Chat.Protocol;

namespace ChatTestClient;

class Program
{
    static async Task Main(string[] args)
    {
        Console.WriteLine("=== Universal Chat Test Client (Protobuf) ===\n");

        string host = "localhost";
        int port = 7777;

        Console.WriteLine($"Connecting to {host}:{port}...");

        try
        {
            using var client = new TcpClient();
            await client.ConnectAsync(host, port);
            Console.WriteLine("✓ Connected!\n");

            var stream = client.GetStream();

            // Test 1: Authentication
            Console.WriteLine("Test 1: Authentication");
            var authRequest = new AuthRequest
            {
                UserId = "test_user_123",
                Nickname = "TestPlayer",
                ClientVersion = "1.0.0",
                DeviceId = "test-device-001"
            };
            await SendPacket(stream, 0x0101, authRequest);
            var (authType, authData) = await ReceivePacket(stream);
            Console.WriteLine($"  Response Type: 0x{authType:X4}");
            if (authType == 0x0102)
            {
                var authResponse = AuthResponse.Parser.ParseFrom(authData);
                Console.WriteLine($"  Success: {authResponse.Success}");
                Console.WriteLine($"  SessionId: {authResponse.SessionId}");
                Console.WriteLine($"  HeartbeatInterval: {authResponse.HeartbeatInterval}s");
            }
            else if (authType == 0xFF01)
            {
                var error = Error.Parser.ParseFrom(authData);
                Console.WriteLine($"  Error: {error.ErrorMessage}");
            }
            Console.WriteLine();

            // Test 2: Channel List
            Console.WriteLine("Test 2: Request Channel List");
            var listRequest = new ChannelListRequest { IncludeMemberCount = true };
            await SendPacket(stream, 0x0201, listRequest);
            var (listType, listData) = await ReceivePacket(stream);
            Console.WriteLine($"  Response Type: 0x{listType:X4}");
            if (listType == 0x0202)
            {
                var listResponse = ChannelListResponse.Parser.ParseFrom(listData);
                Console.WriteLine($"  Channels: {listResponse.Channels.Count}");
                foreach (var ch in listResponse.Channels)
                {
                    Console.WriteLine($"    - {ch.ChannelId}: {ch.ChannelName} ({ch.MemberCount}/{ch.MaxMembers})");
                }
            }
            Console.WriteLine();

            // Test 3: Join Channel
            Console.WriteLine("Test 3: Join Channel 'world'");
            var joinRequest = new ChannelJoin { ChannelId = "world" };
            await SendPacket(stream, 0x0203, joinRequest);
            var (joinType, joinData) = await ReceivePacket(stream);
            Console.WriteLine($"  Response Type: 0x{joinType:X4}");
            if (joinType == 0x0204)
            {
                var joinResponse = ChannelJoinAck.Parser.ParseFrom(joinData);
                Console.WriteLine($"  Success: {joinResponse.Success}");
                Console.WriteLine($"  ChannelId: {joinResponse.ChannelId}");
                Console.WriteLine($"  Members: {joinResponse.Members.Count}");
            }
            Console.WriteLine();

            // Test 4: Send Message
            Console.WriteLine("Test 4: Send Message");
            var msgRequest = new MessageSend
            {
                ChannelId = "world",
                Content = "Hello from Protobuf test client!",
                MessageType = MessageType.Text,
                ClientMessageId = Guid.NewGuid().ToString()
            };
            await SendPacket(stream, 0x0301, msgRequest);

            // Wait for message ack and broadcast
            Console.WriteLine("  Waiting for message ack/broadcast...");
            var (msgType1, msgData1) = await ReceivePacket(stream);
            Console.WriteLine($"  Response 1 Type: 0x{msgType1:X4}");

            if (msgType1 == 0x0303) // MessageAck
            {
                var ack = MessageAck.Parser.ParseFrom(msgData1);
                Console.WriteLine($"  Ack Success: {ack.Success}, MessageId: {ack.MessageId}");
            }
            else if (msgType1 == 0x0302) // MessageReceive
            {
                var recv = MessageReceive.Parser.ParseFrom(msgData1);
                Console.WriteLine($"  Received: [{recv.Message.SenderNickname}] {recv.Message.Content}");
            }
            Console.WriteLine();

            // Test 5: Heartbeat
            Console.WriteLine("Test 5: Heartbeat");
            var heartbeat = new Heartbeat { ClientTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() };
            await SendPacket(stream, 0x0001, heartbeat);
            var (hbType, hbData) = await ReceivePacket(stream);
            Console.WriteLine($"  Response Type: 0x{hbType:X4}");
            if (hbType == 0x0002)
            {
                var hbAck = HeartbeatAck.Parser.ParseFrom(hbData);
                Console.WriteLine($"  Server Time: {hbAck.ServerTime}");
            }
            Console.WriteLine();

            // Test 6: Leave Channel
            Console.WriteLine("Test 6: Leave Channel");
            var leaveRequest = new ChannelLeave { ChannelId = "world" };
            await SendPacket(stream, 0x0205, leaveRequest);
            var (leaveType, leaveData) = await ReceivePacket(stream);
            Console.WriteLine($"  Response Type: 0x{leaveType:X4}");
            if (leaveType == 0x0206)
            {
                var leaveResponse = ChannelLeaveAck.Parser.ParseFrom(leaveData);
                Console.WriteLine($"  Success: {leaveResponse.Success}");
            }
            Console.WriteLine();

            Console.WriteLine("=== All Tests Completed Successfully! ===");
        }
        catch (Exception ex)
        {
            Console.WriteLine($"✗ Error: {ex.Message}");
            Console.WriteLine(ex.StackTrace);
            return;
        }
    }

    static async Task SendPacket(NetworkStream stream, ushort type, IMessage message)
    {
        byte[] bodyBytes = message.ToByteArray();
        uint totalLength = (uint)(8 + bodyBytes.Length);
        byte[] packet = new byte[totalLength];

        // Header: Length (4 bytes) + Type (2 bytes) + Reserved (2 bytes)
        packet[0] = (byte)(totalLength & 0xFF);
        packet[1] = (byte)((totalLength >> 8) & 0xFF);
        packet[2] = (byte)((totalLength >> 16) & 0xFF);
        packet[3] = (byte)((totalLength >> 24) & 0xFF);
        packet[4] = (byte)(type & 0xFF);
        packet[5] = (byte)((type >> 8) & 0xFF);
        packet[6] = 0;
        packet[7] = 0;

        Array.Copy(bodyBytes, 0, packet, 8, bodyBytes.Length);

        await stream.WriteAsync(packet);
        await stream.FlushAsync();

        Console.WriteLine($"  → Sent: Type=0x{type:X4}, Size={bodyBytes.Length} bytes");
    }

    static async Task<(ushort type, byte[] body)> ReceivePacket(NetworkStream stream)
    {
        // Read header
        byte[] header = new byte[8];
        int totalRead = 0;
        while (totalRead < 8)
        {
            int read = await stream.ReadAsync(header, totalRead, 8 - totalRead);
            if (read == 0) throw new Exception("Connection closed");
            totalRead += read;
        }

        uint length = (uint)(header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24));
        ushort type = (ushort)(header[4] | (header[5] << 8));

        // Read body
        int bodyLength = (int)length - 8;
        byte[] bodyBytes = Array.Empty<byte>();

        if (bodyLength > 0)
        {
            bodyBytes = new byte[bodyLength];
            totalRead = 0;
            while (totalRead < bodyLength)
            {
                int read = await stream.ReadAsync(bodyBytes, totalRead, bodyLength - totalRead);
                if (read == 0) throw new Exception("Connection closed");
                totalRead += read;
            }
        }

        Console.WriteLine($"  ← Recv: Type=0x{type:X4}, Size={bodyLength} bytes");
        return (type, bodyBytes);
    }
}
