// SIMUMAV — Python `random.Random(int)` ile bit düzeyinde uyumlu MT19937 (yalnız C++17; Unreal'sız).
// Sunucudaki halka/it dalaşı üretimi (app/flightsim_match_core.py) Python Mersenne Twister kullanır; misafir kipte aynı tohumla
// AYNI parkurun çıkması için seed / random() / uniform() / shuffle() / getrandbits() Python 3 davranışıyla birebir yazıldı.
// tests/cpp/test_core.py Python'a karşı sınar.
#pragma once

#include <cstdint>
#include <vector>

namespace SmvCore
{
	class FPyRandom
	{
	public:
		explicit FPyRandom(uint64_t Seed = 1) { SeedInt(Seed); }
		/** random.seed(n) — n ≥ 0 tam sayı (32 bitlik parçalara bölünüp init_by_array). */
		void SeedInt(uint64_t N);
		uint32_t GenRandU32();
		/** random.random(): [0, 1) 53 bit. */
		double Random();
		double Uniform(double A, double B) { return A + (B - A) * Random(); }
		/** random.getrandbits(k), k ≤ 32. */
		uint32_t GetRandBits(int K);
		/** random._randbelow(n) (getrandbits yolu). */
		uint32_t RandBelow(uint32_t N);
		template <typename T> void Shuffle(std::vector<T>& X)
		{
			for (size_t i = X.size(); i-- > 1;)
			{
				const size_t j = RandBelow((uint32_t)(i + 1));
				T Tmp = X[i]; X[i] = X[j]; X[j] = Tmp;
			}
		}
	private:
		uint32_t Mt[624];
		int Mti = 625;
		void InitGenRand(uint32_t S);
		void InitByArray(const std::vector<uint32_t>& Key);
	};
}
