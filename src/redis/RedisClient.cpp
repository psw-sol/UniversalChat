#include "RedisClient.hpp"
#include "../util/Logger.hpp"
#include <cstdarg>

namespace chat {

RedisClient::RedisClient(const Config& config)
    : config_(config)
{
}

RedisClient::~RedisClient() {
    disconnect();
}

#ifdef ENABLE_REDIS

bool RedisClient::connect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (connected_ && context_) {
        return true;
    }

    // Connect with timeout
    struct timeval timeout = {
        config_.connect_timeout_ms / 1000,
        (config_.connect_timeout_ms % 1000) * 1000
    };

    context_ = redisConnectWithTimeout(
        config_.host.c_str(),
        config_.port,
        timeout
    );

    if (!context_ || context_->err) {
        last_error_ = context_ ? context_->errstr : "Failed to allocate redis context";
        LOG_ERROR("Redis connection failed: {}", last_error_);
        if (context_) {
            redisFree(context_);
            context_ = nullptr;
        }
        connected_ = false;
        return false;
    }

    // Set command timeout
    struct timeval cmd_timeout = {
        config_.command_timeout_ms / 1000,
        (config_.command_timeout_ms % 1000) * 1000
    };
    redisSetTimeout(context_, cmd_timeout);

    // Authenticate if password is set
    if (!config_.password.empty()) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(context_, "AUTH %s", config_.password.c_str())
        );

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            last_error_ = reply ? reply->str : "AUTH failed";
            LOG_ERROR("Redis AUTH failed: {}", last_error_);
            if (reply) freeReplyObject(reply);
            redisFree(context_);
            context_ = nullptr;
            connected_ = false;
            return false;
        }
        freeReplyObject(reply);
    }

    // Select database
    if (config_.db != 0) {
        redisReply* reply = static_cast<redisReply*>(
            redisCommand(context_, "SELECT %d", config_.db)
        );

        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            last_error_ = reply ? reply->str : "SELECT failed";
            LOG_ERROR("Redis SELECT failed: {}", last_error_);
            if (reply) freeReplyObject(reply);
            redisFree(context_);
            context_ = nullptr;
            connected_ = false;
            return false;
        }
        freeReplyObject(reply);
    }

    connected_ = true;
    LOG_INFO("Redis connected to {}:{} (db={})", config_.host, config_.port, config_.db);
    return true;
}

void RedisClient::disconnect() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (context_) {
        redisFree(context_);
        context_ = nullptr;
    }
    connected_ = false;
}

bool RedisClient::isConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && context_ && context_->err == 0;
}

bool RedisClient::reconnect() {
    disconnect();
    return connect();
}

bool RedisClient::ping() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!context_) return false;

    redisReply* reply = static_cast<redisReply*>(redisCommand(context_, "PING"));
    if (!reply) {
        last_error_ = "PING failed: no reply";
        return false;
    }

    bool success = (reply->type == REDIS_REPLY_STATUS &&
                   std::string(reply->str) == "PONG");
    freeReplyObject(reply);
    return success;
}

redisReply* RedisClient::executeCommandWithReply(const char* format, ...) {
    if (!context_) return nullptr;

    va_list args;
    va_start(args, format);
    redisReply* reply = static_cast<redisReply*>(redisvCommand(context_, format, args));
    va_end(args);

    if (!reply) {
        last_error_ = context_->errstr;
        // Connection might be broken, mark as disconnected
        if (context_->err) {
            connected_ = false;
        }
    } else if (reply->type == REDIS_REPLY_ERROR) {
        last_error_ = reply->str;
    }

    return reply;
}

void RedisClient::freeReply(redisReply* reply) {
    if (reply) {
        freeReplyObject(reply);
    }
}

// ==========================================
// String Operations
// ==========================================

bool RedisClient::set(const std::string& key, const std::string& value, int expire_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply;
    if (expire_seconds > 0) {
        reply = executeCommandWithReply("SET %s %s EX %d", key.c_str(), value.c_str(), expire_seconds);
    } else {
        reply = executeCommandWithReply("SET %s %s", key.c_str(), value.c_str());
    }

    bool success = reply && reply->type == REDIS_REPLY_STATUS;
    freeReply(reply);
    return success;
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("GET %s", key.c_str());
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReply(reply);
    return result;
}

bool RedisClient::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("DEL %s", key.c_str());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    freeReply(reply);
    return success;
}

bool RedisClient::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("EXISTS %s", key.c_str());
    bool exists = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    freeReply(reply);
    return exists;
}

bool RedisClient::expire(const std::string& key, int seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("EXPIRE %s %d", key.c_str(), seconds);
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    freeReply(reply);
    return success;
}

// ==========================================
// List Operations
// ==========================================

bool RedisClient::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("LPUSH %s %b", key.c_str(), value.c_str(), value.size());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER;
    freeReply(reply);
    return success;
}

bool RedisClient::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("RPUSH %s %b", key.c_str(), value.c_str(), value.size());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER;
    freeReply(reply);
    return success;
}

std::optional<std::string> RedisClient::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("LPOP %s", key.c_str());
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReply(reply);
    return result;
}

std::optional<std::string> RedisClient::rpop(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("RPOP %s", key.c_str());
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReply(reply);
    return result;
}

std::vector<std::string> RedisClient::lrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    redisReply* reply = executeCommandWithReply("LRANGE %s %d %d", key.c_str(), start, stop);

    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        result.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }

    freeReply(reply);
    return result;
}

bool RedisClient::ltrim(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("LTRIM %s %d %d", key.c_str(), start, stop);
    bool success = reply && reply->type == REDIS_REPLY_STATUS;
    freeReply(reply);
    return success;
}

int64_t RedisClient::llen(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("LLEN %s", key.c_str());
    int64_t len = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        len = reply->integer;
    }
    freeReply(reply);
    return len;
}

// ==========================================
// Hash Operations
// ==========================================

bool RedisClient::hset(const std::string& key, const std::string& field, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("HSET %s %s %b",
        key.c_str(), field.c_str(), value.c_str(), value.size());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER;
    freeReply(reply);
    return success;
}

std::optional<std::string> RedisClient::hget(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("HGET %s %s", key.c_str(), field.c_str());
    if (!reply) return std::nullopt;

    std::optional<std::string> result;
    if (reply->type == REDIS_REPLY_STRING) {
        result = std::string(reply->str, reply->len);
    }

    freeReply(reply);
    return result;
}

bool RedisClient::hdel(const std::string& key, const std::string& field) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("HDEL %s %s", key.c_str(), field.c_str());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer > 0;
    freeReply(reply);
    return success;
}

bool RedisClient::hmset(const std::string& key, const std::unordered_map<std::string, std::string>& fields) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (fields.empty()) {
        return true;
    }

    // Build HMSET command: HMSET key field1 value1 field2 value2 ...
    std::string cmd = "HMSET " + key;
    for (const auto& [field, value] : fields) {
        cmd += " " + field + " " + value;
    }

    redisReply* reply = static_cast<redisReply*>(redisCommand(context_, cmd.c_str()));
    bool success = reply && reply->type == REDIS_REPLY_STATUS;
    freeReply(reply);
    return success;
}

std::unordered_map<std::string, std::string> RedisClient::hgetall(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<std::string, std::string> result;
    redisReply* reply = executeCommandWithReply("HGETALL %s", key.c_str());

    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements % 2 == 0) {
        result.reserve(reply->elements / 2);
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str, reply->element[i]->len);
            std::string value(reply->element[i + 1]->str, reply->element[i + 1]->len);
            result[std::move(field)] = std::move(value);
        }
    }

    freeReply(reply);
    return result;
}

// ==========================================
// Set Operations
// ==========================================

int64_t RedisClient::sadd(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("SADD %s %b", key.c_str(), member.c_str(), member.size());
    int64_t added = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        added = reply->integer;
    }
    freeReply(reply);
    return added;
}

int64_t RedisClient::srem(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("SREM %s %b", key.c_str(), member.c_str(), member.size());
    int64_t removed = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        removed = reply->integer;
    }
    freeReply(reply);
    return removed;
}

bool RedisClient::sismember(const std::string& key, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("SISMEMBER %s %b", key.c_str(), member.c_str(), member.size());
    bool is_member = reply && reply->type == REDIS_REPLY_INTEGER && reply->integer == 1;
    freeReply(reply);
    return is_member;
}

int64_t RedisClient::scard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("SCARD %s", key.c_str());
    int64_t count = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        count = reply->integer;
    }
    freeReply(reply);
    return count;
}

std::vector<std::string> RedisClient::smembers(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    redisReply* reply = executeCommandWithReply("SMEMBERS %s", key.c_str());

    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        result.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }

    freeReply(reply);
    return result;
}

std::pair<std::string, std::vector<std::string>> RedisClient::sscan(const std::string& key,
    const std::string& cursor, const std::string& pattern, int count) {

    std::lock_guard<std::mutex> lock(mutex_);

    std::string next_cursor = "0";
    std::vector<std::string> members;

    redisReply* reply;
    if (pattern.empty()) {
        reply = executeCommandWithReply("SSCAN %s %s COUNT %d", key.c_str(), cursor.c_str(), count);
    } else {
        reply = executeCommandWithReply("SSCAN %s %s MATCH %s COUNT %d",
            key.c_str(), cursor.c_str(), pattern.c_str(), count);
    }

    if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 2) {
        // First element is the next cursor
        if (reply->element[0]->type == REDIS_REPLY_STRING) {
            next_cursor = std::string(reply->element[0]->str, reply->element[0]->len);
        }
        // Second element is array of members
        if (reply->element[1]->type == REDIS_REPLY_ARRAY) {
            redisReply* arr = reply->element[1];
            members.reserve(arr->elements);
            for (size_t i = 0; i < arr->elements; ++i) {
                if (arr->element[i]->type == REDIS_REPLY_STRING) {
                    members.emplace_back(arr->element[i]->str, arr->element[i]->len);
                }
            }
        }
    }

    freeReply(reply);
    return {next_cursor, members};
}

// ==========================================
// Sorted Set Operations
// ==========================================

bool RedisClient::zadd(const std::string& key, double score, const std::string& member) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("ZADD %s %f %b",
        key.c_str(), score, member.c_str(), member.size());
    bool success = reply && reply->type == REDIS_REPLY_INTEGER;
    freeReply(reply);
    return success;
}

std::vector<std::string> RedisClient::zrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    redisReply* reply = executeCommandWithReply("ZRANGE %s %d %d", key.c_str(), start, stop);

    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        result.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }

    freeReply(reply);
    return result;
}

std::vector<std::string> RedisClient::zrevrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    redisReply* reply = executeCommandWithReply("ZREVRANGE %s %d %d", key.c_str(), start, stop);

    if (reply && reply->type == REDIS_REPLY_ARRAY) {
        result.reserve(reply->elements);
        for (size_t i = 0; i < reply->elements; ++i) {
            if (reply->element[i]->type == REDIS_REPLY_STRING) {
                result.emplace_back(reply->element[i]->str, reply->element[i]->len);
            }
        }
    }

    freeReply(reply);
    return result;
}

bool RedisClient::zremrangebyrank(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("ZREMRANGEBYRANK %s %d %d", key.c_str(), start, stop);
    bool success = reply && reply->type == REDIS_REPLY_INTEGER;
    freeReply(reply);
    return success;
}

int64_t RedisClient::zcard(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    redisReply* reply = executeCommandWithReply("ZCARD %s", key.c_str());
    int64_t count = 0;
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        count = reply->integer;
    }
    freeReply(reply);
    return count;
}

// ==========================================
// Pipeline
// ==========================================

RedisClient::Pipeline::Pipeline(RedisClient& client) : client_(client) {}

RedisClient::Pipeline::~Pipeline() = default;

RedisClient::Pipeline& RedisClient::Pipeline::lpush(const std::string& key, const std::string& value) {
    commands_.push_back([this, key, value]() {
        return client_.lpush(key, value);
    });
    return *this;
}

RedisClient::Pipeline& RedisClient::Pipeline::ltrim(const std::string& key, int start, int stop) {
    commands_.push_back([this, key, start, stop]() {
        return client_.ltrim(key, start, stop);
    });
    return *this;
}

RedisClient::Pipeline& RedisClient::Pipeline::expire(const std::string& key, int seconds) {
    commands_.push_back([this, key, seconds]() {
        return client_.expire(key, seconds);
    });
    return *this;
}

RedisClient::Pipeline& RedisClient::Pipeline::zadd(const std::string& key, double score, const std::string& member) {
    commands_.push_back([this, key, score, member]() {
        return client_.zadd(key, score, member);
    });
    return *this;
}

RedisClient::Pipeline& RedisClient::Pipeline::zremrangebyrank(const std::string& key, int start, int stop) {
    commands_.push_back([this, key, start, stop]() {
        return client_.zremrangebyrank(key, start, stop);
    });
    return *this;
}

bool RedisClient::Pipeline::execute() {
    for (const auto& cmd : commands_) {
        if (!cmd()) {
            return false;
        }
    }
    return true;
}

RedisClient::Pipeline RedisClient::pipeline() {
    return Pipeline(*this);
}

#else // !ENABLE_REDIS - Stub implementation

bool RedisClient::connect() {
    LOG_WARN("Redis support is disabled. Rebuild with -DENABLE_REDIS=ON");
    return false;
}

void RedisClient::disconnect() {}
bool RedisClient::isConnected() const { return false; }
bool RedisClient::reconnect() { return false; }
bool RedisClient::ping() { return false; }

bool RedisClient::set(const std::string&, const std::string&, int) { return false; }
std::optional<std::string> RedisClient::get(const std::string&) { return std::nullopt; }
bool RedisClient::del(const std::string&) { return false; }
bool RedisClient::exists(const std::string&) { return false; }
bool RedisClient::expire(const std::string&, int) { return false; }

bool RedisClient::lpush(const std::string&, const std::string&) { return false; }
bool RedisClient::rpush(const std::string&, const std::string&) { return false; }
std::optional<std::string> RedisClient::lpop(const std::string&) { return std::nullopt; }
std::optional<std::string> RedisClient::rpop(const std::string&) { return std::nullopt; }
std::vector<std::string> RedisClient::lrange(const std::string&, int, int) { return {}; }
bool RedisClient::ltrim(const std::string&, int, int) { return false; }
int64_t RedisClient::llen(const std::string&) { return 0; }

bool RedisClient::hset(const std::string&, const std::string&, const std::string&) { return false; }
bool RedisClient::hmset(const std::string&, const std::unordered_map<std::string, std::string>&) { return false; }
std::optional<std::string> RedisClient::hget(const std::string&, const std::string&) { return std::nullopt; }
bool RedisClient::hdel(const std::string&, const std::string&) { return false; }
std::unordered_map<std::string, std::string> RedisClient::hgetall(const std::string&) { return {}; }

int64_t RedisClient::sadd(const std::string&, const std::string&) { return 0; }
int64_t RedisClient::srem(const std::string&, const std::string&) { return 0; }
bool RedisClient::sismember(const std::string&, const std::string&) { return false; }
int64_t RedisClient::scard(const std::string&) { return 0; }
std::vector<std::string> RedisClient::smembers(const std::string&) { return {}; }
std::pair<std::string, std::vector<std::string>> RedisClient::sscan(const std::string&, const std::string&, const std::string&, int) { return {"0", {}}; }

bool RedisClient::zadd(const std::string&, double, const std::string&) { return false; }
std::vector<std::string> RedisClient::zrange(const std::string&, int, int) { return {}; }
std::vector<std::string> RedisClient::zrevrange(const std::string&, int, int) { return {}; }
bool RedisClient::zremrangebyrank(const std::string&, int, int) { return false; }
int64_t RedisClient::zcard(const std::string&) { return 0; }

RedisClient::Pipeline::Pipeline(RedisClient& client) : client_(client) {}
RedisClient::Pipeline::~Pipeline() = default;
RedisClient::Pipeline& RedisClient::Pipeline::lpush(const std::string&, const std::string&) { return *this; }
RedisClient::Pipeline& RedisClient::Pipeline::ltrim(const std::string&, int, int) { return *this; }
RedisClient::Pipeline& RedisClient::Pipeline::expire(const std::string&, int) { return *this; }
RedisClient::Pipeline& RedisClient::Pipeline::zadd(const std::string&, double, const std::string&) { return *this; }
RedisClient::Pipeline& RedisClient::Pipeline::zremrangebyrank(const std::string&, int, int) { return *this; }
bool RedisClient::Pipeline::execute() { return false; }
RedisClient::Pipeline RedisClient::pipeline() { return Pipeline(*this); }

#endif // ENABLE_REDIS

} // namespace chat
