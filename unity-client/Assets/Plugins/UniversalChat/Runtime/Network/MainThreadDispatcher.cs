using System;
using System.Collections.Concurrent;
using UnityEngine;

namespace UniversalChat.Network
{
    /// <summary>
    /// 백그라운드 스레드에서 Unity 메인 스레드로 액션을 디스패치하는 유틸리티
    /// </summary>
    public class MainThreadDispatcher : MonoBehaviour
    {
        private static MainThreadDispatcher _instance;
        private static readonly ConcurrentQueue<Action> _actions = new ConcurrentQueue<Action>();
        private static bool _initialized;

        [RuntimeInitializeOnLoadMethod(RuntimeInitializeLoadType.BeforeSceneLoad)]
        private static void Initialize()
        {
            if (_initialized) return;
            if (!Application.isPlaying) return;

            var go = new GameObject("[MainThreadDispatcher]");
            _instance = go.AddComponent<MainThreadDispatcher>();
            DontDestroyOnLoad(go);
            _initialized = true;
        }

        public static void Enqueue(Action action)
        {
            if (action == null) return;

            // 이미 메인 스레드인 경우 즉시 실행
            if (UnityMainThreadId == System.Threading.Thread.CurrentThread.ManagedThreadId)
            {
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    Debug.LogException(ex);
                }
                return;
            }

            _actions.Enqueue(action);
        }

        private static int UnityMainThreadId;

        private void Awake()
        {
            UnityMainThreadId = System.Threading.Thread.CurrentThread.ManagedThreadId;
        }

        private void Update()
        {
            // 프레임당 처리할 최대 액션 수 제한
            int processedCount = 0;
            const int maxActionsPerFrame = 100;

            while (processedCount < maxActionsPerFrame && _actions.TryDequeue(out var action))
            {
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    Debug.LogException(ex);
                }

                processedCount++;
            }
        }

        private void OnDestroy()
        {
            _initialized = false;
        }
    }
}
