#!/usr/bin/env python3
"""Bölge paketi çekirdeği (Source/Simumav/Private/Core/SmvRegionCore) — g++ ile derle + çalıştır. Kullanım: python3 tests/cpp/test_region.py"""
import os
import shutil
import subprocess
import sys
import tempfile

KOK = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.normpath(os.path.join(KOK, '..', '..', 'Source', 'Simumav', 'Private', 'Core'))
if not shutil.which('g++'):
    print('ATLANDI: g++ yok')
    sys.exit(0)
with tempfile.TemporaryDirectory() as d:
    exe = os.path.join(d, 'region')
    subprocess.check_call(['g++', '-std=c++17', '-O1', '-Wall', '-Wextra', '-o', exe, os.path.join(KOK, 'test_region_main.cpp'), os.path.join(CORE, 'SmvRegionCore.cpp')])
    sys.exit(subprocess.call([exe]))
