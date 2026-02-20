# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 프로젝트 개요

UniversalChatServer는 C++17로 작성된 고성능 채팅 서버입니다. Boost.Asio 기반의 비동기 TCP 서버로, Protobuf 프로토콜을 사용하여 Unity 클라이언트와 통신합니다.

**기술 스택**: C++17, Boost.Asio, Protobuf, spdlog, nlohmann_json, GoogleTest

## 빌드 명령어

```bash
# 빌드 디렉토리 생성 및 CMake 구성
mkdir build && cd build
cmake ..

# 빌드 (Release)
cmake --build . --config Release

# 빌드 (Debug)
cmake --build . --config Debug

# Redis 지원 활성화하여 빌드
cmake .. -DENABLE_REDIS=ON
cmake --build . --config Release

# 테스트 빌드 비활성화
cmake .. -DBUILD_TESTS=OFF
```

## 서버 실행

```bash
# 기본 설정으로 실행
./bin/Release/chat_server

# 설정 파일 지정
./bin/Release/chat_server --config config/server.json

# 명령줄 옵션으로 오버라이드
./bin/Release/chat_server --port 8080 --log-level debug --threads 8

# 도움말
./bin/Release/chat_server --help
```

## 테스트 실행

```bash
cd build

# 모든 테스트 실행
ctest --output-on-failure

# 특정 테스트 실행
./bin/Debug/chat_server_tests --gtest_filter=RateLimiterTest.*
./bin/Debug/chat_server_tests --gtest_filter=SessionTest.*
./bin/Debug/chat_server_tests --gtest_filter=PasswordHasherTest.*
./bin/Debug/chat_server_tests --gtest_filter=IntegrationTest.*
```

## Docker 실행

```bash
# 빌드 및 실행 (Redis 포함)
docker compose up -d

# 스케일 아웃 (다중 인스턴스)
docker compose -f docker-compose.scale.yml up -d --scale chat-server=4

# 로그 확인
docker compose logs -f chat-server
```

## 아키텍처

### 핵심 컴포넌트 (src/)

```
src/
├── core/           # 서버 코어
│   ├── Server      # TCP acceptor, 컴포넌트 조정
│   ├── Session     # 클라이언트 연결 상태 관리
│   ├── SessionManager  # 세션 풀 관리
│   └── IOContextPool   # IO 스레드 풀
├── channel/        # 채널 시스템
│   ├── Channel     # 개별 채널 (멤버, 메시지 기록)
│   └── ChannelManager  # 채널 생성/삭제/조회 + prefix 기반 자동생성
├── dm/             # DM 서브시스템 (v2.0)
│   ├── DMChannel   # 2인 전용 DM 채널 (히스토리, 읽음확인)
│   └── DMManager   # DM 생명주기 관리 (생성, 목록, 전달)
├── message/        # 메시지 처리
│   └── MessageDispatcher  # 패킷 타입별 핸들러 라우팅 (DM 핸들러 6개 포함)
├── protocol/       # 네트워크 프로토콜
│   ├── PacketCodec     # Protobuf 인코딩/디코딩
│   ├── PacketHeader    # 8바이트 패킷 헤더 구조
│   └── PacketTypes     # 패킷 타입 상수 (0x0001~0xFF01)
├── redis/          # Redis 통합 (선택적)
│   ├── RedisClient     # Redis 연결 관리
│   └── MessageStore    # 메시지 영속화
└── util/           # 유틸리티
    ├── Config      # JSON 설정 파싱 (채널 자동생성, DM 설정 포함)
    ├── Logger      # spdlog 래퍼
    ├── Snowflake   # 분산 ID 생성
    ├── RateLimiter # 속도 제한
    └── PasswordHasher  # bcrypt/PBKDF2 해싱
```

### 요청 처리 흐름

1. **Server::doAccept()** → 새 TCP 연결 수락
2. **Session** 생성 → SessionManager에 등록
3. **Session::doRead()** → 비동기 패킷 수신
4. **PacketCodec::decode()** → Protobuf 메시지 파싱
5. **MessageDispatcher::dispatch()** → 패킷 타입별 핸들러 호출
6. 핸들러에서 응답 생성 → **Session::send()** → 클라이언트로 전송

### 프로토콜 구조 (proto/chat.proto)

패킷 헤더: 8바이트 (4바이트 길이 + 2바이트 타입 + 2바이트 예약)

| 범위 | 카테고리 |
|------|---------|
| 0x00xx | Connection (Heartbeat) |
| 0x01xx | Authentication |
| 0x02xx | Channel (List, Join, Leave, AutoAssign) |
| 0x03xx | Message (Send, Receive, History, Announcement) |
| 0x04xx | Whisper (온라인 전용 1:1) |
| 0x05xx | Profile |
| 0x08xx | DM (영속적 1:1 대화, 히스토리, 읽음확인) [v2.0] |
| 0xFFxx | Error |

### DM 서브시스템 (v2.0)

- **DM 채널 ID**: `dm:{sorted_user1}:{user2}` (알파벳 정렬 고유성)
- **DMChannel**: 2인 전용 채널, 히스토리 관리, 읽음확인
- **DMManager**: DM 생명주기 (생성, 목록, 메시지 라우팅, 삭제)
- **패킷**: DMStart/DMList/DMMessageSend/Receive/DMReadReceipt/DMHistory/DMDelete (13개)
- **에러 코드**: 6001~6005 (DMChannelCreateFailed ~ DMHistoryLoadFailed)
- **Redis 저장**: dm:messages (Sorted Set), dm:read (Hash), dm:user:list (Sorted Set)

### 채널 자동생성 (v2.0)

- `ChannelManager::joinChannel()`에서 prefix 매칭 자동 생성
- 설정: `channel.auto_create_prefixes` (예: `["guild_", "alliance_", "party_"]`)
- 설정: `channel.auto_create_max_members` (기본 200)

### Unity 클라이언트 (unity-client/)

`Assets/Plugins/UniversalChat/` 경로에 Unity 패키지로 제공 (v2.0.0):

- **IChatService**: 채팅 서비스 추상화 인터페이스 (DM 6메서드 + 4이벤트 포함)
- **ChatServiceBase<T>**: 제네릭 기본 클래스 (채널 타입별 관리, DM, 재연결, 히스토리)
- **ChatManager**: MonoBehaviour 싱글톤 + IChatService 구현체 (Level 1 Zero Code)
- **ChatClient**: 저수준 TCP 클라이언트, DM 패킷 송수신
- **ChatUIManager**: Plug&Play UI, 17개 UnityEvent (DM 4개 포함)
- **PacketSerializer**: Protobuf 직렬화 (Google.Protobuf 의존)

3-Level 사용 패턴:
- **Level 1**: ChatManager 싱글톤 (Inspector만으로 사용)
- **Level 2**: ChatServiceBase<T> 상속 → ClassifyChannel()만 구현
- **Level 3**: IChatService 직접 구현

## 설정 (config/server.json)

주요 설정 항목:

| 섹션 | 키 | 설명 | 기본값 |
|------|-----|------|--------|
| server | port | 서버 포트 | 7777 |
| server | io_threads | IO 스레드 수 | 4 |
| server | max_connections | 최대 동시 접속 | 10000 |
| connection | heartbeat_interval | 하트비트 간격(초) | 30 |
| connection | heartbeat_timeout | 하트비트 타임아웃(초) | 90 |
| channel | predefined_channels | 시스템 채널 정의 | world, trade, help |
| channel | auto_create_prefixes | 자동 생성 prefix 목록 | guild_, alliance_, party_ |
| channel | auto_create_max_members | 자동 생성 채널 최대 멤버 | 200 |
| redis | enabled | Redis 사용 여부 | false |
| logging | level | 로그 레벨 | info |

## 코딩 컨벤션

- **네임스페이스**: 모든 코드는 `chat` 네임스페이스 내에 정의
- **스마트 포인터**: `shared_ptr`, `unique_ptr` 사용 (raw pointer 지양)
- **비동기 패턴**: Boost.Asio completion handler 사용, callback hell 주의
- **로깅**: `LOG_INFO`, `LOG_DEBUG`, `LOG_ERROR` 매크로 사용 (src/util/Logger.hpp)
- **에러 코드**: `include/universalchat/Errors.hpp`에 정의된 상수 사용

## 중요 사항

- **Protobuf 생성 파일**: proto 수정 후 `cmake --build .` 시 자동 생성됨 (build/ 디렉토리)
- **Windows 빌드**: Visual Studio 2019+ 또는 MinGW-w64 필요, ws2_32/wsock32/bcrypt 자동 링크
- **Linux 빌드**: OpenSSL 필요 (패스워드 해싱용)
- **스레드 안전성**: Session, Channel은 strand로 직렬화됨, shared state 접근 시 mutex 사용
