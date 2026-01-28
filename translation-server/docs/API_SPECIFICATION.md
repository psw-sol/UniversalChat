# M2M-100 Translation Server API Specification

## 개요

M2M-100 기반 다국어 번역 서버 API 명세서입니다. Fine-tuned LoRA 모델을 지원하며, Azure Translator 및 Gemini API를 fallback으로 사용합니다.

**Base URL**: `http://localhost:8000`
**API Version**: v1
**Content-Type**: `application/json`

---

## 인증

현재 버전에서는 인증이 필요하지 않습니다. Rate limiting은 클라이언트 IP 기반으로 적용됩니다.

---

## Rate Limiting

| 제한 유형 | 한도 | 윈도우 |
|-----------|------|--------|
| 분당 요청 | 30회 | 60초 (슬라이딩 윈도우) |
| 시간당 요청 | 500회 | 3600초 (슬라이딩 윈도우) |
| 일일 요청 | 5000회 | 86400초 (슬라이딩 윈도우) |

Rate limit 초과 시 `429 Too Many Requests` 응답이 반환됩니다.

---

## Endpoints

### 1. Health Check

서버 상태 및 모델 로딩 상태를 확인합니다.

```
GET /health
```

#### Response

```json
{
  "status": "healthy",
  "model_loaded": true,
  "gpu_available": true,
  "redis_connected": true,
  "timestamp": "2025-01-28T12:00:00.000000"
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| status | string | 서버 상태 (`healthy`, `unhealthy`) |
| model_loaded | boolean | 번역 모델 로딩 여부 |
| gpu_available | boolean | GPU 사용 가능 여부 |
| redis_connected | boolean | Redis 연결 상태 |
| timestamp | string | ISO 8601 형식의 타임스탬프 |

---

### 2. 지원 언어 목록

지원하는 언어 코드 목록을 반환합니다.

```
GET /languages
```

#### Response

```json
{
  "supported_languages": ["ko", "en", "zh", "ja", "es", "pt", "fr", "de", "ru", "vi", "th", "id"],
  "primary_languages": ["ko", "en", "zh", "ja"]
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| supported_languages | string[] | 지원되는 모든 언어 코드 (ISO 639-1) |
| primary_languages | string[] | 주요 지원 언어 (최적화된 번역 품질) |

#### 언어 코드 참조

| 코드 | 언어 |
|------|------|
| ko | 한국어 |
| en | 영어 |
| zh | 중국어 |
| ja | 일본어 |
| es | 스페인어 |
| pt | 포르투갈어 |
| fr | 프랑스어 |
| de | 독일어 |
| ru | 러시아어 |
| vi | 베트남어 |
| th | 태국어 |
| id | 인도네시아어 |

---

### 3. 단일 번역

텍스트를 번역합니다.

```
POST /translate
```

#### Request Body

```json
{
  "text": "안녕하세요",
  "source_lang": "ko",
  "target_lang": "en"
}
```

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| text | string | ✅ | 번역할 텍스트 (최대 256자) |
| source_lang | string | ✅ | 원본 언어 코드 |
| target_lang | string | ✅ | 대상 언어 코드 |

#### Response (200 OK)

```json
{
  "translated_text": "Hello",
  "source_lang": "ko",
  "target_lang": "en",
  "provider": "m2m100",
  "cache_hit": false,
  "latency_ms": 125.5,
  "fallback_used": false
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| translated_text | string | 번역된 텍스트 |
| source_lang | string | 원본 언어 코드 |
| target_lang | string | 대상 언어 코드 |
| provider | string | 사용된 번역 제공자 (`m2m100`, `azure`, `gemini`) |
| cache_hit | boolean | 캐시 히트 여부 |
| latency_ms | float | 응답 시간 (밀리초) |
| fallback_used | boolean | fallback 사용 여부 |

#### Error Response (400 Bad Request)

```json
{
  "detail": "Unsupported source language: xx"
}
```

#### Error Response (429 Too Many Requests)

```json
{
  "detail": "Rate limit exceeded"
}
```

---

### 4. 배치 번역

여러 텍스트를 한 번에 번역합니다.

```
POST /translate/batch
```

#### Request Body

```json
{
  "texts": ["안녕하세요", "감사합니다", "좋은 하루 되세요"],
  "source_lang": "ko",
  "target_lang": "en"
}
```

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| texts | string[] | ✅ | 번역할 텍스트 배열 (최대 100개) |
| source_lang | string | ✅ | 원본 언어 코드 |
| target_lang | string | ✅ | 대상 언어 코드 |

#### Response (200 OK)

```json
{
  "translations": [
    {
      "translated_text": "Hello",
      "source_lang": "ko",
      "target_lang": "en",
      "provider": "m2m100",
      "cache_hit": false,
      "latency_ms": 125.5,
      "fallback_used": false
    },
    {
      "translated_text": "Thank you",
      "source_lang": "ko",
      "target_lang": "en",
      "provider": "m2m100",
      "cache_hit": true,
      "latency_ms": 2.1,
      "fallback_used": false
    }
  ],
  "total_count": 3,
  "success_count": 3,
  "total_latency_ms": 250.3
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| translations | object[] | 번역 결과 배열 |
| total_count | integer | 요청된 텍스트 수 |
| success_count | integer | 성공한 번역 수 |
| total_latency_ms | float | 총 처리 시간 (밀리초) |

---

### 5. 캐시 통계

Redis 캐시 통계를 조회합니다.

```
GET /cache/stats
```

#### Response

```json
{
  "enabled": true,
  "hit_count": 1500,
  "miss_count": 300,
  "hit_rate": 0.833,
  "ttl_seconds": 86400
}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| enabled | boolean | 캐시 활성화 여부 |
| hit_count | integer | 캐시 히트 수 |
| miss_count | integer | 캐시 미스 수 |
| hit_rate | float | 캐시 히트율 (0.0 ~ 1.0) |
| ttl_seconds | integer | 캐시 TTL (초) |

---

### 6. Rate Limit 상태

현재 클라이언트의 Rate limit 상태를 조회합니다.

```
GET /rate-limit/status
```

#### Response

```json
{
  "client_ip": "127.0.0.1",
  "limits": {
    "per_minute": {
      "limit": 30,
      "remaining": 25,
      "reset_at": "2025-01-28T12:01:00.000000"
    },
    "per_hour": {
      "limit": 500,
      "remaining": 480,
      "reset_at": "2025-01-28T13:00:00.000000"
    },
    "per_day": {
      "limit": 5000,
      "remaining": 4800,
      "reset_at": "2025-01-29T00:00:00.000000"
    }
  }
}
```

---

### 7. Fallback 통계

Fallback 제공자 사용 통계를 조회합니다.

```
GET /fallback/stats
```

#### Response

```json
{
  "providers": {
    "m2m100": {
      "total_requests": 10000,
      "success_count": 9800,
      "failure_count": 200,
      "success_rate": 0.98,
      "avg_latency_ms": 150.5
    },
    "azure": {
      "total_requests": 200,
      "success_count": 195,
      "failure_count": 5,
      "success_rate": 0.975,
      "avg_latency_ms": 250.3
    },
    "gemini": {
      "total_requests": 5,
      "success_count": 5,
      "failure_count": 0,
      "success_rate": 1.0,
      "avg_latency_ms": 800.2
    }
  },
  "fallback_chain": ["m2m100", "azure", "gemini"]
}
```

---

### 8. Prometheus 메트릭

Prometheus 형식의 메트릭을 반환합니다.

```
GET /metrics
```

#### Response (text/plain)

```
# HELP translation_requests_total Total translation requests
# TYPE translation_requests_total counter
translation_requests_total{provider="m2m100",status="success"} 9800
translation_requests_total{provider="m2m100",status="failure"} 200
translation_requests_total{provider="azure",status="success"} 195

# HELP translation_latency_seconds Translation request latency
# TYPE translation_latency_seconds histogram
translation_latency_seconds_bucket{le="0.1"} 5000
translation_latency_seconds_bucket{le="0.5"} 9000
translation_latency_seconds_bucket{le="1.0"} 9800

# HELP cache_hits_total Cache hit count
# TYPE cache_hits_total counter
cache_hits_total 1500
```

---

## 에러 코드

| HTTP 상태 | 설명 | 예시 메시지 |
|-----------|------|-------------|
| 400 | 잘못된 요청 | `Unsupported source language: xx` |
| 422 | 유효성 검사 실패 | `field required` |
| 429 | Rate limit 초과 | `Rate limit exceeded` |
| 500 | 서버 내부 오류 | `Translation failed` |
| 503 | 서비스 불가 | `Model not loaded` |

---

## Fallback 체인

번역 요청 시 다음 순서로 제공자를 시도합니다:

1. **M2M-100** (Primary) - Fine-tuned 로컬 모델
   - Timeout: 5초
   - GPU 가속 지원

2. **Azure Translator** (Secondary) - Microsoft Azure API
   - Timeout: 5초
   - API 키 필요 (`AZURE_TRANSLATOR_KEY`)

3. **Gemini** (Tertiary) - Google Gemini API
   - Timeout: 10초
   - API 키 필요 (`GEMINI_API_KEY`)

모든 제공자 실패 시 `500 Internal Server Error`가 반환됩니다.

---

## 캐싱 전략

- **캐시 키**: `translate:{source_lang}:{target_lang}:{text_hash}`
- **TTL**: 24시간 (86400초, 설정 가능)
- **저장소**: Redis
- **캐시 히트 시**: 약 2~5ms 응답

---

## 버전 정보

| 항목 | 버전 |
|------|------|
| API 버전 | v1 |
| 기본 모델 | facebook/m2m100_418M |
| Fine-tuned 모델 | m2m100-finetuned-v2 (BLEU 22.50) |
| 서버 프레임워크 | FastAPI |

---

## 변경 이력

| 날짜 | 버전 | 변경 내용 |
|------|------|----------|
| 2025-01-28 | v1.0 | 초기 API 명세 작성 |
| 2025-01-28 | v1.1 | Fine-tuned LoRA 모델 지원 추가 |
