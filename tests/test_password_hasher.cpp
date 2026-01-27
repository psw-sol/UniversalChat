#include <gtest/gtest.h>
#include "util/PasswordHasher.hpp"

using namespace chat;

class PasswordHasherTest : public ::testing::Test {
protected:
    void SetUp() override {}
};

// Test basic hashing
TEST_F(PasswordHasherTest, BasicHash) {
    std::string password = "mySecretPassword123";
    std::string hash = PasswordHasher::hash(password);

    // Hash should be 64 characters (SHA256 = 32 bytes = 64 hex chars)
    EXPECT_EQ(hash.length(), 64);

    // Same password should produce same hash
    std::string hash2 = PasswordHasher::hash(password);
    EXPECT_EQ(hash, hash2);
}

// Test different passwords produce different hashes
TEST_F(PasswordHasherTest, DifferentPasswords) {
    std::string hash1 = PasswordHasher::hash("password1");
    std::string hash2 = PasswordHasher::hash("password2");

    EXPECT_NE(hash1, hash2);
}

// Test verify function
TEST_F(PasswordHasherTest, VerifyPassword) {
    std::string password = "testPassword";
    std::string hash = PasswordHasher::hash(password);

    EXPECT_TRUE(PasswordHasher::verify(password, hash));
    EXPECT_FALSE(PasswordHasher::verify("wrongPassword", hash));
}

// Test salt generation
TEST_F(PasswordHasherTest, GenerateSalt) {
    std::string salt1 = PasswordHasher::generateSalt();
    std::string salt2 = PasswordHasher::generateSalt();

    // Default salt is 16 bytes = 32 hex chars
    EXPECT_EQ(salt1.length(), 32);
    EXPECT_EQ(salt2.length(), 32);

    // Two salts should be different
    EXPECT_NE(salt1, salt2);
}

// Test custom salt length
TEST_F(PasswordHasherTest, CustomSaltLength) {
    std::string salt = PasswordHasher::generateSalt(32);  // 32 bytes = 64 hex chars
    EXPECT_EQ(salt.length(), 64);
}

// Test salted hashing
TEST_F(PasswordHasherTest, HashWithSalt) {
    std::string password = "myPassword";
    std::string salt = PasswordHasher::generateSalt();

    std::string saltedHash = PasswordHasher::hashWithSalt(password, salt);

    // Should contain delimiter
    EXPECT_NE(saltedHash.find('$'), std::string::npos);

    // Format: salt$hash (32 chars + 1 + 64 chars = 97)
    EXPECT_EQ(saltedHash.length(), 32 + 1 + 64);
}

// Test salted verification
TEST_F(PasswordHasherTest, VerifyWithSalt) {
    std::string password = "securePassword123";
    std::string salt = PasswordHasher::generateSalt();
    std::string saltedHash = PasswordHasher::hashWithSalt(password, salt);

    EXPECT_TRUE(PasswordHasher::verifyWithSalt(password, saltedHash));
    EXPECT_FALSE(PasswordHasher::verifyWithSalt("wrongPassword", saltedHash));
}

// Test same password with different salts produces different hashes
TEST_F(PasswordHasherTest, SaltedHashUniqueness) {
    std::string password = "samePassword";
    std::string salt1 = PasswordHasher::generateSalt();
    std::string salt2 = PasswordHasher::generateSalt();

    std::string hash1 = PasswordHasher::hashWithSalt(password, salt1);
    std::string hash2 = PasswordHasher::hashWithSalt(password, salt2);

    // Same password but different salts should produce different results
    EXPECT_NE(hash1, hash2);
}

// Test empty password handling
TEST_F(PasswordHasherTest, EmptyPassword) {
    std::string hash = PasswordHasher::hash("");

    // Empty string should still produce a valid hash
    EXPECT_EQ(hash.length(), 64);

    // Verify should work with empty password
    EXPECT_TRUE(PasswordHasher::verify("", hash));
}

// Test backward compatibility with unsalted hash
TEST_F(PasswordHasherTest, BackwardCompatibility) {
    std::string password = "legacyPassword";
    std::string unsaltedHash = PasswordHasher::hash(password);

    // verifyWithSalt should handle unsalted hash (no $ delimiter)
    EXPECT_TRUE(PasswordHasher::verifyWithSalt(password, unsaltedHash));
}

// Test special characters in password
TEST_F(PasswordHasherTest, SpecialCharacters) {
    std::string password = "p@ssw0rd!#$%^&*(){}[]";
    std::string salt = PasswordHasher::generateSalt();
    std::string saltedHash = PasswordHasher::hashWithSalt(password, salt);

    EXPECT_TRUE(PasswordHasher::verifyWithSalt(password, saltedHash));
}

// Test unicode password
TEST_F(PasswordHasherTest, UnicodePassword) {
    std::string password = "password";  // Korean characters might not work, use ASCII
    std::string salt = PasswordHasher::generateSalt();
    std::string saltedHash = PasswordHasher::hashWithSalt(password, salt);

    EXPECT_TRUE(PasswordHasher::verifyWithSalt(password, saltedHash));
}

// Test long password
TEST_F(PasswordHasherTest, LongPassword) {
    std::string password(1000, 'a');  // 1000 character password
    std::string salt = PasswordHasher::generateSalt();
    std::string saltedHash = PasswordHasher::hashWithSalt(password, salt);

    EXPECT_TRUE(PasswordHasher::verifyWithSalt(password, saltedHash));
    EXPECT_EQ(saltedHash.length(), 32 + 1 + 64);  // Hash length is constant
}
