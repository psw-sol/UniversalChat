using System.IO;
using UnityEngine;
using UnityEditor;
using UniversalChat.RichContent;

namespace UniversalChat.Editor
{
    /// <summary>
    /// Rich Content 관련 에디터 메뉴
    /// </summary>
    public static class RichContentEditorMenu
    {
        private const string SamplesPath = "Assets/Plugins/UniversalChat/Samples";
        private const string RichContentConfigPath = SamplesPath + "/Default Rich Content Config.asset";

        [MenuItem("UniversalChat/Create Rich Content Config", false, 201)]
        public static void CreateRichContentConfig()
        {
            var config = CreateOrLoadRichContentConfig();
            Selection.activeObject = config;
            EditorGUIUtility.PingObject(config);

            Debug.Log($"[UniversalChat] Rich Content Config: {RichContentConfigPath}");
        }

        [MenuItem("UniversalChat/Add RichContentManager to Scene", false, 202)]
        public static void AddRichContentManagerToScene()
        {
            // 이미 존재하는지 확인
            var existing = Object.FindFirstObjectByType<RichContentManager>();
            if (existing != null)
            {
                Debug.LogWarning("[RichContent] RichContentManager already exists in scene.");
                Selection.activeGameObject = existing.gameObject;
                EditorGUIUtility.PingObject(existing.gameObject);
                return;
            }

            // 새로 생성
            var go = new GameObject("[RichContentManager]");
            var manager = go.AddComponent<RichContentManager>();

            // Config 자동 연결 (Reflection 사용)
            var config = CreateOrLoadRichContentConfig();
            var configField = typeof(RichContentManager).GetField("_config",
                System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance);
            configField?.SetValue(manager, config);

            Undo.RegisterCreatedObjectUndo(go, "Add RichContentManager");
            EditorUtility.SetDirty(manager);

            Selection.activeGameObject = go;
            EditorGUIUtility.PingObject(go);

            Debug.Log("[RichContent] RichContentManager added to scene.");
        }

        [MenuItem("UniversalChat/Add RichContentManager to Scene", true)]
        private static bool ValidateAddRichContentManagerToScene()
        {
            return !EditorApplication.isPlaying;
        }

        /// <summary>
        /// Rich Content Config 생성 또는 로드
        /// </summary>
        public static RichContentConfig CreateOrLoadRichContentConfig()
        {
            EnsureDirectoryExists(SamplesPath);

            var existing = AssetDatabase.LoadAssetAtPath<RichContentConfig>(RichContentConfigPath);
            if (existing != null)
                return existing;

            var config = ScriptableObject.CreateInstance<RichContentConfig>();
            AssetDatabase.CreateAsset(config, RichContentConfigPath);
            AssetDatabase.SaveAssets();

            Debug.Log($"[UniversalChat] Rich Content Config 생성됨: {RichContentConfigPath}");
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
    }
}
