#!/usr/bin/env python3
"""SmvKdf (Source/Simumav/Private/Crypto) ↔ Python hashlib çapraz sınaması — simumav.com giriş özeti (auth_hash).

Formül (static/simumav/site/js/auth.js): master = PBKDF2-SHA256(şifre, b64çöz(salt), iter, 32); auth_hash = b64(PBKDF2(master, şifre, 1, 32)).
Kullanım: python3 tests/cpp/test_kdf.py
"""
import base64
import hashlib
import hmac
import os
import random
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.abspath(__file__))
KAYNAK = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Crypto', 'SmvKdf.cpp'))

SURUCU = r'''
#include "SmvKdf.h"
#include <cstdio>
#include <iostream>
#include <string>
using namespace SmvKdf;
static std::string Hex(const FBytes& B) { std::string S; char T[3]; for (auto X : B) { std::snprintf(T, 3, "%02x", X); S += T; } return S; }
int main() {
  std::string Kind;
  while (std::cin >> Kind) {
    if (Kind == "SHA") { std::string D; std::cin >> D; FBytes B; Base64Decode(D == "-" ? "" : D, B); std::printf("%s\n", Hex(Sha256(B.data(), B.size())).c_str()); }
    else if (Kind == "HMAC") { std::string K, D; std::cin >> K >> D; FBytes Kb, Db; Base64Decode(K, Kb); Base64Decode(D, Db); std::printf("%s\n", Hex(HmacSha256(Kb, Db.data(), Db.size())).c_str()); }
    else if (Kind == "B64") { std::string D; std::cin >> D; FBytes B; Base64Decode(D == "-" ? "" : D, B); { std::string E = Base64Encode(B); std::printf("%s\n", E.empty() ? "-" : E.c_str()); } }
    else if (Kind == "AUTH") { std::string P, S; unsigned It; std::cin >> P >> S >> It; FBytes Pb; Base64Decode(P, Pb); bool Ok; std::printf("%s\n", ComputeAuthHash(std::string(Pb.begin(), Pb.end()), S, It, Ok).c_str()); }
  }
  return 0;
}
'''


def auth_hash(sifre, salt_b64, it):
    master = hashlib.pbkdf2_hmac('sha256', sifre.encode(), base64.b64decode(salt_b64), it, 32)
    return base64.b64encode(hashlib.pbkdf2_hmac('sha256', master, sifre.encode(), 1, 32)).decode()


def main():
    rnd = random.Random(7)
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, 'surucu.cpp')
        open(src, 'w').write(SURUCU)
        exe = os.path.join(td, 'kdf')
        subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-O2', '-I', os.path.dirname(KAYNAK), '-o', exe, src, KAYNAK], check=True)
        girdi, beklenen = [], []
        for n in [0, 1, 3, 55, 56, 63, 64, 65, 119, 200, 1000]:
            d = bytes(rnd.getrandbits(8) for _ in range(n))
            girdi.append('SHA ' + (base64.b64encode(d).decode() or '-'))
            beklenen.append(hashlib.sha256(d).hexdigest())
            girdi.append('B64 ' + (base64.b64encode(d).decode() or '-'))
            beklenen.append(base64.b64encode(d).decode() or '-')
        for kn in [1, 32, 64, 65, 130]:
            k = bytes(rnd.getrandbits(8) for _ in range(kn))
            d = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 300)))
            girdi.append(f'HMAC {base64.b64encode(k).decode()} {base64.b64encode(d).decode()}')
            beklenen.append(hmac.new(k, d, hashlib.sha256).hexdigest())
        for sifre, it in [('Gizli-Sifre1', 1000), ('çğıöşü İ şifre 🙂', 2000), ('a', 1), ('uzun' * 40, 600000 // 100)]:
            salt = base64.b64encode(bytes(rnd.getrandbits(8) for _ in range(16))).decode()
            girdi.append(f'AUTH {base64.b64encode(sifre.encode()).decode()} {salt} {it}')
            beklenen.append(auth_hash(sifre, salt, it))
        out = subprocess.run([exe], input='\n'.join(girdi) + '\n', capture_output=True, text=True, check=True).stdout.split()
        hata = sum(1 for a, b in zip(out, beklenen) if a != b) + abs(len(out) - len(beklenen))
        for g, a, b in zip(girdi, out, beklenen):
            if a != b:
                print('FARK', g[:40], a, b)
        print(f'{len(beklenen)} vektör denetlendi; hata {hata}')
        return 1 if hata else 0


if __name__ == '__main__':
    sys.exit(main())
