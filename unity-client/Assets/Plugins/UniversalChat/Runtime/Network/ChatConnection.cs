using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using UnityEngine;
using UniversalChat.Protocol;

namespace UniversalChat.Network
{
    /// <summary>
    /// TCP 소켓 연결을 관리하는 저수준 네트워크 클래스
    /// </summary>
    public class ChatConnection : IDisposable
    {
        #region Constants

        private const int HeaderSize = 8;
        private const int MaxPacketSize = 65535;
        private const int ReadBufferSize = 8192;

        #endregion

        #region Events

        public event Action<PacketType, byte[]> OnPacketReceived;
        public event Action<string> OnDisconnected;
        public event Action<string> OnError;

        #endregion

        #region Properties

        public bool IsConnected => _socket?.Connected ?? false;

        #endregion

        #region Fields

        private TcpClient _socket;
        private NetworkStream _stream;
        private CancellationTokenSource _readCts;
        private readonly SemaphoreSlim _writeLock = new SemaphoreSlim(1, 1);
        private readonly byte[] _readBuffer = new byte[ReadBufferSize];

        #endregion

        #region Connection

        public async Task<bool> ConnectAsync(string host, int port, int timeoutMs = 5000)
        {
            try
            {
                _socket = new TcpClient
                {
                    NoDelay = true,
                    ReceiveBufferSize = ReadBufferSize,
                    SendBufferSize = ReadBufferSize
                };

                // Unity 호환: WaitAsync 대신 Task.WhenAny 사용
                var connectTask = _socket.ConnectAsync(host, port);
                var timeoutTask = Task.Delay(timeoutMs);

                if (await Task.WhenAny(connectTask, timeoutTask) != connectTask)
                {
                    throw new OperationCanceledException();
                }
                await connectTask; // 예외 전파

                _stream = _socket.GetStream();

                _readCts = new CancellationTokenSource();
                _ = ReadLoopAsync(_readCts.Token);

                Debug.Log($"[ChatConnection] Connected to {host}:{port}");
                return true;
            }
            catch (OperationCanceledException)
            {
                OnError?.Invoke("Connection timeout");
                return false;
            }
            catch (Exception ex)
            {
                OnError?.Invoke($"Connection failed: {ex.Message}");
                return false;
            }
        }

        public void Disconnect()
        {
            if (_socket == null) return;

            try
            {
                _readCts?.Cancel();

                _stream?.Close();
                _socket?.Close();

                Debug.Log("[ChatConnection] Disconnected");
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[ChatConnection] Error during disconnect: {ex.Message}");
            }
            finally
            {
                _stream = null;
                _socket = null;
            }
        }

        #endregion

        #region Send

        public async Task SendAsync(byte[] packet)
        {
            if (!IsConnected)
            {
                OnError?.Invoke("Not connected");
                return;
            }

            await _writeLock.WaitAsync();

            try
            {
                await _stream.WriteAsync(packet, 0, packet.Length);
                await _stream.FlushAsync();
            }
            catch (Exception ex)
            {
                OnError?.Invoke($"Send failed: {ex.Message}");
                HandleDisconnection("Send error");
            }
            finally
            {
                _writeLock.Release();
            }
        }

        #endregion

        #region Receive

        private async Task ReadLoopAsync(CancellationToken token)
        {
            var headerBuffer = new byte[HeaderSize];

            try
            {
                while (!token.IsCancellationRequested && IsConnected)
                {
                    // Read header
                    int headerRead = await ReadExactAsync(headerBuffer, 0, HeaderSize, token);
                    if (headerRead != HeaderSize)
                    {
                        HandleDisconnection("Connection closed by server");
                        return;
                    }

                    // Parse header
                    var header = PacketHeader.FromBytes(headerBuffer);

                    if (header.Length < HeaderSize || header.Length > MaxPacketSize)
                    {
                        OnError?.Invoke($"Invalid packet length: {header.Length}");
                        HandleDisconnection("Invalid packet");
                        return;
                    }

                    // Read body
                    int bodyLength = (int)header.Length - HeaderSize;
                    byte[] bodyData = null;

                    if (bodyLength > 0)
                    {
                        bodyData = new byte[bodyLength];
                        int bodyRead = await ReadExactAsync(bodyData, 0, bodyLength, token);

                        if (bodyRead != bodyLength)
                        {
                            HandleDisconnection("Incomplete packet received");
                            return;
                        }
                    }
                    else
                    {
                        bodyData = Array.Empty<byte>();
                    }

                    // Dispatch to main thread
                    var type = header.Type;
                    var data = bodyData;

                    MainThreadDispatcher.Enqueue(() =>
                    {
                        OnPacketReceived?.Invoke(type, data);
                    });
                }
            }
            catch (OperationCanceledException)
            {
                // Normal cancellation
            }
            catch (Exception ex)
            {
                if (!token.IsCancellationRequested)
                {
                    OnError?.Invoke($"Read error: {ex.Message}");
                    HandleDisconnection("Read error");
                }
            }
        }

        private async Task<int> ReadExactAsync(byte[] buffer, int offset, int count, CancellationToken token)
        {
            int totalRead = 0;

            while (totalRead < count && !token.IsCancellationRequested)
            {
                int read = await _stream.ReadAsync(buffer, offset + totalRead, count - totalRead, token);

                if (read == 0)
                {
                    return totalRead;
                }

                totalRead += read;
            }

            return totalRead;
        }

        private void HandleDisconnection(string reason)
        {
            _readCts?.Cancel();

            MainThreadDispatcher.Enqueue(() =>
            {
                OnDisconnected?.Invoke(reason);
            });

            Disconnect();
        }

        #endregion

        #region IDisposable

        public void Dispose()
        {
            Disconnect();
            _writeLock?.Dispose();
            _readCts?.Dispose();
        }

        #endregion
    }
}
