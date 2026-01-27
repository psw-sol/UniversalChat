#include "PasswordHasher.hpp"
#include "Logger.hpp"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else
#include <openssl/sha.h>
#include <openssl/rand.h>
#endif

#include <sstream>
#include <iomanip>
#include <random>
#include <stdexcept>

namespace chat {

std::vector<uint8_t> PasswordHasher::sha256(const std::string& data) {
    std::vector<uint8_t> hash(32);  // SHA256 produces 32 bytes

#ifdef _WIN32
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    NTSTATUS status;

    // Open algorithm provider
    status = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        throw std::runtime_error("Failed to open BCrypt algorithm provider");
    }

    // Create hash object
    status = BCryptCreateHash(hAlg, &hHash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("Failed to create BCrypt hash");
    }

    // Hash the data
    status = BCryptHashData(hHash,
        reinterpret_cast<PUCHAR>(const_cast<char*>(data.data())),
        static_cast<ULONG>(data.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("Failed to hash data");
    }

    // Finish hash
    status = BCryptFinishHash(hHash, hash.data(), static_cast<ULONG>(hash.size()), 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptDestroyHash(hHash);
        BCryptCloseAlgorithmProvider(hAlg, 0);
        throw std::runtime_error("Failed to finish hash");
    }

    // Cleanup
    BCryptDestroyHash(hHash);
    BCryptCloseAlgorithmProvider(hAlg, 0);
#else
    // OpenSSL implementation for non-Windows
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data.data(), data.size());
    SHA256_Final(hash.data(), &ctx);
#endif

    return hash;
}

std::string PasswordHasher::toHex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : bytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> PasswordHasher::fromHex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);

    for (size_t i = 0; i < hex.length(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(
            std::stoul(hex.substr(i, 2), nullptr, 16));
        bytes.push_back(byte);
    }

    return bytes;
}

std::string PasswordHasher::hash(const std::string& password) {
    try {
        auto hashBytes = sha256(password);
        return toHex(hashBytes);
    } catch (const std::exception& e) {
        LOG_ERROR("Password hashing failed: {}", e.what());
        return "";
    }
}

bool PasswordHasher::verify(const std::string& password, const std::string& hashStr) {
    if (hashStr.empty()) {
        return false;
    }

    std::string computedHash = hash(password);

    // Constant-time comparison to prevent timing attacks
    if (computedHash.length() != hashStr.length()) {
        return false;
    }

    volatile int result = 0;
    for (size_t i = 0; i < computedHash.length(); ++i) {
        result |= (computedHash[i] ^ hashStr[i]);
    }

    return result == 0;
}

std::string PasswordHasher::generateSalt(size_t length) {
    std::vector<uint8_t> salt(length);

#ifdef _WIN32
    BCRYPT_ALG_HANDLE hRng = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&hRng, BCRYPT_RNG_ALGORITHM, nullptr, 0);

    if (BCRYPT_SUCCESS(status)) {
        BCryptGenRandom(hRng, salt.data(), static_cast<ULONG>(length), 0);
        BCryptCloseAlgorithmProvider(hRng, 0);
    } else {
        // Fallback to std::random_device
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, 255);
        for (size_t i = 0; i < length; ++i) {
            salt[i] = static_cast<uint8_t>(dis(gen));
        }
    }
#else
    RAND_bytes(salt.data(), static_cast<int>(length));
#endif

    return toHex(salt);
}

std::string PasswordHasher::hashWithSalt(const std::string& password, const std::string& salt) {
    // Combine salt and password before hashing
    std::string combined = salt + password;
    std::string hashStr = hash(combined);

    // Return in format: salt$hash
    return salt + "$" + hashStr;
}

bool PasswordHasher::verifyWithSalt(const std::string& password, const std::string& saltedHash) {
    // Parse salt$hash format
    size_t delimPos = saltedHash.find('$');
    if (delimPos == std::string::npos) {
        // No salt delimiter, treat as plain hash for backward compatibility
        return verify(password, saltedHash);
    }

    std::string salt = saltedHash.substr(0, delimPos);
    std::string storedHash = saltedHash.substr(delimPos + 1);

    // Compute hash with extracted salt
    std::string combined = salt + password;
    std::string computedHash = hash(combined);

    // Constant-time comparison
    if (computedHash.length() != storedHash.length()) {
        return false;
    }

    volatile int result = 0;
    for (size_t i = 0; i < computedHash.length(); ++i) {
        result |= (computedHash[i] ^ storedHash[i]);
    }

    return result == 0;
}

} // namespace chat
