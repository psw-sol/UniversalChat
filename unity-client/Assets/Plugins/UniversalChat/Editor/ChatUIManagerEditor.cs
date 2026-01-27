using UnityEditor;
using UnityEngine;
using UniversalChat.UI;

namespace UniversalChat.Editor
{
    /// <summary>
    /// ChatUIManager 컴포넌트의 커스텀 인스펙터
    /// </summary>
    [CustomEditor(typeof(ChatUIManager))]
    public class ChatUIManagerEditor : UnityEditor.Editor
    {
        private SerializedProperty _serverHost;
        private SerializedProperty _serverPort;
        private SerializedProperty _connectOnStart;
        private SerializedProperty _autoLoginUserId;

        private SerializedProperty _chatPanel;
        private SerializedProperty _inputField;
        private SerializedProperty _channelListPanel;
        private SerializedProperty _sendButton;
        private SerializedProperty _connectButton;
        private SerializedProperty _connectionStatusText;

        private SerializedProperty _uiConfig;

        private SerializedProperty _onConnectedEvent;
        private SerializedProperty _onDisconnectedEvent;
        private SerializedProperty _onErrorEvent;
        private SerializedProperty _onAuthenticatedEvent;
        private SerializedProperty _onMessageReceivedEvent;
        private SerializedProperty _onChannelJoinedEvent;
        private SerializedProperty _onChannelLeftEvent;

        private bool _showConnectionSettings = true;
        private bool _showUIReferences = true;
        private bool _showTheme = true;
        private bool _showEvents = false;
        private bool _showDebugInfo = false;

        private void OnEnable()
        {
            _serverHost = serializedObject.FindProperty("_serverHost");
            _serverPort = serializedObject.FindProperty("_serverPort");
            _connectOnStart = serializedObject.FindProperty("_connectOnStart");
            _autoLoginUserId = serializedObject.FindProperty("_autoLoginUserId");

            _chatPanel = serializedObject.FindProperty("_chatPanel");
            _inputField = serializedObject.FindProperty("_inputField");
            _channelListPanel = serializedObject.FindProperty("_channelListPanel");
            _sendButton = serializedObject.FindProperty("_sendButton");
            _connectButton = serializedObject.FindProperty("_connectButton");
            _connectionStatusText = serializedObject.FindProperty("_connectionStatusText");

            _uiConfig = serializedObject.FindProperty("_uiConfig");

            _onConnectedEvent = serializedObject.FindProperty("OnConnectedEvent");
            _onDisconnectedEvent = serializedObject.FindProperty("OnDisconnectedEvent");
            _onErrorEvent = serializedObject.FindProperty("OnErrorEvent");
            _onAuthenticatedEvent = serializedObject.FindProperty("OnAuthenticatedEvent");
            _onMessageReceivedEvent = serializedObject.FindProperty("OnMessageReceivedEvent");
            _onChannelJoinedEvent = serializedObject.FindProperty("OnChannelJoinedEvent");
            _onChannelLeftEvent = serializedObject.FindProperty("OnChannelLeftEvent");
        }

        public override void OnInspectorGUI()
        {
            serializedObject.Update();

            var manager = (ChatUIManager)target;

            // Header
            EditorGUILayout.Space();
            DrawHeader();
            EditorGUILayout.Space();

            // Connection Status (Runtime)
            if (Application.isPlaying)
            {
                DrawRuntimeStatus(manager);
                EditorGUILayout.Space();
            }

            // Connection Settings
            _showConnectionSettings = EditorGUILayout.Foldout(_showConnectionSettings, "Connection Settings", true);
            if (_showConnectionSettings)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_serverHost, new GUIContent("Server Host"));
                EditorGUILayout.PropertyField(_serverPort, new GUIContent("Server Port"));
                EditorGUILayout.PropertyField(_connectOnStart, new GUIContent("Connect On Start"));

                if (_connectOnStart.boolValue)
                {
                    EditorGUILayout.PropertyField(_autoLoginUserId, new GUIContent("Auto Login User ID"));
                }

                EditorGUI.indentLevel--;
            }

            EditorGUILayout.Space();

            // UI References
            _showUIReferences = EditorGUILayout.Foldout(_showUIReferences, "UI References", true);
            if (_showUIReferences)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_chatPanel);
                EditorGUILayout.PropertyField(_inputField);
                EditorGUILayout.PropertyField(_channelListPanel);
                EditorGUILayout.PropertyField(_sendButton);
                EditorGUILayout.PropertyField(_connectButton);
                EditorGUILayout.PropertyField(_connectionStatusText);
                EditorGUI.indentLevel--;
            }

            EditorGUILayout.Space();

            // Theme
            _showTheme = EditorGUILayout.Foldout(_showTheme, "Theme", true);
            if (_showTheme)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_uiConfig, new GUIContent("UI Config"));

                if (_uiConfig.objectReferenceValue == null)
                {
                    EditorGUILayout.HelpBox("Assign a ChatUIConfig asset for custom styling.", MessageType.Info);

                    if (GUILayout.Button("Create New UI Config"))
                    {
                        CreateNewUIConfig();
                    }
                }

                EditorGUI.indentLevel--;
            }

            EditorGUILayout.Space();

            // Events
            _showEvents = EditorGUILayout.Foldout(_showEvents, "Events", true);
            if (_showEvents)
            {
                EditorGUI.indentLevel++;
                EditorGUILayout.PropertyField(_onConnectedEvent);
                EditorGUILayout.PropertyField(_onDisconnectedEvent);
                EditorGUILayout.PropertyField(_onErrorEvent);
                EditorGUILayout.PropertyField(_onAuthenticatedEvent);
                EditorGUILayout.PropertyField(_onMessageReceivedEvent);
                EditorGUILayout.PropertyField(_onChannelJoinedEvent);
                EditorGUILayout.PropertyField(_onChannelLeftEvent);
                EditorGUI.indentLevel--;
            }

            // Runtime Controls
            if (Application.isPlaying)
            {
                EditorGUILayout.Space();
                DrawRuntimeControls(manager);
            }

            serializedObject.ApplyModifiedProperties();
        }

        private void DrawHeader()
        {
            EditorGUILayout.BeginHorizontal();
            GUILayout.FlexibleSpace();

            var style = new GUIStyle(EditorStyles.boldLabel)
            {
                fontSize = 16,
                alignment = TextAnchor.MiddleCenter
            };

            EditorGUILayout.LabelField("Universal Chat UI Manager", style);

            GUILayout.FlexibleSpace();
            EditorGUILayout.EndHorizontal();
        }

        private void DrawRuntimeStatus(ChatUIManager manager)
        {
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);

            EditorGUILayout.LabelField("Runtime Status", EditorStyles.boldLabel);

            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("Connected:", GUILayout.Width(80));
            var statusColor = manager.IsConnected ? Color.green : Color.red;
            GUI.contentColor = statusColor;
            EditorGUILayout.LabelField(manager.IsConnected ? "Yes" : "No");
            GUI.contentColor = Color.white;
            EditorGUILayout.EndHorizontal();

            EditorGUILayout.BeginHorizontal();
            EditorGUILayout.LabelField("Authenticated:", GUILayout.Width(80));
            statusColor = manager.IsAuthenticated ? Color.green : Color.yellow;
            GUI.contentColor = statusColor;
            EditorGUILayout.LabelField(manager.IsAuthenticated ? "Yes" : "No");
            GUI.contentColor = Color.white;
            EditorGUILayout.EndHorizontal();

            if (!string.IsNullOrEmpty(manager.CurrentChannelId))
            {
                EditorGUILayout.LabelField($"Channel: {manager.CurrentChannelId}");
            }

            EditorGUILayout.EndVertical();
        }

        private void DrawRuntimeControls(ChatUIManager manager)
        {
            EditorGUILayout.BeginVertical(EditorStyles.helpBox);
            EditorGUILayout.LabelField("Runtime Controls", EditorStyles.boldLabel);

            EditorGUILayout.BeginHorizontal();

            if (!manager.IsConnected)
            {
                if (GUILayout.Button("Connect"))
                {
                    _ = manager.ConnectAsync();
                }
            }
            else
            {
                if (GUILayout.Button("Disconnect"))
                {
                    manager.Disconnect();
                }
            }

            EditorGUILayout.EndHorizontal();

            EditorGUILayout.EndVertical();
        }

        private void CreateNewUIConfig()
        {
            var config = ScriptableObject.CreateInstance<ChatUIConfig>();

            string path = EditorUtility.SaveFilePanelInProject(
                "Save UI Config",
                "ChatUIConfig",
                "asset",
                "Save chat UI configuration"
            );

            if (!string.IsNullOrEmpty(path))
            {
                AssetDatabase.CreateAsset(config, path);
                AssetDatabase.SaveAssets();

                _uiConfig.objectReferenceValue = config;
                serializedObject.ApplyModifiedProperties();
            }
        }
    }
}
