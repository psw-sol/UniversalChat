using System.IO;
using UnityEngine;
using UnityEngine.UI;
using UnityEditor;
using UnityEditor.SceneManagement;
using UniversalChat.UI;
using UniversalChat.RichContent;

namespace UniversalChat.Editor
{
    /// <summary>
    /// 채팅 UI 샘플 씬을 생성하는 에디터 도구
    /// 런타임 빌더를 사용하여 프리팹 없이 UI를 생성합니다.
    /// </summary>
    public static class ChatUISampleSceneGenerator
    {
        private const string SamplesPath = "Assets/Plugins/UniversalChat/Samples";
        private const string ScenePath = SamplesPath + "/Scenes/ChatSample.unity";
        private const string ConfigPath = SamplesPath + "/Default Chat UI Config.asset";

        [MenuItem("UniversalChat/Create Sample Scene", false, 100)]
        public static void CreateSampleScene()
        {
            // 폴더 생성
            EnsureDirectoryExists(SamplesPath);
            EnsureDirectoryExists(SamplesPath + "/Scenes");

            // 새 씬 생성
            var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);

            // UIConfig 생성/로드
            var uiConfig = CreateOrLoadUIConfig();

            // Canvas 생성
            var canvas = CreateCanvas();

            // 런타임 빌더를 사용하여 채팅 UI 생성
            var chatUI = ChatUIBuilder.Build(canvas.transform, uiConfig);

            // 위치 및 크기 설정
            var rectTransform = chatUI.GetComponent<RectTransform>();
            rectTransform.anchorMin = new Vector2(0.5f, 0.5f);
            rectTransform.anchorMax = new Vector2(0.5f, 0.5f);
            rectTransform.anchoredPosition = Vector2.zero;
            rectTransform.sizeDelta = new Vector2(420, 600);

            // EventSystem 확인
            if (Object.FindFirstObjectByType<UnityEngine.EventSystems.EventSystem>() == null)
            {
                var eventSystemObj = new GameObject("EventSystem");
                eventSystemObj.AddComponent<UnityEngine.EventSystems.EventSystem>();
                eventSystemObj.AddComponent<UnityEngine.EventSystems.StandaloneInputModule>();
            }

            // 씬 저장
            EditorSceneManager.SaveScene(scene, ScenePath);
            AssetDatabase.Refresh();

            Debug.Log("[UniversalChat] 샘플 씬 생성 완료!");
            Debug.Log($"위치: {ScenePath}");

            // 생성된 오브젝트 선택
            Selection.activeGameObject = chatUI.gameObject;
        }

        [MenuItem("UniversalChat/Create Sample Scene", true)]
        private static bool ValidateCreateSampleScene()
        {
            return !EditorApplication.isPlaying;
        }

        [MenuItem("UniversalChat/Add Chat UI to Scene", false, 101)]
        public static void AddChatUIToScene()
        {
            AddChatUIToSceneInternal(enableRichContent: true);
        }

        [MenuItem("UniversalChat/Add Chat UI to Scene (Basic)", false, 102)]
        public static void AddBasicChatUIToScene()
        {
            AddChatUIToSceneInternal(enableRichContent: false);
        }

        private static void AddChatUIToSceneInternal(bool enableRichContent)
        {
            // 현재 씬에 Canvas가 있는지 확인
            var canvas = Object.FindFirstObjectByType<Canvas>();
            if (canvas == null)
            {
                canvas = CreateCanvas();
            }

            // UIConfig 생성/로드
            var uiConfig = CreateOrLoadUIConfig();

            // 런타임 빌더를 사용하여 채팅 UI 생성
            var chatUI = ChatUIBuilder.Build(canvas.transform, uiConfig);

            // Rich Content 설정
            if (enableRichContent)
            {
                // RichContentManager 확인/생성
                var richManager = Object.FindFirstObjectByType<RichContentManager>();
                if (richManager == null)
                {
                    var richManagerGo = new GameObject("[RichContentManager]");
                    richManager = richManagerGo.AddComponent<RichContentManager>();

                    // Config 연결
                    var richConfig = RichContentEditorMenu.CreateOrLoadRichContentConfig();
                    var configField = typeof(RichContentManager).GetField("_config",
                        System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
                    configField?.SetValue(richManager, richConfig);

                    EditorUtility.SetDirty(richManager);
                    Debug.Log("[UniversalChat] RichContentManager가 자동으로 추가되었습니다.");
                }
            }

            // 위치 설정
            var rectTransform = chatUI.GetComponent<RectTransform>();
            rectTransform.anchorMin = new Vector2(1, 0);
            rectTransform.anchorMax = new Vector2(1, 0);
            rectTransform.pivot = new Vector2(1, 0);
            rectTransform.anchoredPosition = new Vector2(-10, 10);
            rectTransform.sizeDelta = new Vector2(400, 500);

            // EventSystem 확인
            if (Object.FindFirstObjectByType<UnityEngine.EventSystems.EventSystem>() == null)
            {
                var eventSystemObj = new GameObject("EventSystem");
                eventSystemObj.AddComponent<UnityEngine.EventSystems.EventSystem>();
                eventSystemObj.AddComponent<UnityEngine.EventSystems.StandaloneInputModule>();
            }

            // 씬 더티 마킹
            EditorSceneManager.MarkSceneDirty(UnityEngine.SceneManagement.SceneManager.GetActiveScene());

            string modeText = enableRichContent ? "(Rich Content 포함)" : "(Basic)";
            Debug.Log($"[UniversalChat] 채팅 UI가 현재 씬에 추가되었습니다! {modeText}");

            // 생성된 오브젝트 선택
            Selection.activeGameObject = chatUI.gameObject;
            EditorGUIUtility.PingObject(chatUI.gameObject);
        }

        [MenuItem("UniversalChat/Add Chat UI to Scene", true)]
        [MenuItem("UniversalChat/Add Chat UI to Scene (Basic)", true)]
        private static bool ValidateAddChatUIToScene()
        {
            return !EditorApplication.isPlaying;
        }

        [MenuItem("UniversalChat/Create UI Config", false, 200)]
        public static void CreateUIConfig()
        {
            var config = CreateOrLoadUIConfig();
            Selection.activeObject = config;
            EditorGUIUtility.PingObject(config);
        }

        #region Helper Methods

        private static Canvas CreateCanvas()
        {
            var canvasObj = new GameObject("Canvas");
            var canvas = canvasObj.AddComponent<Canvas>();
            canvas.renderMode = RenderMode.ScreenSpaceOverlay;

            var scaler = canvasObj.AddComponent<CanvasScaler>();
            scaler.uiScaleMode = CanvasScaler.ScaleMode.ScaleWithScreenSize;
            scaler.referenceResolution = new Vector2(1920, 1080);
            scaler.matchWidthOrHeight = 0.5f;

            canvasObj.AddComponent<GraphicRaycaster>();

            return canvas;
        }

        private static ChatUIConfig CreateOrLoadUIConfig()
        {
            EnsureDirectoryExists(SamplesPath);

            var existing = AssetDatabase.LoadAssetAtPath<ChatUIConfig>(ConfigPath);
            if (existing != null) return existing;

            var config = ScriptableObject.CreateInstance<ChatUIConfig>();
            AssetDatabase.CreateAsset(config, ConfigPath);
            AssetDatabase.SaveAssets();

            Debug.Log($"[UniversalChat] UI Config 생성됨: {ConfigPath}");
            return config;
        }

        private static void EnsureDirectoryExists(string path)
        {
            if (!AssetDatabase.IsValidFolder(path))
            {
                var parent = Path.GetDirectoryName(path)?.Replace("\\", "/");
                var folderName = Path.GetFileName(path);

                if (!string.IsNullOrEmpty(parent) && !AssetDatabase.IsValidFolder(parent))
                {
                    EnsureDirectoryExists(parent);
                }

                AssetDatabase.CreateFolder(parent, folderName);
            }
        }

        #endregion
    }
}
