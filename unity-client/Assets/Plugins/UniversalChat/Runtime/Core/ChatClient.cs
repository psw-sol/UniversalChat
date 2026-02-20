using System;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UniversalChat.Network;
using UniversalChat.Protocol;
using Chat.Protocol;

namespace UniversalChat.Core
{
    /// <summary>
    /// 채팅 서버 연결 및 통신을 담당하는 메인 클라이언트 클래스
    /// Protobuf 프로토콜 기반
    /// </summary>
    public class ChatClient : IDisposable
    {
        #region Events

        public event Action OnConnected;
        public event Action<string> OnDisconnected;
        public event Action<string> OnError;
        public event Action<AuthResponse> OnAuthenticated;
        public event Action<MessageReceive> OnMessageReceived;
        public event Action<WhisperReceive> OnWhisperReceived;
        public event Action<ChannelJoinAck> OnChannelJoined;
        public event Action<ChannelLeaveAck> OnChannelLeft;
        public event Action<Chat.Protocol.ChannelListResponse> OnChannelListReceived;
        public event Action<ChannelMemberUpdate> OnMemberUpdated;
        public event Action<ProfileUpdateResponse> OnProfileUpdated;
        public event Action<ProfileChanged> OnProfileChanged;
        public event Action<ChannelAutoAssignAck> OnChannelAutoAssigned;
        public event Action<AnnouncementReceive> OnAnnouncementReceived;
        public event Action<UserActionNotificationReceive> OnUserActionNotificationReceived;

        // DM Events
        public event Action<Chat.Protocol.DMStartResponse> OnDMStartResponse;
        public event Action<Chat.Protocol.DMListResponse> OnDMListResponse;
        public event Action<Chat.Protocol.DMMessageReceive> OnDMMessageReceived;
        public event Action<Chat.Protocol.DMMessageAck> OnDMMessageAck;
        public event Action<Chat.Protocol.DMReadReceiptNotify> OnDMReadReceiptNotify;
        public event Action<Chat.Protocol.DMHistoryResponse> OnDMHistoryResponse;
        public event Action<Chat.Protocol.DMDeleteResponse> OnDMDeleteResponse;

        #endregion

        #region Properties

        public bool IsConnected => _connection?.IsConnected ?? false;
        public bool IsAuthenticated { get; private set; }
        public string UserId { get; private set; }
        public string SessionId { get; private set; }
        public string Nickname { get; private set; }
        public string ProfileImage { get; private set; }
        public string FrameImage { get; private set; }
        public string ExtraData { get; private set; }

        #endregion

        #region Fields

        private ChatConnection _connection;
        private readonly PacketSerializer _serializer;
        private CancellationTokenSource _heartbeatCts;
        private readonly float _heartbeatInterval;

        #endregion

        #region Constructor

        public ChatClient(float heartbeatInterval = 30f)
        {
            _serializer = new PacketSerializer();
            _heartbeatInterval = heartbeatInterval;
        }

        #endregion

        #region Connection Methods

        public async Task<bool> ConnectAsync(string host, int port, int timeoutMs = 5000)
        {
            try
            {
                _connection = new ChatConnection();
                _connection.OnPacketReceived += HandlePacketReceived;
                _connection.OnDisconnected += HandleDisconnected;
                _connection.OnError += HandleError;

                bool connected = await _connection.ConnectAsync(host, port, timeoutMs);

                if (connected)
                {
                    OnConnected?.Invoke();
                    StartHeartbeat();
                }

                return connected;
            }
            catch (Exception ex)
            {
                OnError?.Invoke($"Connection failed: {ex.Message}");
                return false;
            }
        }

        public void Disconnect()
        {
            StopHeartbeat();
            _connection?.Disconnect();
            IsAuthenticated = false;
            UserId = null;
            SessionId = null;
            Nickname = null;
            ProfileImage = null;
            FrameImage = null;
            ExtraData = null;
        }

        #endregion

        #region Authentication

        /// <summary>
        /// 서버에 인증 요청 (토큰 및 프로필 정보 포함)
        /// </summary>
        /// <param name="userId">사용자 ID</param>
        /// <param name="authToken">게임 서버에서 발급받은 인증 토큰</param>
        /// <param name="nickname">닉네임 (null이면 userId 사용)</param>
        /// <param name="profileImage">프로필 이미지 URL 또는 ID</param>
        /// <param name="frameImage">프레임 이미지 URL 또는 ID</param>
        /// <param name="extraData">기타 정보 (JSON 등)</param>
        public async Task<bool> AuthenticateAsync(
            string userId,
            string authToken = null,
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            if (!IsConnected)
            {
                OnError?.Invoke("Not connected to server");
                return false;
            }

            UserId = userId;
            Nickname = nickname ?? userId;
            ProfileImage = profileImage ?? string.Empty;
            FrameImage = frameImage ?? string.Empty;
            ExtraData = extraData ?? string.Empty;

            var request = new AuthRequest
            {
                UserId = userId,
                Nickname = Nickname,
                AuthToken = authToken ?? string.Empty,
                ClientVersion = Application.version,
                DeviceId = SystemInfo.deviceUniqueIdentifier,
                ProfileImage = ProfileImage,
                FrameImage = FrameImage,
                ExtraData = ExtraData
            };

            var packet = _serializer.Serialize(PacketType.AuthRequest, request);
            await _connection.SendAsync(packet);

            return true;
        }

        #endregion

        #region Profile

        /// <summary>
        /// 프로필 정보 업데이트 요청
        /// null로 전달된 필드는 변경하지 않음
        /// </summary>
        public async Task UpdateProfileAsync(
            string nickname = null,
            string profileImage = null,
            string frameImage = null,
            string extraData = null)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var request = new ProfileUpdateRequest();

            if (nickname != null)
            {
                request.Nickname = nickname;
            }
            if (profileImage != null)
            {
                request.ProfileImage = profileImage;
            }
            if (frameImage != null)
            {
                request.FrameImage = frameImage;
            }
            if (extraData != null)
            {
                request.ExtraData = extraData;
            }

            var packet = _serializer.Serialize(PacketType.ProfileUpdateRequest, request);
            await _connection.SendAsync(packet);
        }

        #endregion

        #region Channel Operations

        public async Task JoinChannelAsync(string channelId, string password = null)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var request = new ChannelJoin
            {
                ChannelId = channelId,
                Password = password ?? string.Empty
            };

            var packet = _serializer.Serialize(PacketType.ChannelJoin, request);
            await _connection.SendAsync(packet);
        }

        public async Task LeaveChannelAsync(string channelId)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var request = new ChannelLeave
            {
                ChannelId = channelId
            };

            var packet = _serializer.Serialize(PacketType.ChannelLeave, request);
            await _connection.SendAsync(packet);
        }

        public async Task RequestChannelListAsync()
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var request = new ChannelListRequest
            {
                IncludeMemberCount = true
            };

            var packet = _serializer.Serialize(PacketType.ChannelListRequest, request);
            await _connection.SendAsync(packet);
        }

        /// <summary>
        /// 서버에 자동 채널 배정 요청 (월드 채널 등)
        /// 서버가 최적의 채널을 선택하여 자동으로 가입시킴
        /// </summary>
        /// <param name="channelType">채널 타입 ("world", "trade", "help" 등)</param>
        public async Task RequestAutoAssignChannelAsync(string channelType = "world")
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var request = new ChannelAutoAssign
            {
                ChannelType = channelType
            };

            var packet = _serializer.Serialize(PacketType.ChannelAutoAssign, request);
            await _connection.SendAsync(packet);
        }

        public async Task CreateChannelAsync(string channelName, string password = null, int maxUsers = 100)
        {
            // Note: ChannelCreate 메시지가 proto에 정의되지 않음
            // 서버에서 지원 시 구현 필요
            Debug.LogWarning("[ChatClient] CreateChannelAsync: 현재 서버에서 지원하지 않습니다.");
            OnError?.Invoke("Channel creation not supported");
            await Task.CompletedTask;
        }

        public async Task RequestUserListAsync(string channelId)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            // Note: 서버에서 채널 입장 시 멤버 목록이 함께 전달됨
            // 별도의 UserListRequest가 필요한 경우 서버에 해당 패킷 타입 추가 필요
            Debug.LogWarning("[ChatClient] RequestUserListAsync: 현재 버전에서는 채널 입장 시 멤버 목록이 전달됩니다.");
            await Task.CompletedTask;
        }

        #endregion

        #region Messaging

        public async Task SendMessageAsync(string channelId, string content, MessageType messageType = MessageType.Text)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            if (string.IsNullOrWhiteSpace(content))
            {
                return;
            }

            var message = new MessageSend
            {
                ChannelId = channelId,
                Content = content,
                MessageType = messageType,
                ClientMessageId = Guid.NewGuid().ToString()
            };

            var packet = _serializer.Serialize(PacketType.MessageSend, message);
            await _connection.SendAsync(packet);
        }

        public async Task SendWhisperAsync(string targetUserId, string content)
        {
            if (!IsAuthenticated)
            {
                OnError?.Invoke("Not authenticated");
                return;
            }

            var whisper = new WhisperSend
            {
                TargetUserId = targetUserId,
                Content = content
            };

            var packet = _serializer.Serialize(PacketType.WhisperSend, whisper);
            await _connection.SendAsync(packet);
        }

        #endregion

        #region DM Operations

        public async Task SendDMStartAsync(string targetUserId)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }

            var request = new Chat.Protocol.DMStartRequest { TargetUserId = targetUserId };
            var packet = _serializer.Serialize(PacketType.DMStart, request);
            await _connection.SendAsync(packet);
        }

        public async Task SendDMListRequestAsync(int limit = 50, long beforeTimestamp = 0)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }

            var request = new Chat.Protocol.DMListRequest { Limit = limit, BeforeTimestamp = beforeTimestamp };
            var packet = _serializer.Serialize(PacketType.DMListRequest, request);
            await _connection.SendAsync(packet);
        }

        public async Task SendDMMessageAsync(string dmChannelId, string content, MessageType messageType = MessageType.Text)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }
            if (string.IsNullOrWhiteSpace(content)) return;

            var message = new Chat.Protocol.DMMessageSend
            {
                DmChannelId = dmChannelId,
                Content = content,
                MessageType = messageType,
                ClientMessageId = Guid.NewGuid().ToString()
            };

            var packet = _serializer.Serialize(PacketType.DMMessageSend, message);
            await _connection.SendAsync(packet);
        }

        public async Task SendDMReadReceiptAsync(string dmChannelId, string lastReadMessageId, long lastReadTimestamp = 0)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }

            var receipt = new Chat.Protocol.DMReadReceipt
            {
                DmChannelId = dmChannelId,
                LastReadMessageId = lastReadMessageId,
                LastReadTimestamp = lastReadTimestamp > 0 ? lastReadTimestamp : DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };

            var packet = _serializer.Serialize(PacketType.DMReadReceipt, receipt);
            await _connection.SendAsync(packet);
        }

        public async Task SendDMHistoryRequestAsync(string dmChannelId, long beforeTimestamp = 0, int limit = 30)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }

            var request = new Chat.Protocol.DMHistoryRequest
            {
                DmChannelId = dmChannelId,
                BeforeTimestamp = beforeTimestamp,
                Limit = limit
            };

            var packet = _serializer.Serialize(PacketType.DMHistoryRequest, request);
            await _connection.SendAsync(packet);
        }

        public async Task SendDMDeleteRequestAsync(string dmChannelId)
        {
            if (!IsAuthenticated) { OnError?.Invoke("Not authenticated"); return; }

            var request = new Chat.Protocol.DMDeleteRequest { DmChannelId = dmChannelId };
            var packet = _serializer.Serialize(PacketType.DMDeleteRequest, request);
            await _connection.SendAsync(packet);
        }

        #endregion

        #region Heartbeat

        private void StartHeartbeat()
        {
            StopHeartbeat();
            _heartbeatCts = new CancellationTokenSource();
            _ = HeartbeatLoopAsync(_heartbeatCts.Token);
        }

        private void StopHeartbeat()
        {
            _heartbeatCts?.Cancel();
            _heartbeatCts?.Dispose();
            _heartbeatCts = null;
        }

        private async Task HeartbeatLoopAsync(CancellationToken token)
        {
            while (!token.IsCancellationRequested && IsConnected)
            {
                try
                {
                    await Task.Delay(TimeSpan.FromSeconds(_heartbeatInterval), token);

                    if (IsConnected && !token.IsCancellationRequested)
                    {
                        var heartbeat = new Heartbeat
                        {
                            ClientTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
                        };

                        var packet = _serializer.Serialize(PacketType.Heartbeat, heartbeat);
                        await _connection.SendAsync(packet);
                    }
                }
                catch (OperationCanceledException)
                {
                    break;
                }
                catch (Exception ex)
                {
                    Debug.LogWarning($"[ChatClient] Heartbeat error: {ex.Message}");
                }
            }
        }

        #endregion

        #region Packet Handling

        private void HandlePacketReceived(PacketType type, byte[] data)
        {
            try
            {
                switch (type)
                {
                    case PacketType.AuthResponse:
                        HandleAuthResponse(data);
                        break;

                    case PacketType.MessageReceive:
                        HandleMessageReceive(data);
                        break;

                    case PacketType.ChannelJoinAck:
                        HandleChannelJoinAck(data);
                        break;

                    case PacketType.ChannelLeaveAck:
                        HandleChannelLeaveAck(data);
                        break;

                    case PacketType.ChannelListResponse:
                        HandleChannelListResponse(data);
                        break;

                    case PacketType.ChannelMemberUpdate:
                        HandleChannelMemberUpdate(data);
                        break;

                    case PacketType.ChannelAutoAssignAck:
                        HandleChannelAutoAssignAck(data);
                        break;

                    case PacketType.WhisperReceive:
                        HandleWhisperReceive(data);
                        break;

                    case PacketType.ProfileUpdateResponse:
                        HandleProfileUpdateResponse(data);
                        break;

                    case PacketType.ProfileChanged:
                        HandleProfileChanged(data);
                        break;

                    case PacketType.AnnouncementReceive:
                        HandleAnnouncementReceive(data);
                        break;

                    case PacketType.UserActionNotificationReceive:
                        HandleUserActionNotificationReceive(data);
                        break;

                    // DM packet handlers
                    case PacketType.DMStartResponse:
                        OnDMStartResponse?.Invoke(_serializer.Deserialize<Chat.Protocol.DMStartResponse>(data));
                        break;
                    case PacketType.DMListResponse:
                        OnDMListResponse?.Invoke(_serializer.Deserialize<Chat.Protocol.DMListResponse>(data));
                        break;
                    case PacketType.DMMessageReceive:
                        OnDMMessageReceived?.Invoke(_serializer.Deserialize<Chat.Protocol.DMMessageReceive>(data));
                        break;
                    case PacketType.DMMessageAck:
                        OnDMMessageAck?.Invoke(_serializer.Deserialize<Chat.Protocol.DMMessageAck>(data));
                        break;
                    case PacketType.DMReadReceiptNotify:
                        OnDMReadReceiptNotify?.Invoke(_serializer.Deserialize<Chat.Protocol.DMReadReceiptNotify>(data));
                        break;
                    case PacketType.DMHistoryResponse:
                        OnDMHistoryResponse?.Invoke(_serializer.Deserialize<Chat.Protocol.DMHistoryResponse>(data));
                        break;
                    case PacketType.DMDeleteResponse:
                        OnDMDeleteResponse?.Invoke(_serializer.Deserialize<Chat.Protocol.DMDeleteResponse>(data));
                        break;

                    case PacketType.HeartbeatAck:
                        // Heartbeat acknowledged
                        break;

                    case PacketType.Error:
                        HandleErrorResponse(data);
                        break;

                    default:
                        Debug.LogWarning($"[ChatClient] Unhandled packet type: {type}");
                        break;
                }
            }
            catch (Exception ex)
            {
                Debug.LogError($"[ChatClient] Error handling packet {type}: {ex.Message}");
            }
        }

        private void HandleAuthResponse(byte[] data)
        {
            var response = _serializer.Deserialize<AuthResponse>(data);

            if (response.Success)
            {
                IsAuthenticated = true;
                SessionId = response.SessionId;
            }

            OnAuthenticated?.Invoke(response);
        }

        private void HandleMessageReceive(byte[] data)
        {
            var message = _serializer.Deserialize<MessageReceive>(data);
            OnMessageReceived?.Invoke(message);
        }

        private void HandleChannelJoinAck(byte[] data)
        {
            var response = _serializer.Deserialize<ChannelJoinAck>(data);
            OnChannelJoined?.Invoke(response);
        }

        private void HandleChannelLeaveAck(byte[] data)
        {
            var response = _serializer.Deserialize<ChannelLeaveAck>(data);
            OnChannelLeft?.Invoke(response);
        }

        private void HandleChannelListResponse(byte[] data)
        {
            var response = _serializer.Deserialize<Chat.Protocol.ChannelListResponse>(data);
            OnChannelListReceived?.Invoke(response);
        }

        private void HandleChannelMemberUpdate(byte[] data)
        {
            var update = _serializer.Deserialize<ChannelMemberUpdate>(data);
            OnMemberUpdated?.Invoke(update);
        }

        private void HandleChannelAutoAssignAck(byte[] data)
        {
            var response = _serializer.Deserialize<ChannelAutoAssignAck>(data);
            OnChannelAutoAssigned?.Invoke(response);
        }

        private void HandleWhisperReceive(byte[] data)
        {
            var whisper = _serializer.Deserialize<WhisperReceive>(data);
            OnWhisperReceived?.Invoke(whisper);
        }

        private void HandleProfileUpdateResponse(byte[] data)
        {
            var response = _serializer.Deserialize<ProfileUpdateResponse>(data);

            // 성공 시 로컬 프로필 정보 업데이트
            if (response.Success && response.UpdatedProfile != null)
            {
                Nickname = response.UpdatedProfile.Nickname;
                ProfileImage = response.UpdatedProfile.ProfileImage;
                FrameImage = response.UpdatedProfile.FrameImage;
                ExtraData = response.UpdatedProfile.ExtraData;
            }

            OnProfileUpdated?.Invoke(response);
        }

        private void HandleProfileChanged(byte[] data)
        {
            var changed = _serializer.Deserialize<ProfileChanged>(data);
            OnProfileChanged?.Invoke(changed);
        }

        private void HandleAnnouncementReceive(byte[] data)
        {
            var announcement = _serializer.Deserialize<AnnouncementReceive>(data);
            OnAnnouncementReceived?.Invoke(announcement);
        }

        private void HandleUserActionNotificationReceive(byte[] data)
        {
            var notification = _serializer.Deserialize<UserActionNotificationReceive>(data);
            OnUserActionNotificationReceived?.Invoke(notification);
        }

        private void HandleErrorResponse(byte[] data)
        {
            var error = _serializer.Deserialize<Error>(data);
            OnError?.Invoke($"Server error ({error.ErrorCode}): {error.ErrorMessage}");
        }

        private void HandleDisconnected(string reason)
        {
            StopHeartbeat();
            IsAuthenticated = false;
            OnDisconnected?.Invoke(reason);
        }

        private void HandleError(string error)
        {
            OnError?.Invoke(error);
        }

        #endregion

        #region IDisposable

        public void Dispose()
        {
            Disconnect();
            _connection?.Dispose();
            _connection = null;
        }

        #endregion
    }
}
