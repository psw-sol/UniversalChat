using System;
using System.Collections.Generic;
using UnityEngine;

namespace UniversalChat.RichContent
{
    /// <summary>
    /// Rich Content 시스템 설정 ScriptableObject
    /// 프로젝트에서 생성하여 RichContentManager에 할당
    /// </summary>
    [CreateAssetMenu(fileName = "RichContentConfig", menuName = "UniversalChat/Rich Content Config")]
    public class RichContentConfig : ScriptableObject
    {
        [Header("기본 링크 색상")]
        [Tooltip("기본 링크 색상 (Provider에서 색상을 지정하지 않은 경우)")]
        public Color DefaultLinkColor = new Color(0f, 0.75f, 1f); // #00BFFF 하늘색

        [Tooltip("아이템 링크 기본 색상")]
        public Color ItemLinkColor = new Color(1f, 0.84f, 0f); // 골드

        [Tooltip("유저 링크 기본 색상")]
        public Color UserLinkColor = new Color(0.5f, 1f, 0.5f); // 연두색

        [Header("아이템 등급별 색상")]
        [Tooltip("일반(Common) 등급 색상")]
        public Color RarityCommonColor = new Color(0.8f, 0.8f, 0.8f);

        [Tooltip("고급(Uncommon) 등급 색상")]
        public Color RarityUncommonColor = new Color(0.2f, 0.8f, 0.2f);

        [Tooltip("희귀(Rare) 등급 색상")]
        public Color RarityRareColor = new Color(0.2f, 0.5f, 1f);

        [Tooltip("영웅(Epic) 등급 색상")]
        public Color RarityEpicColor = new Color(0.6f, 0.2f, 0.8f);

        [Tooltip("전설(Legendary) 등급 색상")]
        public Color RarityLegendaryColor = new Color(1f, 0.5f, 0f);

        [Header("입력 도우미")]
        [Tooltip("링크 입력 도우미 버튼 활성화")]
        public bool EnableLinkInputHelper = true;

        [Tooltip("지원하는 링크 타입 목록 (입력 도우미 UI에 표시)")]
        public List<LinkTypeInfo> SupportedLinkTypes = new List<LinkTypeInfo>
        {
            new LinkTypeInfo { Type = "ITEM", DisplayName = "아이템", Icon = null },
            new LinkTypeInfo { Type = "USER", DisplayName = "유저", Icon = null }
        };

        [Header("동작 설정")]
        [Tooltip("링크 클릭 시 햅틱(진동) 피드백")]
        public bool EnableHapticFeedback = true;

        [Tooltip("링크 롱프레스 감지 시간 (초)")]
        [Range(0.3f, 1.5f)]
        public float LongPressThreshold = 0.5f;

        [Tooltip("링크 클릭 시 사운드 재생")]
        public bool EnableClickSound = true;

        [Tooltip("링크 클릭 사운드")]
        public AudioClip LinkClickSound;

        /// <summary>
        /// 등급 인덱스로 색상 가져오기
        /// </summary>
        /// <param name="rarity">등급 (0: Common, 1: Uncommon, 2: Rare, 3: Epic, 4: Legendary)</param>
        public Color GetRarityColor(int rarity)
        {
            return rarity switch
            {
                0 => RarityCommonColor,
                1 => RarityUncommonColor,
                2 => RarityRareColor,
                3 => RarityEpicColor,
                4 => RarityLegendaryColor,
                _ => RarityCommonColor
            };
        }

        /// <summary>
        /// 링크 타입별 기본 색상 가져오기
        /// </summary>
        public Color GetDefaultColorForType(string linkType)
        {
            return linkType?.ToUpperInvariant() switch
            {
                "ITEM" => ItemLinkColor,
                "USER" => UserLinkColor,
                _ => DefaultLinkColor
            };
        }
    }

    /// <summary>
    /// 링크 타입 정보 (입력 도우미 UI용)
    /// </summary>
    [Serializable]
    public class LinkTypeInfo
    {
        [Tooltip("링크 타입 코드")]
        public string Type;

        [Tooltip("UI에 표시될 이름")]
        public string DisplayName;

        [Tooltip("아이콘 (선택)")]
        public Sprite Icon;
    }
}
