#include "secure-string.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#include <vector>
#else
// Non-Windows: DPAPI has no equivalent "encrypt this blob for the current
// account" primitive, so this uses OpenSSL AES-256-GCM with a random key --
// generated once and stored in the OS's own secure storage (macOS Keychain,
// or GNOME Keyring/KWallet on Linux via libsecret) through QtKeychain,
// rather than deriving a key from a password. This keeps the exact same
// contract as the Windows path: Encrypt() returns a self-contained
// ciphertext blob for the JSON field, nothing else needs to change about
// how/where it's stored. Only the (single, small) master key lives in the
// keychain -- not one entry per stream key -- so no JSON schema change is
// needed either.
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <QByteArray>
#include <QEventLoop>
#include <QObject>
// Pulled in via FetchContent in CMakeLists.txt (not a system package) --
// keychain.h lives at the repo root of upstream qtkeychain (verified
// directly against the pinned tag's actual file listing), not under a
// qtkeychain/ or qt6keychain/ subdirectory as some docs/summaries suggest.
// CMakeLists.txt adds that source directory to this target's include path.
#include <keychain.h>
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

#else // !_WIN32

namespace {

constexpr int kIvLen = 12;
constexpr int kTagLen = 16;
constexpr int kKeyLen = 32; // AES-256

constexpr char kService[] = "OBS-Simulcast";
constexpr char kMasterKeyName[] = "stream-key-master-key";

// QtKeychain's Job classes are signal-based/async; every call site here
// (SettingsStore::SerializeConfig/DeserializeConfig) expects a plain
// synchronous string in, string out, same as the Windows DPAPI calls this
// replaces. Blocking with a small nested event loop keeps that contract
// intact instead of threading a callback through every caller -- safe here
// since, like the DPAPI calls it replaces, this only ever runs on the GUI
// thread.
template<typename JobT> void RunJobSync(JobT &job)
{
	QEventLoop loop;
	QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
	job.start();
	loop.exec();
}

// Reads the master key from the OS keychain, generating and storing a
// fresh random one on first use. Returns false if no secret-service
// provider is available at all (e.g. headless Linux with no keyring daemon
// running) -- callers fall back to plaintext in that case rather than
// losing the value outright.
bool GetOrCreateMasterKey(std::vector<unsigned char> &outKey)
{
	// Braced, not parenthesized -- QKeychain::ReadPasswordJob readJob(QLatin1String(kService))
	// is the classic C++ "most vexing parse": the compiler reads it as
	// declaring a function named readJob (returning ReadPasswordJob, taking
	// a QLatin1String parameter) rather than constructing an object, which
	// is exactly what a real build caught here.
	QKeychain::ReadPasswordJob readJob{QLatin1String(kService)};
	readJob.setKey(QLatin1String(kMasterKeyName));
	RunJobSync(readJob);

	if (readJob.error() == QKeychain::NoError) {
		QByteArray existing = readJob.binaryData();
		if (existing.size() == kKeyLen) {
			outKey.assign(existing.begin(), existing.end());
			return true;
		}
		// Malformed/wrong-length entry -- fall through and regenerate.
	}

	outKey.resize(kKeyLen);
	if (RAND_bytes(outKey.data(), kKeyLen) != 1)
		return false;

	QKeychain::WritePasswordJob writeJob{QLatin1String(kService)}; // see readJob's comment above
	writeJob.setKey(QLatin1String(kMasterKeyName));
	writeJob.setBinaryData(QByteArray(reinterpret_cast<const char *>(outKey.data()), kKeyLen));
	RunJobSync(writeJob);

	return writeJob.error() == QKeychain::NoError;
}

} // namespace

std::string Encrypt(const std::string &plaintext)
{
	if (plaintext.empty())
		return {};

	std::vector<unsigned char> key;
	if (!GetOrCreateMasterKey(key))
		return plaintext; // no keyring available -- see GetOrCreateMasterKey

	unsigned char iv[kIvLen];
	if (RAND_bytes(iv, kIvLen) != 1)
		return plaintext;

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return plaintext;

	bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
		  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) == 1 &&
		  EVP_EncryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) == 1;

	std::vector<unsigned char> ciphertext(plaintext.size());
	int len = 0, ciphertextLen = 0;
	if (ok)
		ok = EVP_EncryptUpdate(ctx, ciphertext.data(), &len, (const unsigned char *)plaintext.data(),
					(int)plaintext.size()) == 1;
	if (ok)
		ciphertextLen = len;

	int finalLen = 0;
	if (ok)
		ok = EVP_EncryptFinal_ex(ctx, ciphertext.data() + ciphertextLen, &finalLen) == 1;
	ciphertextLen += finalLen;

	unsigned char tag[kTagLen];
	if (ok)
		ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag) == 1;

	EVP_CIPHER_CTX_free(ctx);
	if (!ok)
		return plaintext;
	ciphertext.resize(ciphertextLen);

	QByteArray blob;
	blob.append(reinterpret_cast<const char *>(iv), kIvLen);
	blob.append(reinterpret_cast<const char *>(tag), kTagLen);
	blob.append(reinterpret_cast<const char *>(ciphertext.data()), (int)ciphertext.size());
	return blob.toBase64().toStdString();
}

std::string Decrypt(const std::string &ciphertextIn)
{
	if (ciphertextIn.empty())
		return {};

	std::vector<unsigned char> key;
	// Mirrors Encrypt()'s fallback exactly -- if no keyring was available
	// when this value was saved, it was stored as plain text, so treat it
	// the same way here rather than trying (and failing) to parse it as an
	// encrypted blob.
	if (!GetOrCreateMasterKey(key))
		return ciphertextIn;

	QByteArray raw = QByteArray::fromBase64(QByteArray::fromStdString(ciphertextIn));
	if (raw.size() < kIvLen + kTagLen)
		return {};

	const unsigned char *iv = reinterpret_cast<const unsigned char *>(raw.constData());
	const unsigned char *tag = reinterpret_cast<const unsigned char *>(raw.constData() + kIvLen);
	const unsigned char *ciphertext = reinterpret_cast<const unsigned char *>(raw.constData() + kIvLen + kTagLen);
	size_t ciphertextLen = (size_t)raw.size() - kIvLen - kTagLen;

	EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
	if (!ctx)
		return {};

	bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
		  EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kIvLen, nullptr) == 1 &&
		  EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv) == 1;

	std::vector<unsigned char> plaintext(ciphertextLen);
	int len = 0, plaintextLen = 0;
	if (ok && ciphertextLen > 0)
		ok = EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, (int)ciphertextLen) == 1;
	if (ok)
		plaintextLen = len;

	if (ok)
		ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen, (void *)tag) == 1;

	int finalLen = 0;
	// Wrong/rotated master key fails GCM's authentication tag check here
	// (returns <= 0) -- fails closed, same as the Windows DPAPI path.
	if (ok)
		ok = EVP_DecryptFinal_ex(ctx, plaintext.empty() ? nullptr : plaintext.data() + plaintextLen,
					  &finalLen) > 0;
	plaintextLen += finalLen;

	EVP_CIPHER_CTX_free(ctx);
	if (!ok)
		return {};

	return std::string((char *)plaintext.data(), plaintextLen);
}

#endif

} // namespace SecureString
