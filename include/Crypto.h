#ifndef CRYPTO_H
#define CRYPTO_H

#include <string>
#include <stddef.h>

// nbytes of cryptographically secure randomness, hex-encoded (2*nbytes chars).
// Throws std::runtime_error if the OS RNG fails.
std::string randomHex(size_t nbytes);

// Salted PBKDF2-HMAC-SHA256. Returns "pbkdf2-sha256$<iterations>$<salt hex>$<hash hex>".
std::string hashPassword(const std::string& password);

// Constant-time verification against a stored value. Also accepts legacy
// plaintext rows (see isLegacyPlaintext) so old databases keep working until
// the next successful login upgrades them.
bool verifyPassword(const std::string& password, const std::string& stored);

// True if `stored` is not in the pbkdf2 format above (old plaintext row).
bool isLegacyPlaintext(const std::string& stored);

#endif
