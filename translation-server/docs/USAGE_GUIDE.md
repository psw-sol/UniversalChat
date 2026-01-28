# M2M-100 Translation Server 사용 가이드

## 목차

1. [시스템 요구사항](#시스템-요구사항)
2. [설치](#설치)
3. [설정](#설정)
4. [서버 실행](#서버-실행)
5. [사용 예시](#사용-예시)
6. [모델 Fine-tuning](#모델-fine-tuning)
7. [모니터링](#모니터링)
8. [문제 해결](#문제-해결)

---

## 시스템 요구사항

### 하드웨어

| 구성요소 | 최소 사양 | 권장 사양 |
|----------|----------|----------|
| CPU | 4코어 | 8코어 이상 |
| RAM | 8GB | 16GB 이상 |
| GPU | - | NVIDIA GPU (VRAM 6GB+) |
| 스토리지 | 10GB | 20GB SSD |

### 소프트웨어

- Python 3.10 이상
- CUDA 11.8+ (GPU 사용 시)
- Redis 6.0 이상
- Docker & Docker Compose (선택사항)

---

## 설치

### 1. 저장소 클론

```bash
git clone <repository-url>
cd translation-server
```

### 2. 가상환경 생성 및 활성화

```bash
# Windows
python -m venv venv
venv\Scripts\activate

# Linux/Mac
python -m venv venv
source venv/bin/activate
```

### 3. 의존성 설치

```bash
# 기본 설치
pip install -r requirements.txt

# GPU 지원 (CUDA 11.8)
pip install torch --index-url https://download.pytorch.org/whl/cu118
```

### 4. Redis 설치 및 실행

```bash
# Docker를 사용하는 경우
docker run -d --name redis -p 6379:6379 redis:latest

# 또는 로컬 설치 (Ubuntu)
sudo apt-get install redis-server
sudo systemctl start redis
```

---

## 설정

### 환경 변수 설정

`.env.example`을 `.env`로 복사하고 설정값을 수정합니다:

```bash
cp .env.example .env
```

### 필수 설정

```env
# 모델 설정
MODEL_NAME=facebook/m2m100_418M
FINETUNED_MODEL_PATH=./training/outputs/m2m100-finetuned-v2
USE_QUANTIZATION=true
DEVICE=cuda

# Redis 설정
REDIS_URL=redis://localhost:6379/0
```

### 선택 설정

```env
# Fine-tuned 모델 경로 (비어있으면 기본 모델 사용)
FINETUNED_MODEL_PATH=./training/outputs/m2m100-finetuned-v2

# 4-bit 양자화 (메모리 효율)
USE_QUANTIZATION=true

# 최대 텍스트 길이
MAX_LENGTH=256

# 배치 크기
BATCH_SIZE=8

# 캐시 TTL (초)
CACHE_TTL=86400

# Rate Limiting
RATE_LIMIT_PER_MINUTE=30
RATE_LIMIT_PER_HOUR=500
RATE_LIMIT_PER_DAY=5000

# 로그 레벨
LOG_LEVEL=info
```

### Fallback API 설정 (선택사항)

```env
# Azure Translator
AZURE_TRANSLATOR_KEY=your-azure-key
AZURE_TRANSLATOR_REGION=koreacentral
AZURE_TRANSLATOR_ENDPOINT=https://api.cognitive.microsofttranslator.com

# Gemini
GEMINI_API_KEY=your-gemini-key
```

---

## 서버 실행

### 개발 모드

```bash
# uvicorn 직접 실행
uvicorn src.main:app --reload --host 0.0.0.0 --port 8000

# 또는 Python으로 실행
python -m src.main
```

### 프로덕션 모드

```bash
# Gunicorn + Uvicorn workers
gunicorn src.main:app -w 4 -k uvicorn.workers.UvicornWorker -b 0.0.0.0:8000

# 또는 Docker Compose
docker-compose up -d
```

### 서버 상태 확인

```bash
curl http://localhost:8000/health
```

예상 응답:
```json
{
  "status": "healthy",
  "model_loaded": true,
  "gpu_available": true,
  "redis_connected": true
}
```

---

## 사용 예시

### Python

```python
import requests

# 단일 번역
response = requests.post(
    "http://localhost:8000/translate",
    json={
        "text": "안녕하세요, 반갑습니다.",
        "source_lang": "ko",
        "target_lang": "en"
    }
)
result = response.json()
print(result["translated_text"])  # "Hello, nice to meet you."

# 배치 번역
response = requests.post(
    "http://localhost:8000/translate/batch",
    json={
        "texts": ["안녕하세요", "감사합니다", "좋은 하루 되세요"],
        "source_lang": "ko",
        "target_lang": "en"
    }
)
results = response.json()
for t in results["translations"]:
    print(t["translated_text"])
```

### cURL

```bash
# 단일 번역
curl -X POST http://localhost:8000/translate \
  -H "Content-Type: application/json" \
  -d '{"text": "안녕하세요", "source_lang": "ko", "target_lang": "en"}'

# 배치 번역
curl -X POST http://localhost:8000/translate/batch \
  -H "Content-Type: application/json" \
  -d '{"texts": ["안녕하세요", "감사합니다"], "source_lang": "ko", "target_lang": "en"}'

# 지원 언어 조회
curl http://localhost:8000/languages

# Rate limit 상태 확인
curl http://localhost:8000/rate-limit/status
```

### JavaScript (Node.js)

```javascript
const axios = require('axios');

async function translate(text, sourceLang, targetLang) {
    const response = await axios.post('http://localhost:8000/translate', {
        text: text,
        source_lang: sourceLang,
        target_lang: targetLang
    });
    return response.data.translated_text;
}

// 사용
const result = await translate('안녕하세요', 'ko', 'en');
console.log(result);  // "Hello"
```

### Unity C#

```csharp
using UnityEngine;
using UnityEngine.Networking;
using System.Collections;

[System.Serializable]
public class TranslationRequest
{
    public string text;
    public string source_lang;
    public string target_lang;
}

[System.Serializable]
public class TranslationResponse
{
    public string translated_text;
    public string provider;
    public bool cache_hit;
    public float latency_ms;
}

public class TranslationClient : MonoBehaviour
{
    private const string BASE_URL = "http://localhost:8000";

    public IEnumerator Translate(string text, string sourceLang, string targetLang,
                                  System.Action<string> onSuccess,
                                  System.Action<string> onError)
    {
        var request = new TranslationRequest
        {
            text = text,
            source_lang = sourceLang,
            target_lang = targetLang
        };

        string json = JsonUtility.ToJson(request);

        using (UnityWebRequest www = new UnityWebRequest($"{BASE_URL}/translate", "POST"))
        {
            byte[] bodyRaw = System.Text.Encoding.UTF8.GetBytes(json);
            www.uploadHandler = new UploadHandlerRaw(bodyRaw);
            www.downloadHandler = new DownloadHandlerBuffer();
            www.SetRequestHeader("Content-Type", "application/json");

            yield return www.SendWebRequest();

            if (www.result == UnityWebRequest.Result.Success)
            {
                var response = JsonUtility.FromJson<TranslationResponse>(www.downloadHandler.text);
                onSuccess?.Invoke(response.translated_text);
            }
            else
            {
                onError?.Invoke(www.error);
            }
        }
    }
}
```

---

## 모델 Fine-tuning

### 1. 학습 데이터 준비

`training/data/` 폴더에 JSONL 형식의 학습 데이터를 준비합니다:

```jsonl
{"source": "안녕하세요", "target": "Hello", "source_lang": "ko", "target_lang": "en"}
{"source": "감사합니다", "target": "Thank you", "source_lang": "ko", "target_lang": "en"}
```

### 2. 데이터 전처리

```bash
cd training
python preprocess_data.py
```

### 3. QLoRA Fine-tuning 실행

```bash
# 초기 학습 (3 epochs)
python train_qlora.py

# 추가 학습 (체크포인트에서 계속)
python continue_training.py
```

### 4. 학습 결과 확인

```bash
python evaluate.py --model_path ./outputs/m2m100-finetuned-v2
```

### 5. 서버에 적용

`.env` 파일에서 모델 경로를 업데이트:

```env
FINETUNED_MODEL_PATH=./training/outputs/m2m100-finetuned-v2
```

서버를 재시작하면 fine-tuned 모델이 자동으로 로드됩니다.

---

## 모니터링

### Prometheus 메트릭

`/metrics` 엔드포인트에서 Prometheus 형식의 메트릭을 제공합니다.

```bash
curl http://localhost:8000/metrics
```

### 주요 메트릭

| 메트릭 | 설명 |
|--------|------|
| `translation_requests_total` | 총 번역 요청 수 |
| `translation_latency_seconds` | 번역 응답 시간 |
| `cache_hits_total` | 캐시 히트 수 |
| `fallback_requests_total` | Fallback 사용 횟수 |

### Grafana 대시보드

Prometheus와 Grafana를 연동하여 실시간 모니터링 대시보드를 구성할 수 있습니다.

### 로그 확인

```bash
# 실시간 로그 확인
tail -f logs/translation-server.log

# 에러 로그만 필터링
grep ERROR logs/translation-server.log
```

---

## 문제 해결

### 1. 모델 로딩 실패

**증상**: `Model not loaded` 에러

**해결책**:
```bash
# GPU 메모리 확인
nvidia-smi

# 양자화 활성화 (메모리 절약)
USE_QUANTIZATION=true

# CPU 모드로 전환 (GPU 문제 시)
DEVICE=cpu
```

### 2. Redis 연결 실패

**증상**: `Redis connection failed` 에러

**해결책**:
```bash
# Redis 상태 확인
redis-cli ping

# Redis 재시작
docker restart redis

# 연결 URL 확인
echo $REDIS_URL
```

### 3. Rate Limit 초과

**증상**: `429 Too Many Requests` 에러

**해결책**:
```bash
# Rate limit 상태 확인
curl http://localhost:8000/rate-limit/status

# 제한 완화 (.env)
RATE_LIMIT_PER_MINUTE=60
RATE_LIMIT_PER_HOUR=1000
```

### 4. 번역 품질 저하

**증상**: 번역 결과가 부정확함

**해결책**:
- Fine-tuned 모델 사용 확인
- 지원 언어 쌍 확인 (주요 언어: ko, en, zh, ja)
- 입력 텍스트 길이 확인 (MAX_LENGTH 이내)

### 5. GPU 메모리 부족

**증상**: `CUDA out of memory` 에러

**해결책**:
```bash
# 배치 크기 감소
BATCH_SIZE=4

# 4-bit 양자화 활성화
USE_QUANTIZATION=true

# 최대 길이 감소
MAX_LENGTH=128
```

### 6. Fallback API 실패

**증상**: 모든 제공자 실패

**해결책**:
```bash
# API 키 확인
echo $AZURE_TRANSLATOR_KEY
echo $GEMINI_API_KEY

# Fallback 통계 확인
curl http://localhost:8000/fallback/stats
```

---

## 성능 최적화 팁

### 1. 캐시 활용

동일한 번역 요청은 캐시되므로, 반복 요청 시 빠른 응답을 받을 수 있습니다.

### 2. 배치 요청 사용

여러 텍스트를 번역할 때는 `/translate/batch` 엔드포인트를 사용하세요.

### 3. GPU 가속

CUDA 지원 GPU가 있다면 `DEVICE=cuda`로 설정하여 번역 속도를 높일 수 있습니다.

### 4. 양자화

`USE_QUANTIZATION=true`로 설정하면 메모리 사용량을 줄이면서 비슷한 성능을 유지할 수 있습니다.

---

## 부록: 디렉토리 구조

```
translation-server/
├── src/
│   ├── __init__.py
│   ├── main.py           # FastAPI 애플리케이션
│   ├── config.py         # 설정 관리
│   ├── translator.py     # 번역 엔진
│   ├── models.py         # Pydantic 모델
│   ├── cache.py          # Redis 캐시
│   ├── rate_limiter.py   # Rate limiting
│   └── fallback/
│       ├── __init__.py
│       ├── chain.py      # Fallback 체인
│       ├── azure.py      # Azure Translator
│       └── gemini.py     # Gemini API
├── training/
│   ├── data/             # 학습 데이터
│   ├── outputs/          # 학습된 모델
│   ├── train_qlora.py    # QLoRA 학습 스크립트
│   └── continue_training.py
├── docs/
│   ├── API_SPECIFICATION.md
│   └── USAGE_GUIDE.md
├── tests/
├── .env.example
├── requirements.txt
└── docker-compose.yml
```

---

## 지원 및 문의

- **이슈 리포트**: GitHub Issues
- **문서**: `/docs` 폴더 참조
- **API 스펙**: `docs/API_SPECIFICATION.md`

---

*최종 업데이트: 2025-01-28*
