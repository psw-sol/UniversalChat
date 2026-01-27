# M2M-100 Translation Server

고성능 채팅 번역 서버. Meta의 M2M-100 모델을 사용하여 100개 언어 간 번역을 지원합니다.

## 특징

- **M2M-100-418M**: MIT 라이선스, 상업적 사용 가능
- **4개 주요 언어**: 한국어, 영어, 중국어, 일본어
- **Redis 캐싱**: 24시간 TTL, 빠른 응답
- **GPU 지원**: CUDA를 통한 빠른 추론
- **Docker**: GPU/CPU 버전 모두 지원

## 빠른 시작

### Docker로 실행 (권장)

```bash
# GPU 버전
docker compose up -d

# CPU 버전
docker compose --profile cpu up -d translation-server-cpu redis
```

### 로컬 실행

```bash
# 가상환경 생성
python -m venv venv
source venv/bin/activate  # Windows: venv\Scripts\activate

# 의존성 설치
pip install -r requirements.txt

# 환경변수 설정
cp .env.example .env

# Redis 실행 (별도 터미널)
docker run -d -p 6379:6379 redis:7-alpine

# 서버 실행
uvicorn src.main:app --reload --host 0.0.0.0 --port 8000
```

## API 엔드포인트

### 번역

```bash
# 단일 번역
curl -X POST http://localhost:8000/translate \
  -H "Content-Type: application/json" \
  -d '{"text": "안녕하세요", "source_lang": "ko", "target_lang": "en"}'

# 응답
{
  "translated_text": "Hello",
  "source_lang": "ko",
  "target_lang": "en",
  "provider": "m2m100",
  "cache_hit": false,
  "latency_ms": 125.5
}
```

### 배치 번역

```bash
curl -X POST http://localhost:8000/translate/batch \
  -H "Content-Type: application/json" \
  -d '{
    "texts": ["안녕하세요", "감사합니다"],
    "source_lang": "ko",
    "target_lang": "en"
  }'
```

### 시스템

```bash
# 헬스체크
curl http://localhost:8000/health

# 지원 언어
curl http://localhost:8000/languages

# 캐시 통계
curl http://localhost:8000/cache/stats
```

## 지원 언어

| 코드 | 언어 | 우선순위 |
|------|------|----------|
| ko | 한국어 | Primary |
| en | 영어 | Primary |
| zh | 중국어 | Primary |
| ja | 일본어 | Primary |
| es | 스페인어 | Extended |
| pt | 포르투갈어 | Extended |
| fr | 프랑스어 | Extended |
| de | 독일어 | Extended |
| ru | 러시아어 | Extended |
| vi | 베트남어 | Extended |
| th | 태국어 | Extended |
| id | 인도네시아어 | Extended |

## 설정

`.env` 파일에서 설정:

```bash
# 모델
MODEL_NAME=facebook/m2m100_418M
DEVICE=cuda  # or cpu
MAX_LENGTH=256

# Redis
REDIS_URL=redis://localhost:6379/0
CACHE_TTL=86400  # 24시간

# Rate Limiting
RATE_LIMIT_PER_MINUTE=30
RATE_LIMIT_PER_HOUR=500
RATE_LIMIT_PER_DAY=5000

# 로깅
LOG_LEVEL=info
```

## 성능

| 환경 | 응답 시간 | 처리량 |
|------|----------|--------|
| GPU (RTX 3060) | ~100ms | ~10 req/s |
| CPU (Ryzen 7) | ~500ms | ~2 req/s |
| 캐시 히트 | <10ms | ~1000 req/s |

## 아키텍처

```
┌─────────────────────────────────────────────────────┐
│                 Translation Server                   │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐ │
│  │   FastAPI   │→│  Translator │→│   M2M-100   │ │
│  │   (8000)    │  │   Engine    │  │   Model     │ │
│  └──────┬──────┘  └─────────────┘  └─────────────┘ │
│         │                                           │
│  ┌──────▼──────┐                                   │
│  │    Redis    │  Cache Layer (TTL: 24h)           │
│  │   (6379)    │                                   │
│  └─────────────┘                                   │
└─────────────────────────────────────────────────────┘
```

## 라이선스

- **M2M-100**: MIT License (상업적 사용 가능)
- **이 코드**: MIT License
