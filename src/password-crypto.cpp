#include "password-crypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <vector>
#include <cstring>

namespace {

constexpr ULONG kSaltLen = 16;
constexpr ULONG kIvLen = 12; // GCM standard nonce size
constexpr ULONG kTagLen = 16;
constexpr ULONG kKeyLen = 32; // AES-256
constexpr ULONG kPbkdf2Iterations = 200000;

std::string ToBase64(const std::vector<BYTE> &data)
{
	DWORD outLen = 0;
	CryptBinaryToStringA(data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr,
			      &outLen);
	if (outLen == 0)
		return {};
	std::string out(outLen, '\0');
	if (!CryptBinaryToStringA(data.data(), (DWORD)data.size(), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
				   out.data(), &outLen))
		return {};
	out.resize(outLen > 0 && out[outLen - 1] == '\0' ? outLen - 1 : outLen);
	return out;
}

std::vector<BYTE> FromBase64(const std::string &b64)
{
	DWORD outLen = 0;
	if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &outLen, nullptr,
				   nullptr))
		return {};
	std::vector<BYTE> out(outLen);
	if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, out.data(), &outLen, nullptr,
				   nullptr))
		return {};
	out.resize(outLen);
	return out;
}

// Derives a 32-byte AES-256 key from (password, salt) via PBKDF2-HMAC-SHA256.
bool DeriveKey(const std::string &password, const BYTE *salt, ULONG saltLen, BYTE *outKey)
{
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != 0)
		return false;

	NTSTATUS status = BCryptDeriveKeyPBKDF2(hAlg, (PUCHAR)password.data(), (ULONG)password.size(), (PUCHAR)salt,
						 saltLen, kPbkdf2Iterations, outKey, kKeyLen, 0);
	BCryptCloseAlgorithmProvider(hAlg, 0);
	return status == 0;
}

// RAII-ish wrapper: opens AES-GCM, generates the symmetric key from
// already-derived key bytes; caller must call Close().
struct AesGcmKey {
	BCRYPT_ALG_HANDLE hAlg = nullptr;
	BCRYPT_KEY_HANDLE hKey = nullptr;
	std::vector<BYTE> keyObject;

	bool Open(const BYTE *keyBytes)
	{
		if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0) != 0)
			return false;
		if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
				       sizeof(BCRYPT_CHAIN_MODE_GCM), 0) != 0)
			return false;

		DWORD objLen = 0, copied = 0;
		if (BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &copied, 0) != 0)
			return false;
		keyObject.resize(objLen);

		return BCryptGenerateSymmetricKey(hAlg, &hKey, keyObject.data(), objLen, (PUCHAR)keyBytes, kKeyLen,
						   0) == 0;
	}

	void Close()
	{
		if (hKey)
			BCryptDestroyKey(hKey);
		if (hAlg)
			BCryptCloseAlgorithmProvider(hAlg, 0);
		hKey = nullptr;
		hAlg = nullptr;
	}
};

} // namespace

namespace PasswordCrypto {

std::string Encrypt(const std::string &plaintext, const std::string &password)
{
	if (password.empty())
		return {};

	BYTE salt[kSaltLen], iv[kIvLen];
	if (BCryptGenRandom(nullptr, salt, kSaltLen, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		return {};
	if (BCryptGenRandom(nullptr, iv, kIvLen, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
		return {};

	BYTE key[kKeyLen];
	if (!DeriveKey(password, salt, kSaltLen, key))
		return {};

	AesGcmKey aes;
	if (!aes.Open(key)) {
		aes.Close();
		return {};
	}

	std::vector<BYTE> ciphertext(plaintext.size());
	BYTE tag[kTagLen];

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = iv;
	authInfo.cbNonce = kIvLen;
	authInfo.pbTag = tag;
	authInfo.cbTag = kTagLen;

	ULONG resultLen = 0;
	NTSTATUS status = BCryptEncrypt(aes.hKey, (PUCHAR)plaintext.data(), (ULONG)plaintext.size(), &authInfo,
					 nullptr, 0, ciphertext.empty() ? nullptr : ciphertext.data(),
					 (ULONG)ciphertext.size(), &resultLen, 0);
	aes.Close();
	if (status != 0)
		return {};
	ciphertext.resize(resultLen);

	std::vector<BYTE> blob;
	blob.reserve(kSaltLen + kIvLen + kTagLen + ciphertext.size());
	blob.insert(blob.end(), salt, salt + kSaltLen);
	blob.insert(blob.end(), iv, iv + kIvLen);
	blob.insert(blob.end(), tag, tag + kTagLen);
	blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());

	return ToBase64(blob);
}

std::string Decrypt(const std::string &blob, const std::string &password)
{
	if (password.empty() || blob.empty())
		return {};

	std::vector<BYTE> raw = FromBase64(blob);
	if (raw.size() < kSaltLen + kIvLen + kTagLen)
		return {};

	const BYTE *salt = raw.data();
	const BYTE *iv = raw.data() + kSaltLen;
	const BYTE *tag = raw.data() + kSaltLen + kIvLen;
	const BYTE *ciphertext = raw.data() + kSaltLen + kIvLen + kTagLen;
	size_t ciphertextLen = raw.size() - kSaltLen - kIvLen - kTagLen;

	BYTE key[kKeyLen];
	if (!DeriveKey(password, salt, kSaltLen, key))
		return {};

	AesGcmKey aes;
	if (!aes.Open(key)) {
		aes.Close();
		return {};
	}

	std::vector<BYTE> plaintext(ciphertextLen);

	BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
	BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
	authInfo.pbNonce = (PUCHAR)iv;
	authInfo.cbNonce = kIvLen;
	authInfo.pbTag = (PUCHAR)tag;
	authInfo.cbTag = kTagLen;

	ULONG resultLen = 0;
	NTSTATUS status =
		BCryptDecrypt(aes.hKey, (PUCHAR)ciphertext, (ULONG)ciphertextLen, &authInfo, nullptr, 0,
			      plaintext.empty() ? nullptr : plaintext.data(), (ULONG)plaintext.size(), &resultLen, 0);
	aes.Close();
	// A wrong password produces a key that fails GCM's authentication tag
	// check here (status != 0) -- there's no partial/garbled decrypt to
	// worry about surfacing, BCryptDecrypt simply refuses.
	if (status != 0)
		return {};

	return std::string((char *)plaintext.data(), resultLen);
}

} // namespace PasswordCrypto
