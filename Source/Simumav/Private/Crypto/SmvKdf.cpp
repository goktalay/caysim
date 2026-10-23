// SIMUMAV — SHA-256 / HMAC / PBKDF2 / Base64 (bkz. SmvKdf.h). FIPS 180-4 ve RFC 2104 / RFC 8018 uygulaması.
#include "SmvKdf.h"
#include <cstring>

namespace SmvKdf
{
	namespace
	{
		const uint32_t K[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
			0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
			0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
			0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
			0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
			0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

		inline uint32_t Rotr(uint32_t X, uint32_t N) { return (X >> N) | (X << (32 - N)); }

		struct FSha256
		{
			uint32_t H[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
			uint8_t Block[64] = {};
			size_t BlockLen = 0;
			uint64_t TotalBits = 0;

			void Compress(const uint8_t* P)
			{
				uint32_t W[64];
				for (int i = 0; i < 16; ++i) W[i] = ((uint32_t)P[4 * i] << 24) | ((uint32_t)P[4 * i + 1] << 16) | ((uint32_t)P[4 * i + 2] << 8) | (uint32_t)P[4 * i + 3];
				for (int i = 16; i < 64; ++i)
				{
					const uint32_t S0 = Rotr(W[i - 15], 7) ^ Rotr(W[i - 15], 18) ^ (W[i - 15] >> 3);
					const uint32_t S1 = Rotr(W[i - 2], 17) ^ Rotr(W[i - 2], 19) ^ (W[i - 2] >> 10);
					W[i] = W[i - 16] + S0 + W[i - 7] + S1;
				}
				uint32_t A = H[0], B = H[1], C = H[2], D = H[3], E = H[4], F = H[5], G = H[6], Hh = H[7];
				for (int i = 0; i < 64; ++i)
				{
					const uint32_t S1 = Rotr(E, 6) ^ Rotr(E, 11) ^ Rotr(E, 25);
					const uint32_t Ch = (E & F) ^ (~E & G);
					const uint32_t T1 = Hh + S1 + Ch + K[i] + W[i];
					const uint32_t S0 = Rotr(A, 2) ^ Rotr(A, 13) ^ Rotr(A, 22);
					const uint32_t Maj = (A & B) ^ (A & C) ^ (B & C);
					const uint32_t T2 = S0 + Maj;
					Hh = G; G = F; F = E; E = D + T1; D = C; C = B; B = A; A = T1 + T2;
				}
				H[0] += A; H[1] += B; H[2] += C; H[3] += D; H[4] += E; H[5] += F; H[6] += G; H[7] += Hh;
			}

			void Update(const uint8_t* Data, size_t Len)
			{
				TotalBits += (uint64_t)Len * 8;
				while (Len > 0)
				{
					const size_t N = (64 - BlockLen) < Len ? (64 - BlockLen) : Len;
					std::memcpy(Block + BlockLen, Data, N);
					BlockLen += N; Data += N; Len -= N;
					if (BlockLen == 64) { Compress(Block); BlockLen = 0; }
				}
			}

			FBytes Final()
			{
				const uint64_t Bits = TotalBits;
				uint8_t Pad = 0x80;
				Update(&Pad, 1);
				const uint8_t Zero = 0;
				while (BlockLen != 56) Update(&Zero, 1);
				uint8_t L[8];
				for (int i = 0; i < 8; ++i) L[i] = (uint8_t)(Bits >> (56 - 8 * i));
				Update(L, 8);
				FBytes Out(32);
				for (int i = 0; i < 8; ++i)
				{
					Out[4 * i] = (uint8_t)(H[i] >> 24); Out[4 * i + 1] = (uint8_t)(H[i] >> 16);
					Out[4 * i + 2] = (uint8_t)(H[i] >> 8); Out[4 * i + 3] = (uint8_t)H[i];
				}
				return Out;
			}
		};

		const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	}

	FBytes Sha256(const uint8_t* Data, size_t Len)
	{
		FSha256 S;
		S.Update(Data, Len);
		return S.Final();
	}

	FBytes HmacSha256(const FBytes& KeyIn, const uint8_t* Data, size_t Len)
	{
		FBytes Key = KeyIn.size() > 64 ? Sha256(KeyIn.data(), KeyIn.size()) : KeyIn;
		Key.resize(64, 0);
		uint8_t Ipad[64], Opad[64];
		for (int i = 0; i < 64; ++i) { Ipad[i] = Key[i] ^ 0x36; Opad[i] = Key[i] ^ 0x5c; }
		FSha256 In;
		In.Update(Ipad, 64);
		In.Update(Data, Len);
		const FBytes Inner = In.Final();
		FSha256 Out;
		Out.Update(Opad, 64);
		Out.Update(Inner.data(), Inner.size());
		return Out.Final();
	}

	FBytes Pbkdf2Sha256(const FBytes& Password, const FBytes& Salt, uint32_t Iterations, size_t OutLen)
	{
		FBytes Out;
		Out.reserve(OutLen);
		if (Iterations == 0) Iterations = 1;
		for (uint32_t BlockIndex = 1; Out.size() < OutLen; ++BlockIndex)
		{
			FBytes S = Salt;
			S.push_back((uint8_t)(BlockIndex >> 24)); S.push_back((uint8_t)(BlockIndex >> 16));
			S.push_back((uint8_t)(BlockIndex >> 8)); S.push_back((uint8_t)BlockIndex);
			FBytes U = HmacSha256(Password, S.data(), S.size());
			FBytes T = U;
			for (uint32_t i = 1; i < Iterations; ++i)
			{
				U = HmacSha256(Password, U.data(), U.size());
				for (size_t j = 0; j < T.size(); ++j) T[j] ^= U[j];
			}
			for (size_t j = 0; j < T.size() && Out.size() < OutLen; ++j) Out.push_back(T[j]);
		}
		return Out;
	}

	std::string Base64Encode(const FBytes& In)
	{
		std::string Out;
		Out.reserve(((In.size() + 2) / 3) * 4);
		size_t i = 0;
		for (; i + 2 < In.size(); i += 3)
		{
			const uint32_t V = ((uint32_t)In[i] << 16) | ((uint32_t)In[i + 1] << 8) | In[i + 2];
			Out.push_back(B64[(V >> 18) & 63]); Out.push_back(B64[(V >> 12) & 63]);
			Out.push_back(B64[(V >> 6) & 63]); Out.push_back(B64[V & 63]);
		}
		if (i < In.size())
		{
			uint32_t V = (uint32_t)In[i] << 16;
			if (i + 1 < In.size()) V |= (uint32_t)In[i + 1] << 8;
			Out.push_back(B64[(V >> 18) & 63]); Out.push_back(B64[(V >> 12) & 63]);
			Out.push_back(i + 1 < In.size() ? B64[(V >> 6) & 63] : '=');
			Out.push_back('=');
		}
		return Out;
	}

	bool Base64Decode(const std::string& In, FBytes& Out)
	{
		Out.clear();
		uint32_t Acc = 0;
		int Bits = 0;
		for (char C : In)
		{
			int V;
			if (C >= 'A' && C <= 'Z') V = C - 'A';
			else if (C >= 'a' && C <= 'z') V = C - 'a' + 26;
			else if (C >= '0' && C <= '9') V = C - '0' + 52;
			else if (C == '+' || C == '-') V = 62;
			else if (C == '/' || C == '_') V = 63;
			else if (C == '=' || C == '\n' || C == '\r' || C == ' ') continue;
			else return false;
			Acc = (Acc << 6) | (uint32_t)V;
			Bits += 6;
			if (Bits >= 8) { Bits -= 8; Out.push_back((uint8_t)((Acc >> Bits) & 0xff)); }
		}
		return true;
	}

	std::string ComputeAuthHash(const std::string& Password, const std::string& SaltB64, uint32_t Iterations, bool& bOk)
	{
		FBytes Salt;
		bOk = Base64Decode(SaltB64, Salt) && !Salt.empty() && Iterations > 0;
		if (!bOk) return std::string();
		const FBytes Pw = Utf8(Password);
		const FBytes Master = Pbkdf2Sha256(Pw, Salt, Iterations, 32);
		return Base64Encode(Pbkdf2Sha256(Master, Pw, 1, 32));
	}
}
