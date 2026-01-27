using System;
using System.Collections.Generic;
using System.Text;
using System.Text.RegularExpressions;
using UnityEngine;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// [TYPE:param1:param2:...] 형식의 Rich Content 태그 파서
    /// 원본 텍스트에서 링크 태그를 추출하고 TMP 링크로 변환
    /// </summary>
    public static class RichTextParser
    {
        /// <summary>
        /// 태그 패턴: [TYPE:param:param...] 또는 [TYPE]
        /// TYPE은 대문자와 언더스코어만 허용
        /// </summary>
        private static readonly Regex TagPattern =
            new Regex(@"\[([A-Z_]+)(?::([^\]]+))?\]", RegexOptions.Compiled);

        /// <summary>
        /// 원본 텍스트에서 모든 Rich Link 태그 추출
        /// </summary>
        /// <param name="text">원본 텍스트</param>
        /// <returns>추출된 링크 데이터 목록</returns>
        public static List<RichLinkData> ParseLinks(string text)
        {
            var links = new List<RichLinkData>();

            if (string.IsNullOrEmpty(text))
                return links;

            var matches = TagPattern.Matches(text);

            foreach (Match match in matches)
            {
                var linkData = new RichLinkData
                {
                    Type = match.Groups[1].Value,
                    Parameters = match.Groups[2].Success
                        ? match.Groups[2].Value.Split(':')
                        : Array.Empty<string>(),
                    StartIndex = match.Index,
                    EndIndex = match.Index + match.Length,
                    OriginalTag = match.Value
                };

                links.Add(linkData);
            }

            return links;
        }

        /// <summary>
        /// 텍스트에 Rich Link 태그가 포함되어 있는지 확인
        /// </summary>
        public static bool ContainsLinks(string text)
        {
            if (string.IsNullOrEmpty(text))
                return false;

            return TagPattern.IsMatch(text);
        }

        /// <summary>
        /// 원본 텍스트의 Rich Link 태그를 TextMeshPro 링크 형식으로 변환
        /// </summary>
        /// <param name="originalText">원본 텍스트</param>
        /// <param name="displayTextProvider">링크 데이터를 표시 텍스트로 변환하는 함수</param>
        /// <param name="colorProvider">링크 데이터에 대한 색상을 반환하는 함수 (선택)</param>
        /// <param name="defaultLinkColor">기본 링크 색상 (colorProvider가 null 반환 시 사용)</param>
        /// <returns>TMP 리치 텍스트로 변환된 문자열</returns>
        public static string ConvertToTMPRichText(
            string originalText,
            Func<RichLinkData, string> displayTextProvider,
            Func<RichLinkData, Color?> colorProvider = null,
            Color? defaultLinkColor = null)
        {
            var links = ParseLinks(originalText);

            if (links.Count == 0)
                return originalText;

            var sb = new StringBuilder(originalText.Length * 2);
            int lastIndex = 0;

            foreach (var link in links)
            {
                // 태그 이전의 일반 텍스트 추가
                if (link.StartIndex > lastIndex)
                {
                    sb.Append(originalText.Substring(lastIndex, link.StartIndex - lastIndex));
                }

                // 표시 텍스트 가져오기
                string displayText = displayTextProvider?.Invoke(link) ?? link.OriginalTag;

                // 색상 결정
                Color linkColor = colorProvider?.Invoke(link)
                    ?? defaultLinkColor
                    ?? new Color(0f, 0.75f, 1f); // 기본: 하늘색

                string colorHex = ColorUtility.ToHtmlStringRGB(linkColor);

                // 링크 ID 생성 (원본 태그를 Base64로 인코딩)
                string linkId = EncodeTagToLinkId(link.OriginalTag);

                // TMP 링크 형식으로 변환
                // <link="linkId"><color=#RRGGBB><u>표시텍스트</u></color></link>
                sb.Append($"<link=\"{linkId}\"><color=#{colorHex}><u>{EscapeRichText(displayText)}</u></color></link>");

                lastIndex = link.EndIndex;
            }

            // 마지막 태그 이후의 텍스트 추가
            if (lastIndex < originalText.Length)
            {
                sb.Append(originalText.Substring(lastIndex));
            }

            return sb.ToString();
        }

        /// <summary>
        /// TMP 링크 ID에서 원본 RichLinkData 복원
        /// </summary>
        /// <param name="linkId">TMP 링크 ID</param>
        /// <returns>복원된 링크 데이터 (실패 시 null)</returns>
        public static RichLinkData? DecodeLinkId(string linkId)
        {
            try
            {
                string originalTag = DecodeTagFromLinkId(linkId);
                if (string.IsNullOrEmpty(originalTag))
                    return null;

                var links = ParseLinks(originalTag);
                return links.Count > 0 ? links[0] : (RichLinkData?)null;
            }
            catch (Exception)
            {
                return null;
            }
        }

        /// <summary>
        /// 원본 태그를 링크 ID로 인코딩 (Base64 URL-safe)
        /// </summary>
        private static string EncodeTagToLinkId(string originalTag)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(originalTag);
            string base64 = Convert.ToBase64String(bytes);

            // URL-safe Base64로 변환 (TMP 링크 ID에서 사용 가능하도록)
            return base64.Replace('+', '-').Replace('/', '_').TrimEnd('=');
        }

        /// <summary>
        /// 링크 ID를 원본 태그로 디코딩
        /// </summary>
        private static string DecodeTagFromLinkId(string linkId)
        {
            if (string.IsNullOrEmpty(linkId))
                return null;

            // URL-safe Base64를 표준 Base64로 변환
            string base64 = linkId.Replace('-', '+').Replace('_', '/');

            // 패딩 복원
            switch (base64.Length % 4)
            {
                case 2: base64 += "=="; break;
                case 3: base64 += "="; break;
            }

            byte[] bytes = Convert.FromBase64String(base64);
            return Encoding.UTF8.GetString(bytes);
        }

        /// <summary>
        /// 표시 텍스트에서 TMP 리치 텍스트 태그 이스케이프
        /// </summary>
        private static string EscapeRichText(string text)
        {
            if (string.IsNullOrEmpty(text))
                return text;

            // < 와 > 를 이스케이프하여 TMP 태그로 해석되지 않도록 함
            // 단, 이미 TMP 태그 형식인 경우는 유지
            return text.Replace("<", "<\u200B").Replace(">", "\u200B>");
        }

        /// <summary>
        /// 링크 태그를 직접 생성하는 헬퍼 메서드
        /// </summary>
        /// <param name="type">링크 타입</param>
        /// <param name="parameters">파라미터들</param>
        /// <returns>생성된 태그 문자열</returns>
        public static string CreateTag(string type, params string[] parameters)
        {
            if (string.IsNullOrEmpty(type))
                return string.Empty;

            var sb = new StringBuilder();
            sb.Append('[');
            sb.Append(type.ToUpperInvariant());

            if (parameters != null && parameters.Length > 0)
            {
                foreach (var param in parameters)
                {
                    sb.Append(':');
                    sb.Append(param ?? string.Empty);
                }
            }

            sb.Append(']');
            return sb.ToString();
        }

        /// <summary>
        /// 아이템 링크 태그 생성 헬퍼
        /// </summary>
        public static string CreateItemTag(string itemId, int enhancement = 0)
        {
            return enhancement > 0
                ? CreateTag("ITEM", itemId, enhancement.ToString())
                : CreateTag("ITEM", itemId);
        }

        /// <summary>
        /// 유저 링크 태그 생성 헬퍼
        /// </summary>
        public static string CreateUserTag(string oderId, string displayName)
        {
            return CreateTag("USER", oderId, displayName);
        }
    }
}
