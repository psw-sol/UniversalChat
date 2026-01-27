# 채팅 번역 기능 구현 - 상세 작업 계획서

**작성일**: 2026-01-27
**기반 문서**: `research_chat_translation_comprehensive.md`
**목표**: M2M-100 기반 채팅 번역 시스템 구축

---

## 📋 작업 개요

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         전체 작업 구조 (WBS)                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Phase 1: 기본 인프라 (1주)                                                  │
│  ├─ 1.1 번역 서버 구축                                                       │
│  ├─ 1.2 캐싱 시스템 구현                                                     │
│  └─ 1.3 Unity 클라이언트 통합                                                │
│                                                                             │
│  Phase 2: 신뢰성 강화 (1주)                                                  │
│  ├─ 2.1 폴백 시스템 구현                                                     │
│  ├─ 2.2 Rate Limiter 구현                                                   │
│  └─ 2.3 모니터링 및 로깅                                                     │
│                                                                             │
│  Phase 3: 품질 개선 (2주)                                                    │
│  ├─ 3.1 학습 데이터 수집                                                     │
│  ├─ 3.2 QLoRA 파인튜닝                                                      │
│  └─ 3.3 A/B 테스트 및 배포                                                   │
│                                                                             │
│  Phase 4: 운영 최적화 (1주)                                                  │
│  ├─ 4.1 모니터링 대시보드                                                    │
│  ├─ 4.2 알림 시스템                                                         │
│  └─ 4.3 추가 언어 지원                                                       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 🏗️ Phase 1: 기본 인프라 구축 (예상 5일)

### 1.1 번역 서버 구축 (2일)

#### Task 1.1.1: Docker 환경 설정
```
파일: translation-server/docker-compose.yml
      translation-server/Dockerfile

작업 내용:
□ Dockerfile 작성 (Python 3.10 + CUDA 지원)
□ docker-compose.yml 작성 (번역 서버 + Redis)
□ GPU 지원 설정 (nvidia-docker)
□ 환경 변수 설정 (.env 파일)
□ 볼륨 마운트 설정 (모델 캐시)

산출물:
- translation-server/
  ├── Dockerfile
  ├── docker-compose.yml
  ├── .env.example
  └── .dockerignore

검증 기준:
- docker compose up 정상 실행
- GPU 인식 확인 (nvidia-smi)
- 컨테이너 헬스체크 통과
```

#### Task 1.1.2: FastAPI 서버 구현
```
파일: translation-server/src/main.py
      translation-server/src/translator.py
      translation-server/src/models.py

작업 내용:
□ FastAPI 앱 초기화
□ M2M-100-418M 모델 로딩 클래스
□ /translate 엔드포인트 구현
□ /health 엔드포인트 구현
□ /languages 엔드포인트 구현
□ 요청/응답 Pydantic 모델

API 스펙:
POST /translate
{
  "text": "안녕하세요",
  "source_lang": "ko",
  "target_lang": "en"
}
→ {"translated_text": "Hello", "source_lang": "ko", "target_lang": "en"}

GET /health
→ {"status": "healthy", "model_loaded": true, "gpu_available": true}

GET /languages
→ {"supported": ["ko", "en", "zh", "ja"]}

검증 기준:
- 4개 언어 간 12방향 번역 테스트
- 응답 시간 < 500ms (GPU)
- 동시 요청 10개 처리
```

#### Task 1.1.3: 의존성 및 설정
```
파일: translation-server/requirements.txt
      translation-server/src/config.py

작업 내용:
□ requirements.txt 작성
  - fastapi==0.109.0
  - uvicorn[standard]==0.27.0
  - transformers==4.37.0
  - torch==2.1.0
  - sentencepiece==0.1.99
  - redis==5.0.1
  - pydantic-settings==2.1.0

□ 설정 관리 클래스 (환경변수)
  - MODEL_NAME: facebook/m2m100_418M
  - DEVICE: cuda/cpu
  - REDIS_URL: redis://localhost:6379
  - MAX_LENGTH: 256
  - BATCH_SIZE: 8

검증 기준:
- pip install -r requirements.txt 성공
- 환경변수 오버라이드 동작
```

---

### 1.2 캐싱 시스템 구현 (1일)

#### Task 1.2.1: Redis 캐시 레이어
```
파일: translation-server/src/cache.py

작업 내용:
□ Redis 연결 관리 클래스
□ 캐시 키 생성 함수 (hash(text+src+tgt))
□ 캐시 조회/저장 함수
□ TTL 설정 (기본 24시간)
□ 캐시 통계 함수

캐시 키 구조:
trans:{sha256(text+src_lang+tgt_lang)[:16]}

예시:
trans:a1b2c3d4e5f6g7h8 → "Hello"

검증 기준:
- 동일 요청 캐시 히트
- TTL 만료 후 자동 삭제
- 캐시 히트율 측정 가능
```

#### Task 1.2.2: 캐시 적용
```
파일: translation-server/src/main.py (수정)

작업 내용:
□ /translate 엔드포인트에 캐시 적용
□ 캐시 히트 시 즉시 반환
□ 캐시 미스 시 번역 후 저장
□ 응답에 cache_hit 플래그 추가

응답 예시:
{
  "translated_text": "Hello",
  "source_lang": "ko",
  "target_lang": "en",
  "cache_hit": true,
  "latency_ms": 5
}

검증 기준:
- 캐시 히트 시 응답 < 10ms
- 캐시 미스 시 응답 < 500ms
```

---

### 1.3 Unity 클라이언트 통합 (2일)

#### Task 1.3.1: 번역 서비스 클래스
```
파일: unity-client/Assets/Plugins/UniversalChat/Runtime/Translation/TranslationService.cs

작업 내용:
□ ITranslationService 인터페이스 정의
□ TranslationService 구현 클래스
□ HTTP 요청 래퍼 (UnityWebRequest)
□ async/await 패턴 적용
□ 에러 핸들링

인터페이스:
public interface ITranslationService
{
    Task<TranslationResult> TranslateAsync(string text, string sourceLang, string targetLang);
    Task<string[]> GetSupportedLanguagesAsync();
    bool IsAvailable { get; }
}

검증 기준:
- Unity 에디터에서 번역 테스트
- 네트워크 에러 처리
- 타임아웃 처리 (10초)
```

#### Task 1.3.2: 번역 UI 컴포넌트
```
파일: unity-client/Assets/Plugins/UniversalChat/Runtime/Translation/TranslationUI.cs
      unity-client/Assets/Plugins/UniversalChat/Runtime/UI/ChatMessageItem.cs (수정)

작업 내용:
□ 메시지 아이템에 번역 버튼 추가
□ 번역 결과 표시 UI
□ 로딩 인디케이터
□ 언어 자동 감지 표시
□ 번역 토글 (원본/번역 전환)

UI 흐름:
1. 메시지 롱프레스 또는 버튼 클릭
2. 번역 요청 (로딩 표시)
3. 번역 결과 표시 (원본 아래 또는 교체)
4. 탭으로 원본/번역 전환

검증 기준:
- 번역 버튼 클릭 → 번역 표시
- 로딩 중 UX
- 에러 시 사용자 피드백
```

#### Task 1.3.3: 설정 및 연동
```
파일: unity-client/Assets/Plugins/UniversalChat/Runtime/Translation/TranslationConfig.cs
      unity-client/Assets/Plugins/UniversalChat/Runtime/Core/ChatManager.cs (수정)

작업 내용:
□ TranslationConfig ScriptableObject
  - 서버 URL
  - 타임아웃
  - 기본 타겟 언어
  - 캐시 설정
□ ChatManager에 TranslationService 통합
□ 에디터 메뉴에서 Config 생성

설정 항목:
- Server URL: http://localhost:8000
- Timeout: 10 seconds
- Default Target Language: en
- Enable Local Cache: true
- Cache Duration: 24 hours

검증 기준:
- Inspector에서 설정 변경 가능
- 런타임 설정 로드
```

---

## 🛡️ Phase 2: 신뢰성 강화 (예상 5일)

### 2.1 폴백 시스템 구현 (2일)

#### Task 2.1.1: Azure Translator 폴백
```
파일: translation-server/src/fallback/azure.py

작업 내용:
□ Azure Translator API 클라이언트
□ 인증 설정 (API Key)
□ 번역 요청 함수
□ 에러 핸들링

설정:
- AZURE_TRANSLATOR_KEY: API 키
- AZURE_TRANSLATOR_REGION: 리전
- AZURE_TRANSLATOR_ENDPOINT: 엔드포인트

검증 기준:
- Azure API 연동 테스트
- 무료 티어 한도 확인 (월 200만 자)
```

#### Task 2.1.2: Gemini 폴백
```
파일: translation-server/src/fallback/gemini.py

작업 내용:
□ Gemini API 클라이언트
□ 번역 프롬프트 템플릿
□ 응답 파싱
□ Rate limiting 대응

프롬프트:
"""
Translate the following text from {source_lang} to {target_lang}.
Only output the translation, nothing else.

Text: {text}
"""

검증 기준:
- Gemini 2.0 Flash Lite 연동
- 프롬프트 최적화
```

#### Task 2.1.3: 폴백 체인 구현
```
파일: translation-server/src/fallback/chain.py
      translation-server/src/main.py (수정)

작업 내용:
□ 폴백 체인 매니저
□ 우선순위: M2M-100 → Azure → Gemini
□ 실패 시 자동 폴백
□ 폴백 사용 로깅

폴백 로직:
1. M2M-100 시도 (타임아웃 5초)
2. 실패 시 Azure 시도 (월 200만 자 한도)
3. 실패 시 Gemini 시도
4. 모두 실패 시 에러 반환

응답에 provider 필드 추가:
{
  "translated_text": "Hello",
  "provider": "m2m100",  // or "azure", "gemini"
  "fallback_used": false
}

검증 기준:
- M2M-100 장애 시 Azure 폴백
- Azure 한도 초과 시 Gemini 폴백
- 폴백 로깅 확인
```

---

### 2.2 Rate Limiter 구현 (1일)

#### Task 2.2.1: 사용자별 Rate Limiting
```
파일: translation-server/src/rate_limiter.py

작업 내용:
□ Redis 기반 Rate Limiter
□ 사용자별 제한 (IP 또는 user_id)
□ Sliding Window 알고리즘
□ 제한 초과 시 429 응답

설정:
- 분당 요청: 30회
- 시간당 요청: 500회
- 일당 요청: 5,000회

검증 기준:
- 제한 초과 시 429 반환
- X-RateLimit-* 헤더 포함
- Redis 키 자동 만료
```

#### Task 2.2.2: API 적용
```
파일: translation-server/src/main.py (수정)

작업 내용:
□ /translate에 Rate Limiter 미들웨어
□ 요청 헤더에서 user_id 추출
□ 제한 정보 응답 헤더

응답 헤더:
X-RateLimit-Limit: 30
X-RateLimit-Remaining: 25
X-RateLimit-Reset: 1706400000

검증 기준:
- Rate Limit 헤더 확인
- Unity 클라이언트에서 처리
```

---

### 2.3 모니터링 및 로깅 (2일)

#### Task 2.3.1: 구조화된 로깅
```
파일: translation-server/src/logging_config.py

작업 내용:
□ structlog 설정
□ JSON 포맷 로깅
□ 요청/응답 로깅
□ 에러 로깅 (스택트레이스)

로그 필드:
- timestamp
- request_id
- user_id
- source_lang
- target_lang
- text_length
- latency_ms
- provider
- cache_hit
- error (있는 경우)

검증 기준:
- JSON 로그 파싱 가능
- ELK/Loki 연동 가능
```

#### Task 2.3.2: 메트릭 수집
```
파일: translation-server/src/metrics.py

작업 내용:
□ Prometheus 메트릭 클라이언트
□ 요청 수 카운터
□ 응답 시간 히스토그램
□ 캐시 히트율 게이지
□ 폴백 사용 카운터

메트릭:
- translation_requests_total{provider, source_lang, target_lang}
- translation_latency_seconds{provider}
- translation_cache_hit_ratio
- translation_fallback_total{from_provider, to_provider}
- translation_errors_total{error_type}

검증 기준:
- /metrics 엔드포인트 노출
- Prometheus 스크래핑 가능
```

---

## 📈 Phase 3: 품질 개선 (예상 10일)

### 3.1 학습 데이터 수집 (3일)

#### Task 3.1.1: 데이터 수집 파이프라인
```
파일: translation-server/training/data_collector.py

작업 내용:
□ 채팅 로그에서 번역 데이터 추출
□ 상용 API 번역 결과 저장
□ 데이터 포맷 변환
□ 중복 제거

데이터 형식:
{
  "source": "안녕하세요",
  "target": "Hello",
  "src_lang": "ko",
  "tgt_lang": "en",
  "domain": "chat",
  "quality_score": 0.95
}

목표:
- 언어쌍당 10,000+ 문장
- 게임/채팅 도메인 특화

검증 기준:
- 데이터 품질 검증 스크립트
- 중복률 < 5%
```

#### Task 3.1.2: 공개 데이터셋 수집
```
파일: translation-server/training/download_datasets.py

작업 내용:
□ Tatoeba 다운로드 (고품질 예문)
□ OPUS-100 다운로드 (대규모)
□ CCAligned 필터링 (채팅 도메인)
□ 데이터 병합 및 전처리

데이터셋:
- Tatoeba: ~500K 문장 (고품질)
- OPUS-100: 수백만 문장 (ko-en, ko-zh, ko-ja)
- CCAligned: 웹 크롤링 데이터 (필터링 필요)

검증 기준:
- 다운로드 자동화
- 전처리 파이프라인
```

#### Task 3.1.3: 데이터 검증 및 정제
```
파일: translation-server/training/data_cleaner.py

작업 내용:
□ 길이 기반 필터링 (너무 짧거나 긴 문장)
□ 언어 감지 검증
□ 욕설/스팸 필터링
□ 품질 점수 계산

필터링 기준:
- 최소 길이: 3자
- 최대 길이: 256자
- 언어 감지 신뢰도 > 0.8
- 금칙어 필터

검증 기준:
- 정제 전/후 데이터 품질 비교
- 필터링된 데이터 샘플 검토
```

---

### 3.2 QLoRA 파인튜닝 (5일)

#### Task 3.2.1: 학습 환경 설정
```
파일: translation-server/training/requirements.txt
      translation-server/training/config.yaml

작업 내용:
□ 학습용 의존성
  - peft==0.7.1
  - bitsandbytes==0.42.0
  - datasets==2.16.0
  - accelerate==0.26.0
  - wandb==0.16.2

□ 학습 설정 파일
  - 모델: facebook/m2m100_418M
  - LoRA r: 16
  - LoRA alpha: 32
  - Target modules: q_proj, v_proj
  - Batch size: 4
  - Gradient accumulation: 8
  - Learning rate: 2e-4
  - Epochs: 3

검증 기준:
- RTX 3060 12GB에서 OOM 없이 실행
- VRAM 사용량 < 10GB
```

#### Task 3.2.2: 학습 스크립트
```
파일: translation-server/training/train_qlora.py

작업 내용:
□ 데이터셋 로딩
□ 4-bit 양자화 설정
□ LoRA 어댑터 설정
□ Trainer 설정
□ 체크포인트 저장
□ WandB 로깅

주요 코드:
- BitsAndBytesConfig(load_in_4bit=True)
- LoraConfig(r=16, lora_alpha=32)
- Trainer with gradient checkpointing

검증 기준:
- 학습 손실 감소
- 체크포인트 저장
- WandB 대시보드
```

#### Task 3.2.3: 모델 평가
```
파일: translation-server/training/evaluate.py

작업 내용:
□ BLEU 스코어 계산
□ 테스트셋 평가
□ 언어쌍별 성능 비교
□ 베이스라인 대비 개선율

평가 메트릭:
- BLEU score
- chrF score
- 인간 평가 (샘플)

검증 기준:
- BLEU +5% 이상 개선
- 언어쌍별 리포트
```

#### Task 3.2.4: 모델 배포 준비
```
파일: translation-server/training/merge_adapter.py
      translation-server/training/export_model.py

작업 내용:
□ LoRA 어댑터 병합
□ 모델 저장 (safetensors)
□ 모델 버전 관리
□ 배포 스크립트

산출물:
- models/m2m100-418m-finetuned/
  ├── config.json
  ├── model.safetensors
  ├── sentencepiece.bpe.model
  └── special_tokens_map.json

검증 기준:
- 병합된 모델 추론 테스트
- 원본 대비 지연시간 비교
```

---

### 3.3 A/B 테스트 및 배포 (2일)

#### Task 3.3.1: A/B 테스트 설정
```
파일: translation-server/src/ab_test.py
      translation-server/src/main.py (수정)

작업 내용:
□ 모델 버전 관리
□ 트래픽 분배 (50/50)
□ 결과 로깅
□ 통계 분석

A/B 테스트 설정:
- 대조군: 기존 M2M-100
- 실험군: 파인튜닝 모델
- 메트릭: 응답시간, 사용자 피드백

검증 기준:
- 균등한 트래픽 분배
- 결과 통계 수집
```

#### Task 3.3.2: 점진적 배포
```
파일: translation-server/deploy/canary.sh

작업 내용:
□ Canary 배포 스크립트
□ 10% → 50% → 100% 롤아웃
□ 롤백 스크립트
□ 헬스체크 모니터링

배포 단계:
1. 10% 트래픽 (1일 모니터링)
2. 50% 트래픽 (1일 모니터링)
3. 100% 트래픽

검증 기준:
- 무중단 배포
- 문제 시 자동 롤백
```

---

## 📊 Phase 4: 운영 최적화 (예상 5일)

### 4.1 모니터링 대시보드 (2일)

#### Task 4.1.1: Grafana 대시보드
```
파일: translation-server/monitoring/grafana/dashboard.json

작업 내용:
□ 요청량 패널
□ 응답시간 패널
□ 캐시 히트율 패널
□ 에러율 패널
□ 폴백 사용 패널
□ 비용 추정 패널

대시보드 섹션:
1. Overview (주요 지표 요약)
2. Performance (응답시간, 처리량)
3. Cache (히트율, 메모리)
4. Fallback (사용량, 비용)
5. Errors (에러율, 타입별)

검증 기준:
- 실시간 데이터 표시
- 7일 히스토리
```

#### Task 4.1.2: 알림 규칙
```
파일: translation-server/monitoring/alertmanager/rules.yml

작업 내용:
□ 에러율 > 5% 알림
□ 응답시간 > 2초 알림
□ 캐시 히트율 < 50% 알림
□ 폴백 사용 > 30% 알림
□ GPU 메모리 > 90% 알림

알림 채널:
- Slack
- Email
- PagerDuty (선택)

검증 기준:
- 임계값 초과 시 알림 발생
- 알림 중복 방지
```

---

### 4.2 비용 관리 (1일)

#### Task 4.2.1: 비용 추적
```
파일: translation-server/src/cost_tracker.py

작업 내용:
□ API 사용량 추적
□ 비용 계산 (Azure, Gemini)
□ 일/월별 리포트
□ 예산 알림

비용 계산:
- Azure: $10/백만 자
- Gemini: $0.08/백만 토큰

검증 기준:
- 실시간 비용 추적
- 예산 초과 알림
```

#### Task 4.2.2: 비용 최적화
```
파일: translation-server/src/cost_optimizer.py

작업 내용:
□ 캐시 적극 활용 전략
□ 배치 처리 최적화
□ 로컬 모델 우선 라우팅
□ 비용 기반 폴백 순서

최적화 전략:
1. 동일 메시지 캐시 (24시간)
2. 인기 번역 프리캐싱
3. Azure 무료 한도 모니터링
4. 한도 초과 시 로컬 우선

검증 기준:
- 월 비용 < $10 (1만 사용자)
```

---

### 4.3 추가 언어 지원 (2일)

#### Task 4.3.1: 언어 확장 프레임워크
```
파일: translation-server/src/language_manager.py

작업 내용:
□ 언어 설정 관리
□ 동적 언어 추가
□ 언어별 품질 모니터링
□ 지원 언어 API

지원 언어 확장 계획:
- Phase 1: ko, en, zh, ja (4개)
- Phase 2: +es, pt, fr, de, ru (9개)
- Phase 3: +동남아 (vi, th, id) (12개)

검증 기준:
- 설정만으로 언어 추가
- 언어별 품질 리포트
```

#### Task 4.3.2: 문서화
```
파일: translation-server/docs/
      - README.md
      - API.md
      - DEPLOYMENT.md
      - TROUBLESHOOTING.md

작업 내용:
□ 설치 가이드
□ API 문서 (OpenAPI)
□ 배포 가이드
□ 문제 해결 가이드
□ 성능 튜닝 가이드

검증 기준:
- 신규 개발자 온보딩 가능
- API 문서 자동 생성
```

---

## 📅 전체 일정 요약

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          구현 타임라인                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  Week 1: Phase 1 - 기본 인프라                                           │
│  ├─ Day 1-2: 번역 서버 구축                                              │
│  ├─ Day 3: 캐싱 시스템                                                   │
│  └─ Day 4-5: Unity 클라이언트 통합                                       │
│                                                                         │
│  Week 2: Phase 2 - 신뢰성 강화                                           │
│  ├─ Day 1-2: 폴백 시스템                                                 │
│  ├─ Day 3: Rate Limiter                                                 │
│  └─ Day 4-5: 모니터링 및 로깅                                            │
│                                                                         │
│  Week 3-4: Phase 3 - 품질 개선                                           │
│  ├─ Day 1-3: 학습 데이터 수집                                            │
│  ├─ Day 4-8: QLoRA 파인튜닝                                              │
│  └─ Day 9-10: A/B 테스트 및 배포                                         │
│                                                                         │
│  Week 5: Phase 4 - 운영 최적화                                           │
│  ├─ Day 1-2: 모니터링 대시보드                                           │
│  ├─ Day 3: 비용 관리                                                     │
│  └─ Day 4-5: 추가 언어 및 문서화                                         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

총 예상 기간: 5주 (25일)
```

---

## ✅ 체크리스트

### Phase 1 완료 조건
- [ ] 번역 서버 Docker 실행
- [ ] 4개 언어 12방향 번역 동작
- [ ] Redis 캐싱 동작
- [ ] Unity 클라이언트 연동

### Phase 2 완료 조건
- [ ] Azure 폴백 동작
- [ ] Gemini 폴백 동작
- [ ] Rate Limiting 동작
- [ ] 메트릭 수집 동작

### Phase 3 완료 조건
- [ ] 10,000+ 학습 데이터 수집
- [ ] QLoRA 파인튜닝 완료
- [ ] BLEU +5% 개선
- [ ] 프로덕션 배포

### Phase 4 완료 조건
- [ ] Grafana 대시보드 구축
- [ ] 알림 시스템 동작
- [ ] 비용 추적 동작
- [ ] 문서화 완료

---

## 📁 최종 디렉토리 구조

```
translation-server/
├── docker-compose.yml
├── Dockerfile
├── requirements.txt
├── .env.example
├── src/
│   ├── main.py
│   ├── translator.py
│   ├── models.py
│   ├── config.py
│   ├── cache.py
│   ├── rate_limiter.py
│   ├── metrics.py
│   ├── logging_config.py
│   ├── ab_test.py
│   ├── cost_tracker.py
│   ├── cost_optimizer.py
│   ├── language_manager.py
│   └── fallback/
│       ├── chain.py
│       ├── azure.py
│       └── gemini.py
├── training/
│   ├── requirements.txt
│   ├── config.yaml
│   ├── data_collector.py
│   ├── download_datasets.py
│   ├── data_cleaner.py
│   ├── train_qlora.py
│   ├── evaluate.py
│   ├── merge_adapter.py
│   └── export_model.py
├── monitoring/
│   ├── grafana/
│   │   └── dashboard.json
│   └── alertmanager/
│       └── rules.yml
├── deploy/
│   └── canary.sh
├── docs/
│   ├── README.md
│   ├── API.md
│   ├── DEPLOYMENT.md
│   └── TROUBLESHOOTING.md
└── tests/
    ├── test_translator.py
    ├── test_cache.py
    ├── test_fallback.py
    └── test_rate_limiter.py

unity-client/Assets/Plugins/UniversalChat/Runtime/Translation/
├── ITranslationService.cs
├── TranslationService.cs
├── TranslationConfig.cs
├── TranslationResult.cs
└── TranslationUI.cs
```

---

**작성자**: Claude Code
**버전**: 1.0.0
