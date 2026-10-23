#!/usr/bin/env python3
"""SmvSolar (NOAA güneş hesabı) sınaması — bilinen gün doğumu/batımı ve güneş konumlarıyla (±3 dk / ±0,5°). Kullanım: python3 tests/cpp/test_solar.py"""
import os
import subprocess
import sys
import tempfile

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
CORE = os.path.join(KOK, 'Source', 'Simumav', 'Private', 'Core')
MAIN = r'''
#include "SmvSolar.h"
#include <cstdio>
int main() {
  using namespace SmvCore;
  auto a = SolarDayEvents(39.9334, 32.8597, 2026, 6, 21, 3);   // Ankara yaz gündönümü
  auto l = SolarDayEvents(51.5074, -0.1278, 2026, 12, 21, 0);  // Londra kış gündönümü
  auto p = SolarPosition(39.9334, 32.8597, 2026, 6, 21, 9.839); // Ankara güneş öğlesi civarı (UTC)
  auto s = SolarDayEvents(78.22, 15.65, 2026, 6, 21, 2);       // Svalbard: kutup gündüzü
  std::printf("%f %f %f %f %f %f %f %f %f\n", a.Sunrise, a.Sunset, l.Sunrise, l.Sunset, p.ElevationDeg, p.AzimuthDeg, s.Sunrise,
              ApproxTimeZone(39.9, 32.8), TimeOfDayHours("dusk", 51.5074, -0.1278, 2026, 12, 21, 0));
}
'''
BEKLENEN = [(5.30, 0.05), (20.35, 0.05), (8.067, 0.05), (15.883, 0.05), (73.55, 0.5), (180.0, 2.0), (-1.0, 0.0), (3.0, 0.0), (15.55, 0.06)]
ADLAR = ['Ankara doğuş', 'Ankara batış', 'Londra doğuş', 'Londra batış', 'öğle yüksekliği', 'öğle azimutu', 'kutup gündüzü', 'TR dilimi', 'Londra alacakaranlık']


def main():
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, 'm.cpp'); exe = os.path.join(td, 'm')
        open(src, 'w').write(MAIN)
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', CORE, '-o', exe, src, os.path.join(CORE, 'SmvSolar.cpp')], check=True)
        v = [float(x) for x in subprocess.run([exe], capture_output=True, text=True, check=True).stdout.split()]
    hata = [f'{ad}: {x:.3f} (beklenen {b} ± {t})' for ad, x, (b, t) in zip(ADLAR, v, BEKLENEN) if abs(x - b) > t]
    print(f'güneş sınaması: {len(hata)} hata')
    for h in hata:
        print('  ' + h)
    return 1 if hata else 0


if __name__ == '__main__':
    sys.exit(main())
