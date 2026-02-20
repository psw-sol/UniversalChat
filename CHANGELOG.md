# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.1] - 2026-02-20

### Changed
- **CLAUDE.md (server root)**: v2.0 반영 - DM 서브시스템, 채널 자동생성, 0x08xx 패킷 범위, src/dm/ 디렉토리, Unity 3-Level 아키텍처
- **unity-client/CLAUDE.md**: v2.0 전면 갱신 - IChatService, ChatServiceBase, DM API, 17개 UnityEvent 테이블, 폴더 구조, 사용 예시

## [2.0.0] - 2026-02-20

### Added
- **DM 서브시스템**: 영속적 1:1 대화, 메시지 히스토리, 읽음확인, 오프라인 수신
  - 서버: `src/dm/DMChannel.hpp/cpp`, `src/dm/DMManager.hpp/cpp`
  - DM 채널 ID: `dm:{sorted_user1}:{user2}` (알파벳 정렬 고유성)
  - 13개 DM 패킷 타입 (0x08xx 범위)
  - DM 에러 코드 6001-6005
  - Redis 저장: dm:messages (Sorted Set), dm:read (Hash), dm:user:list (Sorted Set)
- **채널 자동생성**: prefix 기반 (guild_, alliance_, party_) 자동 채널 생성
  - `ChannelManager::joinChannel()`에서 prefix 매칭
  - 설정: `channel.auto_create_prefixes`, `channel.auto_create_max_members`
- **Unity Client DM API**: IChatService에 6개 메서드 + 4개 이벤트
  - StartDMAsync, GetDMListAsync, SendDMMessageAsync, MarkDMReadAsync, LoadDMHistoryAsync, DeleteDMAsync
  - OnDMStarted, OnDMMessageReceived, OnDMReadReceiptReceived, OnDMListUpdated
- **ChatUIManager DM UnityEvent 4개** (총 13 -> 17개)
  - OnDMStartedEvent, OnDMMessageReceivedEvent, OnDMReadReceiptEvent, OnDMListUpdatedEvent
- **데이터 모델**: DMConversation, DMReadReceiptData (DataModels.cs)
- **TaskCompletionSource 패턴**: DM 비동기 요청-응답 (10초 타임아웃)
- **GameChatManager 확장**: Alliance/DM 채널 타입 + 편의 메서드
- **GameChatUIExample (NEW)**: 3탭 UI 참고 샘플 (월드/연맹/DM)
- **Samples/README.md**: DM 시스템, 채널 자동생성, 탭 UI 예시 상세 문서

### Unchanged
- 기존 Whisper (0x04xx) 시스템 하위호환 유지
- 기존 13개 ChatUIManager UnityEvent 변경 없음

## [1.0.0] - 2026-02-09

### Added
- **Plug & Play 아키텍처**: ChatUIManager 13개 UnityEvent 기반 UI 시스템
- **3-Level 사용 패턴**: Zero Code (ChatManager) / Minimal Code (ChatServiceBase) / Full Control (IChatService)
- **IChatService 인터페이스**: 채팅 서비스 추상화 (모든 UI가 의존)
- **ChatServiceBase<T>**: 제네릭 기본 클래스 (채널 타입별 관리, 이벤트 브릿징, 재연결, 히스토리)
- **ChatUIBuilder**: 프리팹 없이 런타임 UI 동적 생성
- **VirtualizedChatPanel**: 가상화 스크롤 (대량 메시지 성능)
- **RichContent 시스템**: `[TYPE:param1:...]` 태그 파싱 + 클릭 가능 링크
- **Translation 시스템**: REST API 번역, 캐싱 + 레이트 리미팅
- **ChannelJoinResult**: RecentMessages + Members + IsAutoAssign 통합 결과
- **OnChatReady 이벤트**: Connect -> Login -> Join 파이프라인 완료 이벤트
- **프로필 지원**: LoginAsync에 nickname, profileImage, frameImage, extraData
- **GameChatManager 샘플**: ChatServiceBase 기반 게임 전용 매니저

### Fixed
- 재연결 시 UserId null 버그 수정 (`_lastUserId` 캐싱)
- Whisper 이벤트 브릿지 누락 수정 (ChatClient -> ChatManager)

### Removed
- `ChatPanel.cs`: ChatUIBuilder에 통합
- `ChatMessageItem.cs`: VirtualizedChatPanel로 대체

## [1.0.0-alpha] - 2026-02-05

### Added
- 초기 릴리즈: TCP 채팅 서버 + Unity 클라이언트 패키지
- Boost.Asio 비동기 TCP 서버
- Protobuf 프로토콜 (Authentication, Channel, Message, Whisper, Profile)
- ChatClient, ChatManager, ChatConnection 기본 구조
- Redis 통합 (선택적 메시지 영속화)
- 월드 채널 자동 샤딩 (WorldChannelManager)
- Heartbeat, RateLimiter, Snowflake ID, PasswordHasher

[2.0.1]: https://github.com/psw-sol/UniversalChat/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/psw-sol/UniversalChat/compare/v1.0.0...v2.0.0
[1.0.0]: https://github.com/psw-sol/UniversalChat/compare/v1.0.0-alpha...v1.0.0
[1.0.0-alpha]: https://github.com/psw-sol/UniversalChat/releases/tag/v1.0.0-alpha
