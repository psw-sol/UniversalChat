#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace chat {

/**
 * PasswordHasher
 *
 * Provides secure password hashing using SHA256.
 * Uses Windows BCrypt API for cryptographic operations.
 */
class PasswordHasher {
public:
    /**
     * Hash a password using SHA256
     * @param password Plain text password
     * @return Hex-encoded hash string
     */
    static std::string hash(const std::string& password);

    /**
     * Verify a password against a hash
     * @param password Plain text password to verify
     * @param hash Hex-encoded hash to compare against
     * @return true if password matches hash
     */
    static bool verify(const std::string& password, const std::string& hash);

    /**
     * Generate a random salt
     * @param length Salt length in bytes (default 16)
     * @return Hex-encoded salt string
     */
    static std::string generateSalt(size_t length = 16);

    /**
     * Hash a password with salt
     * @param password Plain text password
     * @param salt Salt to use
     * @return Hex-encoded salted hash (format: salt$hash)
     */
    static std::string hashWithSalt(const std::string& password, const std::string& salt);

    /**
     * Verify a password against a salted hash
     * @param password Plain text password
     * @param saltedHash Salted hash in format salt$hash
     * @return true if password matches
     */
    static bool verifyWithSalt(const std::string& password, const std::string& saltedHash);

private:
    static std::vector<uint8_t> sha256(const std::string& data);
    static std::string toHex(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> fromHex(const std::string& hex);
};

} // namespace chat
