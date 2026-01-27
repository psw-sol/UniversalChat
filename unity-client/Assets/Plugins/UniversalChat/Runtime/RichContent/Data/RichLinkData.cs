using System;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// 파싱된 Rich Link 데이터 구조체
    /// [TYPE:param1:param2:...] 형식의 태그에서 추출된 정보
    /// </summary>
    [Serializable]
    public struct RichLinkData
    {
        /// <summary>
        /// 링크 타입 (예: "ITEM", "USER", "GUILD")
        /// </summary>
        public string Type;

        /// <summary>
        /// 파라미터 배열 (콜론으로 구분된 값들)
        /// </summary>
        public string[] Parameters;

        /// <summary>
        /// 원본 텍스트에서의 시작 인덱스
        /// </summary>
        public int StartIndex;

        /// <summary>
        /// 원본 텍스트에서의 끝 인덱스
        /// </summary>
        public int EndIndex;

        /// <summary>
        /// 원본 태그 문자열 (예: "[ITEM:1001:5]")
        /// </summary>
        public string OriginalTag;

        /// <summary>
        /// 첫 번째 파라미터 (보통 ID)
        /// </summary>
        public string Param1 => Parameters != null && Parameters.Length > 0 ? Parameters[0] : null;

        /// <summary>
        /// 두 번째 파라미터
        /// </summary>
        public string Param2 => Parameters != null && Parameters.Length > 1 ? Parameters[1] : null;

        /// <summary>
        /// 세 번째 파라미터
        /// </summary>
        public string Param3 => Parameters != null && Parameters.Length > 2 ? Parameters[2] : null;

        /// <summary>
        /// 특정 인덱스의 파라미터 가져오기
        /// </summary>
        public string GetParam(int index)
        {
            if (Parameters == null || index < 0 || index >= Parameters.Length)
                return null;
            return Parameters[index];
        }

        /// <summary>
        /// 특정 인덱스의 파라미터를 int로 가져오기
        /// </summary>
        public int GetParamAsInt(int index, int defaultValue = 0)
        {
            var param = GetParam(index);
            if (string.IsNullOrEmpty(param))
                return defaultValue;
            return int.TryParse(param, out var result) ? result : defaultValue;
        }

        /// <summary>
        /// 특정 인덱스의 파라미터를 long으로 가져오기
        /// </summary>
        public long GetParamAsLong(int index, long defaultValue = 0)
        {
            var param = GetParam(index);
            if (string.IsNullOrEmpty(param))
                return defaultValue;
            return long.TryParse(param, out var result) ? result : defaultValue;
        }

        public override string ToString()
        {
            return $"RichLinkData[Type={Type}, Params={string.Join(",", Parameters ?? Array.Empty<string>())}]";
        }
    }
}
