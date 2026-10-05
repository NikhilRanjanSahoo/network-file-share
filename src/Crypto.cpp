#include "../include/Crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {

const char* const kScheme = "pbkdf2-sha256";
constexpr unsigned long kIterations = 600000;   // OWASP 2023 guidance for PBKDF2-HMAC-SHA256
constexpr size_t kSaltBytes = 16;
constexpr size_t kHashBytes = 32;

std::string toHex(const unsigned char* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0F]);
    }
    return out;
}

int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Returns empty vector on malformed input.
std::vector<unsigned char> fromHex(const std::string& s) {
    std::vector<unsigned char> out;
    if (s.empty() || s.size() % 2 != 0) return out;
    for (size_t i = 0; i < s.size(); i += 2) {
        int hi = hexVal(s[i]), lo = hexVal(s[i + 1]);
        if (hi < 0 || lo < 0) return std::vector<unsigned char>();
        out.push_back(static_cast<unsigned char>((hi << 4) | lo));
    }
    return out;
}

bool derive(const std::string& password, const unsigned char* salt, size_t saltLen,
            unsigned long iterations, unsigned char* out, size_t outLen) {
    return PKCS5_PBKDF2_HMAC(password.c_str(), static_cast<int>(password.size()),
                             salt, static_cast<int>(saltLen),
                             static_cast<int>(iterations), EVP_sha256(),
                             static_cast<int>(outLen), out) == 1;
}

}  // namespace

std::string randomHex(size_t nbytes) {
    std::vector<unsigned char> buf(nbytes);
    if (RAND_bytes(buf.data(), static_cast<int>(nbytes)) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }
    return toHex(buf.data(), buf.size());
}

bool isLegacyPlaintext(const std::string& stored) {
    return stored.rfind(std::string(kScheme) + "$", 0) != 0;
}

std::string hashPassword(const std::string& password) {
    unsigned char salt[kSaltBytes];
    if (RAND_bytes(salt, sizeof(salt)) != 1) throw std::runtime_error("RAND_bytes failed");

    unsigned char hash[kHashBytes];
    if (!derive(password, salt, sizeof(salt), kIterations, hash, sizeof(hash))) {
        throw std::runtime_error("PBKDF2 failed");
    }
    return std::string(kScheme) + "$" + std::to_string(kIterations) + "$" +
           toHex(salt, sizeof(salt)) + "$" + toHex(hash, sizeof(hash));
}

bool verifyPassword(const std::string& password, const std::string& stored) {
    if (isLegacyPlaintext(stored)) {
        if (password.size() != stored.size()) return false;
        return CRYPTO_memcmp(password.data(), stored.data(), stored.size()) == 0;
    }

    // scheme$iterations$salt$hash
    std::vector<std::string> parts;
    size_t start = 0;
    for (;;) {
        size_t pos = stored.find('$', start);
        if (pos == std::string::npos) { parts.push_back(stored.substr(start)); break; }
        parts.push_back(stored.substr(start, pos - start));
        start = pos + 1;
    }
    if (parts.size() != 4) return false;

    unsigned long iterations = std::strtoul(parts[1].c_str(), nullptr, 10);
    if (iterations == 0 || iterations > 10000000UL) return false;

    std::vector<unsigned char> salt = fromHex(parts[2]);
    std::vector<unsigned char> expected = fromHex(parts[3]);
    if (salt.empty() || expected.empty()) return false;

    std::vector<unsigned char> actual(expected.size());
    if (!derive(password, salt.data(), salt.size(), iterations, actual.data(), actual.size())) {
        return false;
    }
    return CRYPTO_memcmp(actual.data(), expected.data(), expected.size()) == 0;
}
