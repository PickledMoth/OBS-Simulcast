#pragma once

#include <string>

// Encrypts/decrypts secrets (stream keys) for at-rest storage in the
// plugin's JSON config file. On Windows this uses DPAPI (CryptProtectData),
// which ties the ciphertext to the current Windows user account -- the file
// alone is useless if copied to another machine or read by another user.
// Returns empty string on failure (caller should treat that as "not set"
// rather than crash or silently stream with a garbled key).
namespace SecureString {

std::string Encrypt(const std::string &plaintext);
std::string Decrypt(const std::string &ciphertext);

} // namespace SecureString
