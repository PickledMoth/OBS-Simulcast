#include "secure-string.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <vector>
#endif

namespace SecureString {

#ifdef _WIN32

static std::string ToBase64(const BYTE *data, DWORD len)
{
	DWORD outLen = 0;
	CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &outLen);
	if (outLen == 0)
		return {};
	std::string out(outLen, '\0');
	if (!CryptBinaryToStringA(data, len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &outLen))
		return {};
	out.resize(outLen > 0 && out[outLen - 1] == '\0' ? outLen - 1 : outLen);
	return out;
}

static std::vector<BYTE> FromBase64(const std::string &b64)
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

std::string Encrypt(const std::string &plaintext)
{
	if (plaintext.empty())
		return {};

	DATA_BLOB in{};
	in.pbData = (BYTE *)plaintext.data();
	in.cbData = (DWORD)plaintext.size();

	DATA_BLOB out{};
	// Per-user, no extra entropy: the file is meaningless to any account
	// other than the one that saved it, which is the property we want --
	// no separate key file to lose or ship.
	if (!CryptProtectData(&in, L"OBS-Simulcast stream key", nullptr, nullptr, nullptr,
			       CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};

	std::string b64 = ToBase64(out.pbData, out.cbData);
	LocalFree(out.pbData);
	return b64;
}

std::string Decrypt(const std::string &ciphertext)
{
	if (ciphertext.empty())
		return {};

	std::vector<BYTE> raw = FromBase64(ciphertext);
	if (raw.empty())
		return {};

	DATA_BLOB in{};
	in.pbData = raw.data();
	in.cbData = (DWORD)raw.size();

	DATA_BLOB out{};
	if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
		return {};

	std::string plain((char *)out.pbData, out.cbData);
	LocalFree(out.pbData);
	return plain;
}

#else

// No at-rest encryption implemented for this platform yet; stream keys are
// stored in plaintext JSON same as before. macOS/Linux equivalents would be
// Keychain Services and libsecret respectively.
std::string Encrypt(const std::string &plaintext)
{
	return plaintext;
}

std::string Decrypt(const std::string &ciphertext)
{
	return ciphertext;
}

#endif

} // namespace SecureString
