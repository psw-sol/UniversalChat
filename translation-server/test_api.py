"""API Test Script"""
import requests

BASE_URL = "http://localhost:8000"

print("=" * 50)
print("Translation Server API Test")
print("=" * 50)

# 1. Health Check
print("\n[1] Health Check")
r = requests.get(f"{BASE_URL}/health")
print(f"  Status: {r.json()['status']}")
print(f"  Model Loaded: {r.json()['model_loaded']}")
print(f"  GPU Available: {r.json()['gpu_available']}")

# 2. Languages
print("\n[2] Supported Languages")
r = requests.get(f"{BASE_URL}/languages")
print(f"  Primary: {r.json()['primary']}")
print(f"  Total Pairs: {r.json()['pairs']}")

# 3. Single Translation
print("\n[3] Single Translation (ko→en)")
r = requests.post(f"{BASE_URL}/translate", json={
    "text": "안녕하세요, 반갑습니다.",
    "source_lang": "ko",
    "target_lang": "en"
})
result = r.json()
print(f"  Input: 안녕하세요, 반갑습니다.")
print(f"  Output: {result['translated_text']}")
print(f"  Provider: {result['provider']}")
print(f"  Latency: {result['latency_ms']:.1f}ms")
print(f"  Cache Hit: {result['cache_hit']}")

# 4. Batch Translation
print("\n[4] Batch Translation (ko→en)")
texts = ["안녕하세요", "감사합니다", "좋은 하루 되세요"]
r = requests.post(f"{BASE_URL}/translate/batch", json={
    "texts": texts,
    "source_lang": "ko",
    "target_lang": "en"
})
result = r.json()
for i, t in enumerate(result['translations']):
    print(f"  [{i+1}] {texts[i]} → {t}")
print(f"  Provider: {result['provider']}")
print(f"  Cache Hits: {result['cache_hits']}")
print(f"  Latency: {result['latency_ms']:.1f}ms")

# 5. Cache Stats
print("\n[5] Cache Statistics")
r = requests.get(f"{BASE_URL}/cache/stats")
stats = r.json()
print(f"  Hits: {stats['hits']}")
print(f"  Misses: {stats['misses']}")
print(f"  Hit Rate: {stats['hit_rate']:.1f}%")

# 6. Rate Limit Status
print("\n[6] Rate Limit Status")
r = requests.get(f"{BASE_URL}/rate-limit/status")
status = r.json()
print(f"  User ID: {status['user_id']}")
usage = status['usage']
print(f"  Per Minute: {usage['minute']['remaining']}/{usage['minute']['limit']}")
print(f"  Per Hour: {usage['hour']['remaining']}/{usage['hour']['limit']}")
print(f"  Per Day: {usage['day']['remaining']}/{usage['day']['limit']}")

# 7. Cache Hit Test (repeat same translation)
print("\n[7] Cache Hit Test (repeat same request)")
r = requests.post(f"{BASE_URL}/translate", json={
    "text": "안녕하세요, 반갑습니다.",
    "source_lang": "ko",
    "target_lang": "en"
})
result = r.json()
print(f"  Output: {result['translated_text']}")
print(f"  Cache Hit: {result['cache_hit']}")
print(f"  Latency: {result['latency_ms']:.1f}ms")

print("\n" + "=" * 50)
print("All tests completed!")
print("=" * 50)
