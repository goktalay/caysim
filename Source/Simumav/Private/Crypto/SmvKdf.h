// SIMUMAV — SHA-256, HMAC-SHA256, PBKDF2-HMAC-SHA256 ve Base64 (yalnız C++17 standart kitaplığı; Unreal'a bağımlı DEĞİL).
// mastertool hesap girişi (simumav.com sitesi static/simumav/site/js/auth.js ile birebir):
//   master    = PBKDF2-SHA256(şifre, b64çöz(kdf.salt), kdf.iter, 32)
//   auth_hash = b64(PBKDF2-SHA256(master, şifre, 1, 32))     — şifre sunucuya hiç gitmez.
// tests/cpp/test_kdf.py ile Python hashlib'e karşı sınanır.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace SmvKdf
{
	using FBytes = std::vector<uint8_t>;

	FBytes Sha256(const uint8_t* Data, size_t Len);
	FBytes HmacSha256(const FBytes& Key, const uint8_t* Data, size_t Len);
	FBytes Pbkdf2Sha256(const FBytes& Password, const FBytes& Salt, uint32_t Iterations, size_t OutLen);

	std::string Base64Encode(const FBytes& In);
	/** Standart ve URL-güvenli alfabeyi kabul eder; dolgu isteğe bağlı. Geçersiz karakterde false. */
	bool Base64Decode(const std::string& In, FBytes& Out);

	inline FBytes Utf8(const std::string& S) { return FBytes(S.begin(), S.end()); }

	/** simumav.com giriş özeti (yukarıdaki formül). Salt Base64; dönüş Base64. */
	std::string ComputeAuthHash(const std::string& PasswordUtf8, const std::string& SaltB64, uint32_t Iterations, bool& bOk);
}
