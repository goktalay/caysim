// SIMUMAV — Python uyumlu MT19937 (CPython Modules/_randommodule.c ile aynı algoritma).
#include "SmvPyRandom.h"

namespace SmvCore
{
	void FPyRandom::InitGenRand(uint32_t S)
	{
		Mt[0] = S;
		for (Mti = 1; Mti < 624; ++Mti)
		{
			Mt[Mti] = (1812433253U * (Mt[Mti - 1] ^ (Mt[Mti - 1] >> 30)) + (uint32_t)Mti);
		}
	}

	void FPyRandom::InitByArray(const std::vector<uint32_t>& Key)
	{
		InitGenRand(19650218U);
		uint32_t i = 1, j = 0;
		const uint32_t KeyLen = (uint32_t)Key.size();
		uint32_t k = 624 > KeyLen ? 624 : KeyLen;
		for (; k; --k)
		{
			Mt[i] = (Mt[i] ^ ((Mt[i - 1] ^ (Mt[i - 1] >> 30)) * 1664525U)) + Key[j] + j;
			++i; ++j;
			if (i >= 624) { Mt[0] = Mt[623]; i = 1; }
			if (j >= KeyLen) j = 0;
		}
		for (k = 623; k; --k)
		{
			Mt[i] = (Mt[i] ^ ((Mt[i - 1] ^ (Mt[i - 1] >> 30)) * 1566083941U)) - i;
			++i;
			if (i >= 624) { Mt[0] = Mt[623]; i = 1; }
		}
		Mt[0] = 0x80000000U;
		Mti = 624;
	}

	void FPyRandom::SeedInt(uint64_t N)
	{
		std::vector<uint32_t> Key;
		if (N == 0) Key.push_back(0);
		while (N) { Key.push_back((uint32_t)(N & 0xffffffffU)); N >>= 32; }
		InitByArray(Key);
	}

	uint32_t FPyRandom::GenRandU32()
	{
		static const uint32_t Mag01[2] = { 0x0U, 0x9908b0dfU };
		uint32_t Y;
		if (Mti >= 624)
		{
			int Kk;
			for (Kk = 0; Kk < 624 - 397; ++Kk)
			{
				Y = (Mt[Kk] & 0x80000000U) | (Mt[Kk + 1] & 0x7fffffffU);
				Mt[Kk] = Mt[Kk + 397] ^ (Y >> 1) ^ Mag01[Y & 0x1U];
			}
			for (; Kk < 623; ++Kk)
			{
				Y = (Mt[Kk] & 0x80000000U) | (Mt[Kk + 1] & 0x7fffffffU);
				Mt[Kk] = Mt[Kk + (397 - 624)] ^ (Y >> 1) ^ Mag01[Y & 0x1U];
			}
			Y = (Mt[623] & 0x80000000U) | (Mt[0] & 0x7fffffffU);
			Mt[623] = Mt[396] ^ (Y >> 1) ^ Mag01[Y & 0x1U];
			Mti = 0;
		}
		Y = Mt[Mti++];
		Y ^= (Y >> 11);
		Y ^= (Y << 7) & 0x9d2c5680U;
		Y ^= (Y << 15) & 0xefc60000U;
		Y ^= (Y >> 18);
		return Y;
	}

	double FPyRandom::Random()
	{
		const uint32_t A = GenRandU32() >> 5, B = GenRandU32() >> 6;
		return (A * 67108864.0 + B) * (1.0 / 9007199254740992.0);
	}

	uint32_t FPyRandom::GetRandBits(int K)
	{
		if (K <= 0) return 0;
		if (K > 32) K = 32;
		return GenRandU32() >> (32 - K);
	}

	uint32_t FPyRandom::RandBelow(uint32_t N)
	{
		if (N == 0) return 0;
		int K = 0;
		for (uint32_t V = N; V; V >>= 1) ++K; // n.bit_length()
		uint32_t R = GetRandBits(K);
		while (R >= N) R = GetRandBits(K);
		return R;
	}
}
