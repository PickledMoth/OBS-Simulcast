#pragma once

#include <string>

// Password-based encryption for the config export/import feature.
// Deliberately separate from secure-string.h's per-user-account encryption:
// that ties ciphertext to the local machine/account and can't decrypt on a
// different PC, which is the opposite of what a portable backup file needs.
// This derives a key from a user-supplied password (PBKDF2-HMAC-SHA256,
// 200k iterations) and encrypts with AES-256-GCM -- Windows' native BCrypt,
// OpenSSL elsewhere (see password-crypto.cpp) -- so the exported file is
// only as strong as the password chosen for it, and decrypts identically on
// any platform regardless of which one produced it.
namespace PasswordCrypto {

// Returns a self-contained blob (salt + IV + tag + ciphertext, base64'd)
// or empty on failure.
std::string Encrypt(const std::string &plaintext, const std::string &password);

// Returns empty on failure, including a wrong password (GCM's auth tag
// check fails closed rather than returning garbage).
std::string Decrypt(const std::string &blob, const std::string &password);

} // namespace PasswordCrypto
