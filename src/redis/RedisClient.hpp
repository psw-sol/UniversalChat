#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>

#ifdef ENABLE_REDIS
#include <hiredis/hiredis.h>
#endif

namespace chat {

/**
 * RedisClient
 *
 * Thread-safe Redis client wrapper using hiredis.
 * Provides connection management and common Redis operations.
 */
class RedisClient {
public:
    struct Config {
        std::string host = "127.0.0.1";
        int port = 6379;
        std::string password;
        int db = 0;
        int connect_timeout_ms = 5000;
        int command_timeout_ms = 1000;
    };

    explicit RedisClient(const Config& config);
    ~RedisClient();

    // Non-copyable
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    // Connection
    bool connect();
    void disconnect();
    bool isConnected() const;
    bool reconnect();
    bool ping();

    // String operations
    bool set(const std::string& key, const std::string& value, int expire_seconds = 0);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    bool expire(const std::string& key, int seconds);

    // List operations (for message history)
    bool lpush(const std::string& key, const std::string& value);
    bool rpush(const std::string& key, const std::string& value);
    std::optional<std::string> lpop(const std::string& key);
    std::optional<std::string> rpop(const std::string& key);
    std::vector<std::string> lrange(const std::string& key, int start, int stop);
    bool ltrim(const std::string& key, int start, int stop);
    int64_t llen(const std::string& key);

    // Hash operations
    bool hset(const std::string& key, const std::string& field, const std::string& value);
    bool hmset(const std::string& key, const std::unordered_map<std::string, std::string>& fields);
    std::optional<std::string> hget(const std::string& key, const std::string& field);
    bool hdel(const std::string& key, const std::string& field);
    std::unordered_map<std::string, std::string> hgetall(const std::string& key);

    // Set operations (for channel members)
    int64_t sadd(const std::string& key, const std::string& member);
    int64_t srem(const std::string& key, const std::string& member);
    bool sismember(const std::string& key, const std::string& member);
    int64_t scard(const std::string& key);
    std::vector<std::string> smembers(const std::string& key);
    std::pair<std::string, std::vector<std::string>> sscan(const std::string& key,
        const std::string& cursor, const std::string& pattern = "", int count = 10);

    // Sorted set operations (for ordered messages)
    bool zadd(const std::string& key, double score, const std::string& member);
    std::vector<std::string> zrange(const std::string& key, int start, int stop);
    std::vector<std::string> zrevrange(const std::string& key, int start, int stop);
    bool zremrangebyrank(const std::string& key, int start, int stop);
    int64_t zcard(const std::string& key);

    // Pipeline support
    class Pipeline {
    public:
        Pipeline(RedisClient& client);
        ~Pipeline();

        Pipeline& lpush(const std::string& key, const std::string& value);
        Pipeline& ltrim(const std::string& key, int start, int stop);
        Pipeline& expire(const std::string& key, int seconds);
        Pipeline& zadd(const std::string& key, double score, const std::string& member);
        Pipeline& zremrangebyrank(const std::string& key, int start, int stop);

        bool execute();

    private:
        RedisClient& client_;
        std::vector<std::function<bool()>> commands_;
    };

    Pipeline pipeline();

    // Error handling
    std::string lastError() const { return last_error_; }

private:
#ifdef ENABLE_REDIS
    redisContext* context_ = nullptr;
    bool executeCommand(const char* format, ...);
    redisReply* executeCommandWithReply(const char* format, ...);
    void freeReply(redisReply* reply);
#endif

    Config config_;
    mutable std::mutex mutex_;
    std::string last_error_;
    bool connected_ = false;
};

} // namespace chat
