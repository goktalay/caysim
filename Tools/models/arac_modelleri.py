#!/usr/bin/env python3
"""SIMUMAV yeni araç çeşitleri için prosedürel GLB modelleri → Content/Simumav/ModelsSrc/smv_<ad>.glb (Simumav üretimi; dış kaynak/lisans yok).

Kullanım (geliştirme makinesi; trimesh + numpy):
  python3 -I Tools/models/arac_modelleri.py [--out Content/Simumav/ModelsSrc] [--only octa,y6,...]

Konvansiyon (static/simumav/models/*.glb ile aynı; UE yükleyicisi SmvGlb buna göre çevirir): glTF metre, Y yukarı, burun +X, sağ +Z.
Hareketli düğüm adları (ASmvDroneActor bunları tanır; düğüm orijini = göbek/menteşe, ağ yerel çerçevede):
  rotor_<n>               dikey eksenli kaldırma rotoru (çok rotor; koaksiyelde üst/alt ayrı düğüm)
  rotor_main, rotor_main2 helikopter ana rotor(lar)ı (tandem: ön/arka)   rotor_upper / rotor_lower  koaksiyel rotorlar
  rotor_tail              kuyruk rotoru (yatay eksen)                       propeller / prop_<n>       çekici/itici pervane (gövde ekseni)
  aileron_l/r, elevator, rudder, elevon_l/r, flap_l/r                       kontrol yüzeyleri (menteşe = ön kenar)
  nav_l (kırmızı) / nav_r (yeşil) / nav_tail (beyaz) / beacon (kırmızı çakar) seyir ışıkları
Üçgen bütçesi ≤ 30 k / model; doku yok (düz PBR renkler).
"""
import argparse
import math
import sys
from pathlib import Path

import numpy as np
import trimesh

TRI_BUTCE = 30_000
KOK = Path(__file__).resolve().parents[2]


# ---------------- malzeme / ilkel yardımcılar ----------------
def boya(mesh, renk, metal=0.1, rough=0.6):
    m = trimesh.visual.material.PBRMaterial(baseColorFactor=[int(round(c * 255)) for c in renk], metallicFactor=metal, roughnessFactor=rough)
    mesh.visual = trimesh.visual.TextureVisuals(material=m)
    return mesh


def kutu(sx, sy, sz, x=0.0, y=0.0, z=0.0):
    b = trimesh.creation.box([sx, sy, sz])
    b.apply_translation([x, y, z])
    return b


def zarf(noktalar, x=0.0, y=0.0, z=0.0):
    m = trimesh.convex.convex_hull(np.array(noktalar, dtype=float))
    m.apply_translation([x, y, z])
    return m


def silindir(r, h, eksen='y', n=20, x=0.0, y=0.0, z=0.0):
    c = trimesh.creation.cylinder(radius=r, height=h, sections=n)   # ekseni Z
    if eksen == 'x':
        c.apply_transform(trimesh.transformations.rotation_matrix(math.pi / 2, [0, 1, 0]))
    elif eksen == 'y':
        c.apply_transform(trimesh.transformations.rotation_matrix(math.pi / 2, [1, 0, 0]))
    c.apply_translation([x, y, z])
    return c


def boru(a, b, r, n=10):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = b - a
    L = float(np.linalg.norm(d))
    c = trimesh.creation.cylinder(radius=r, height=L, sections=n)
    z = np.array([0, 0, 1.0])
    v = np.cross(z, d / L)
    s = np.linalg.norm(v)
    if s > 1e-9:
        c.apply_transform(trimesh.transformations.rotation_matrix(math.atan2(s, float(np.dot(z, d / L))), v / s))
    elif np.dot(z, d) < 0:
        c.apply_transform(trimesh.transformations.rotation_matrix(math.pi, [1, 0, 0]))
    c.apply_translation((a + b) / 2)
    return c


def kure(r, x=0.0, y=0.0, z=0.0, n=2):
    s = trimesh.creation.icosphere(subdivisions=n, radius=r)
    s.apply_translation([x, y, z])
    return s


def elipsoid(rx, ry, rz, x=0.0, y=0.0, z=0.0, n=3):
    s = trimesh.creation.icosphere(subdivisions=n, radius=1.0)
    s.apply_scale([rx, ry, rz])
    s.apply_translation([x, y, z])
    return s


def kanat(aciklik, kok_veter, uc_veter, kalinlik, ok_acisi=0.0, dihedral=0.0, x=0.0, y=0.0, z0=0.0, iki_yan=True):
    """Basit sivrilen kanat (dışbükey zarf parçaları). Açıklık Z boyunca; ön kenar +X. ok_acisi: ön kenar geriye (derece)."""
    parca = []
    for s in ((-1, 1) if iki_yan else (1,)):
        yari = aciklik / 2
        sw = math.tan(math.radians(ok_acisi)) * yari
        dh = math.tan(math.radians(dihedral)) * yari
        pts = []
        for zz, ch, xo, yo in ((0.0, kok_veter, 0.0, 0.0), (yari, uc_veter, -sw, dh)):
            for px, py in ((0.0, 0.0), (-ch * 0.3, kalinlik * 0.5), (-ch * 0.3, -kalinlik * 0.5), (-ch, 0.0)):
                pts.append([x + xo + px, y + yo + py * (ch / kok_veter), s * (z0 + zz)])
        parca.append(zarf(pts))
    return trimesh.util.concatenate(parca)


def pervane(R, kanat_sayisi=2, veter=None, kalinlik=None, eksen='y', egim=12.0, gobek=None):
    """Göbek merkezli pervane/rotor; dönüş ekseni yerel `eksen`. Kanatlar hafif hücum açılı."""
    veter = veter or R * 0.13
    kalinlik = kalinlik or max(0.003, R * 0.02)
    parca = []
    for k in range(kanat_sayisi):
        b = kutu(R * 0.92, kalinlik, veter)
        b.apply_translation([R * 0.46 + R * 0.04, 0, 0])
        b.apply_transform(trimesh.transformations.rotation_matrix(math.radians(egim), [1, 0, 0]))
        b.apply_transform(trimesh.transformations.rotation_matrix(2 * math.pi * k / kanat_sayisi, [0, 1, 0]))
        parca.append(b)
    parca.append(silindir(gobek or R * 0.08, kalinlik * 3.0, 'y', 14))
    m = trimesh.util.concatenate(parca)
    if eksen == 'x':
        m.apply_transform(trimesh.transformations.rotation_matrix(-math.pi / 2, [0, 0, 1]))
    elif eksen == 'z':
        m.apply_transform(trimesh.transformations.rotation_matrix(math.pi / 2, [1, 0, 0]))
    return m


# ---------------- sahne ----------------
class Sahne:
    def __init__(self, ad):
        self.sc = trimesh.Scene()
        self.kok = ad
        self.sc.graph.update(frame_to=ad, frame_from=self.sc.graph.base_frame, matrix=np.eye(4))
        self.sayac = {}

    def dugum(self, ad, konum=(0, 0, 0), ebeveyn=None):
        self.sc.graph.update(frame_to=ad, frame_from=ebeveyn or self.kok, matrix=trimesh.transformations.translation_matrix(konum))
        return ad

    def ekle(self, mesh, dugum=None, renk=(0.5, 0.5, 0.5, 1.0), metal=0.1, rough=0.6):
        d = dugum or self.kok
        n = self.sayac.get(d, 0)
        self.sayac[d] = n + 1
        boya(mesh, renk, metal, rough)
        self.sc.add_geometry(mesh, node_name=f'{d}_v{n}', geom_name=f'{d}_g{n}', parent_node_name=d)

    def parca(self, ad, konum, mesh, renk, metal=0.1, rough=0.6):
        """Hareketli parça: düğüm `ad` konumda, ağ yerel çerçevede (konuma göre)."""
        self.dugum(ad, konum)
        self.ekle(mesh, ad, renk, metal, rough)

    def isik(self, ad, konum, renk):
        self.parca(ad, konum, kure(0.018, n=1), renk, 0.0, 0.3)


KARBON = (0.09, 0.09, 0.10, 1.0)
KOYU = (0.16, 0.16, 0.17, 1.0)
GRI = (0.45, 0.46, 0.48, 1.0)
BEYAZ = (0.92, 0.93, 0.94, 1.0)
TURUNCU = (1.0, 0.45, 0.08, 1.0)
SARI = (0.98, 0.78, 0.10, 1.0)
KIRMIZI = (0.85, 0.08, 0.06, 1.0)
MAVI = (0.10, 0.35, 0.80, 1.0)
HAKI = (0.33, 0.37, 0.24, 1.0)
CAM = (0.06, 0.12, 0.20, 1.0)
PERVANE = (0.07, 0.07, 0.08, 1.0)
NAV_L, NAV_R, NAV_W = (1.0, 0.1, 0.1, 1.0), (0.1, 1.0, 0.2, 1.0), (1.0, 1.0, 1.0, 1.0)


def cok_rotor(s, aciler, kol, pervane_r, govde, renk_govde, koaks=False, motor_r=0.03, kol_r=0.012, kizak_h=0.18):
    """Ortak çok rotor: gövde + kollar + motor + rotor düğümleri (+ koaksiyelde alt rotorlar) + iniş takımı + ışıklar."""
    gx, gy, gz = govde
    s.ekle(zarf([[sx * gx / 2, sy * gy / 2, sz * gz / 2 * (0.75 if sy > 0 else 1.0)] for sx in (-1, 1) for sy in (-1, 1) for sz in (-1, 1)]
                + [[gx * 0.62, 0, 0], [-gx * 0.6, 0, 0]]), renk=renk_govde, rough=0.45)
    s.ekle(elipsoid(gx * 0.28, gy * 0.35, gz * 0.28, y=gy * 0.45), renk=KOYU, rough=0.35)   # üst kapak
    n = 0
    for a in aciler:
        r = math.radians(a)
        dx, dz = math.cos(r), math.sin(r)
        uc = np.array([dx * kol, 0.0, dz * kol])
        s.ekle(boru([dx * gx * 0.3, 0, dz * gz * 0.3], uc, kol_r), renk=KARBON, rough=0.4)
        s.ekle(silindir(motor_r, motor_r * 1.6, 'y', 16, *(uc + [0, motor_r * 0.8, 0])), renk=KOYU, metal=0.6, rough=0.3)
        on = dx > 0.2
        isik = NAV_R if (on and dz > 0) else NAV_L if (on and dz < 0) else NAV_W
        s.isik(f'nav_{"r" if (on and dz > 0) else "l" if on else "tail"}_{n}', uc + [0, -motor_r * 0.6, 0], isik)
        s.parca(f'rotor_{n}', uc + [0, motor_r * 1.9, 0], pervane(pervane_r, 2), PERVANE, 0.1, 0.5)
        n += 1
        if koaks:
            s.ekle(silindir(motor_r, motor_r * 1.6, 'y', 16, *(uc - [0, motor_r * 0.8, 0])), renk=KOYU, metal=0.6, rough=0.3)
    if koaks:
        for a in aciler:
            r = math.radians(a)
            uc = np.array([math.cos(r) * kol, 0.0, math.sin(r) * kol])
            s.parca(f'rotor_{n}', uc - [0, motor_r * 1.9, 0], pervane(pervane_r, 2), PERVANE, 0.1, 0.5)
            n += 1
    # iniş takımı (iki kızak)
    for z in (-gz * 0.55, gz * 0.55):
        s.ekle(boru([-gx * 0.6, -gy / 2 - kizak_h, z], [gx * 0.6, -gy / 2 - kizak_h, z], 0.01), renk=KOYU)
        for x in (-gx * 0.3, gx * 0.3):
            s.ekle(boru([x, -gy / 2, z * 0.6], [x, -gy / 2 - kizak_h, z], 0.008), renk=KOYU)
    s.isik('beacon', (0, gy * 0.75, 0), NAV_L)


# ---------------- modeller ----------------
def octa():
    s = Sahne('octa')
    cok_rotor(s, [0, 45, 90, 135, 180, 225, 270, 315], 0.5, 0.19, (0.32, 0.12, 0.32), (0.85, 0.86, 0.88, 1.0), motor_r=0.035, kol_r=0.016, kizak_h=0.26)
    s.ekle(kutu(0.08, 0.05, 0.08, 0.1, -0.12, 0), renk=KOYU)                          # gimbal kolu
    s.ekle(kure(0.055, 0.12, -0.18, 0), renk=CAM, metal=0.2, rough=0.15)               # kamera
    s.ekle(boru([-0.05, 0.06, 0], [-0.05, 0.2, 0], 0.006), renk=KOYU)                   # GPS direği
    s.ekle(silindir(0.045, 0.015, 'y', 18, -0.05, 0.21, 0), renk=GRI)
    for a in (0, 180):   # turuncu ön/arka kol işareti
        r = math.radians(a)
        s.ekle(boru([math.cos(r) * 0.2, 0.0, math.sin(r) * 0.2], [math.cos(r) * 0.45, 0.0, math.sin(r) * 0.45], 0.018), renk=TURUNCU if a == 0 else GRI)
    return s


def y6():
    s = Sahne('y6')
    cok_rotor(s, [60, -60, 180], 0.375, 0.165, (0.26, 0.11, 0.2), (0.12, 0.30, 0.55, 1.0), koaks=True, motor_r=0.03, kol_r=0.014, kizak_h=0.2)
    s.ekle(kure(0.045, 0.14, -0.08, 0), renk=CAM, metal=0.2, rough=0.15)
    return s


def cargo_x8():
    s = Sahne('cargo_x8')
    cok_rotor(s, [45, 135, 225, 315], 0.66, 0.38, (0.62, 0.26, 0.62), BEYAZ, koaks=True, motor_r=0.06, kol_r=0.035, kizak_h=0.55)
    s.ekle(kutu(0.62, 0.36, 0.42, 0, -0.36, 0), renk=SARI, rough=0.7)                 # kargo kutusu
    for x in (-0.31, 0.31):
        s.ekle(kutu(0.02, 0.38, 0.44, x, -0.36, 0), renk=KOYU)                          # kutu kayışları
    for x in (-0.2, 0.2):
        for z in (-0.15, 0.15):
            s.ekle(boru([x, -0.13, z], [x, -0.2, z], 0.012), renk=GRI)                 # askı
    s.ekle(kutu(0.3, 0.02, 0.3, 0, 0.17, 0), renk=SARI)                                # üst şerit
    return s


def tailsitter():
    """Kanatlı dört rotorlu tailsitter (artı düzen: sol/sağ kanat üstünde, üst/alt yüzgeçte); düz uçuş duruşunda modellenir."""
    s = Sahne('tailsitter')
    s.ekle(kanat(1.2, 0.34, 0.2, 0.05, ok_acisi=18, x=0.12), renk=(0.88, 0.89, 0.9, 1.0), rough=0.5)
    s.ekle(elipsoid(0.32, 0.07, 0.08, x=0.0), renk=KOYU, rough=0.4)                   # gövde puro
    s.ekle(elipsoid(0.08, 0.05, 0.05, x=0.28), renk=CAM, metal=0.2, rough=0.15)       # burun kamerası
    for yy in (1, -1):   # üst/alt yüzgeç (iniş bacağı)
        s.ekle(zarf([[0.0, 0, -0.01], [0.0, 0, 0.01], [-0.28, 0, -0.01], [-0.28, 0, 0.01], [-0.12, yy * 0.32, -0.008], [-0.3, yy * 0.34, -0.008],
                     [-0.12, yy * 0.32, 0.008], [-0.3, yy * 0.34, 0.008]], x=0.02), renk=GRI)
    mot = [((0.16, 0.0, -0.42), 'r0'), ((0.16, 0.0, 0.42), 'r1'), ((0.2, 0.3, 0.0), 'r2'), ((0.2, -0.3, 0.0), 'r3')]
    for i, (p, _) in enumerate(mot):
        s.ekle(silindir(0.022, 0.12, 'x', 14, p[0] - 0.05, p[1], p[2]), renk=KOYU, metal=0.5, rough=0.35)   # gondol
        if p[1] != 0:
            s.ekle(boru([0.05, 0, 0], [p[0] - 0.1, p[1], 0], 0.01), renk=KARBON)
        s.parca(f'prop_{i + 1}', (p[0] + 0.02, p[1], p[2]), pervane(0.13, 2, eksen='x'), PERVANE)
    for side, z in (('l', -1), ('r', 1)):
        s.parca(f'elevon_{side}', (-0.13, 0.0, z * 0.33), kutu(0.07, 0.012, 0.36, -0.035, 0, 0), (0.75, 0.76, 0.78, 1.0))
        s.isik(f'nav_{side}', (-0.08, 0.0, z * 0.6), NAV_L if side == 'l' else NAV_R)
    s.isik('nav_tail', (-0.3, 0.0, 0.0), NAV_W)
    return s


def trainer():
    """Yüksek kanatlı eğitim uçağı (1,6 m): burunda çekici pervane, klasik kuyruk, üç tekerlek."""
    s = Sahne('trainer')
    L = 1.15
    s.ekle(zarf([[0.42, 0.06, -0.06], [0.42, 0.06, 0.06], [0.42, -0.06, -0.06], [0.42, -0.06, 0.06], [0.1, 0.1, -0.08], [0.1, 0.1, 0.08],
                 [0.1, -0.08, -0.08], [0.1, -0.08, 0.08], [-0.7, 0.04, -0.02], [-0.7, 0.04, 0.02], [-0.7, 0.0, -0.02], [-0.7, 0.0, 0.02]]),
           renk=BEYAZ, rough=0.45)
    s.ekle(zarf([[0.3, 0.1, -0.06], [0.3, 0.1, 0.06], [0.05, 0.15, -0.07], [0.05, 0.15, 0.07], [-0.1, 0.11, -0.07], [-0.1, 0.11, 0.07]]),
           renk=CAM, metal=0.2, rough=0.1)                                                                  # kabin camı
    s.ekle(kanat(1.6, 0.39, 0.33, 0.045, dihedral=3, x=0.2, y=0.17), renk=BEYAZ, rough=0.45)                # yüksek kanat
    for z in (-0.55, 0.55):
        s.ekle(kutu(0.39, 0.004, 0.2, 0.0, 0.198, z), renk=KIRMIZI)                                          # kanat ucu şeritleri
    for z in (-1, 1):
        s.ekle(boru([0.05, -0.06, z * 0.07], [0.0, 0.16, z * 0.35], 0.008), renk=GRI)                        # kanat dikmeleri
    s.ekle(kanat(0.5, 0.15, 0.11, 0.015, x=-0.56, y=0.02), renk=BEYAZ)                                       # yatay kuyruk (sabit)
    s.ekle(zarf([[-0.55, 0.03, -0.006], [-0.55, 0.03, 0.006], [-0.7, 0.03, -0.006], [-0.7, 0.03, 0.006], [-0.68, 0.22, -0.005], [-0.74, 0.22, -0.005],
                 [-0.68, 0.22, 0.005], [-0.74, 0.22, 0.005]]), renk=KIRMIZI)                                 # dikey kuyruk
    s.parca('elevator', (-0.67, 0.02, 0.0), kutu(0.07, 0.01, 0.46, -0.035, 0, 0), BEYAZ)
    s.parca('rudder', (-0.72, 0.13, 0.0), kutu(0.06, 0.18, 0.008, -0.03, 0, 0), KIRMIZI)
    for side, z in (('l', -1), ('r', 1)):
        s.parca(f'aileron_{side}', (-0.13, 0.17 + 0.02, z * 0.55), kutu(0.08, 0.01, 0.46, -0.04, 0, 0), BEYAZ)
        s.isik(f'nav_{side}', (0.0, 0.19, z * 0.8), NAV_L if side == 'l' else NAV_R)
    s.isik('nav_tail', (-0.75, 0.2, 0.0), NAV_W)
    s.isik('beacon', (-0.1, 0.13, 0.0), NAV_L)
    s.ekle(silindir(0.05, 0.06, 'x', 18, 0.45, 0.0, 0.0), renk=GRI, metal=0.6, rough=0.3)                   # kaporta
    s.parca('propeller', (0.49, 0.0, 0.0), pervane(0.13, 2, eksen='x'), PERVANE)
    # üç tekerlek
    for p, r in (((0.32, -0.17, 0.0), 0.035), ((-0.05, -0.17, -0.14), 0.04), ((-0.05, -0.17, 0.14), 0.04)):
        s.ekle(silindir(r, 0.025, 'z', 18, *p), renk=KOYU, rough=0.9)
        s.ekle(boru([p[0], -0.06, p[2] * 0.4], [p[0], p[1], p[2]], 0.006), renk=GRI)
    return s


def heli_tandem():
    """Tandem iki ana rotorlu kargo helikopteri (2,6 m): öndeki rotor alçak, arkadaki yüksek pilonda; üçer pala."""
    s = Sahne('heli_tandem')
    s.ekle(zarf([[x, y, z] for x in (-1.05, 1.0) for y in (-0.18, 0.2) for z in (-0.2, 0.2)] + [[1.25, 0.0, -0.12], [1.25, 0.0, 0.12], [1.18, 0.15, 0], [1.2, -0.12, 0]]),
           renk=HAKI, rough=0.7)
    s.ekle(zarf([[1.05, 0.04, -0.17], [1.05, 0.04, 0.17], [1.2, 0.0, -0.1], [1.2, 0.0, 0.1], [0.95, 0.19, -0.16], [0.95, 0.19, 0.16]]), renk=CAM, metal=0.2, rough=0.1)
    s.ekle(zarf([[x, y, z] for x in (0.55, 1.0) for y in (0.2, 0.36) for z in (-0.1, 0.1)]), renk=HAKI, rough=0.7)       # ön pilon
    s.ekle(zarf([[x, y, z] for x in (-1.05, -0.55) for y in (0.2, 0.55) for z in (-0.12, 0.12)] + [[-1.2, 0.0, 0.0], [-1.15, 0.5, 0]]), renk=HAKI, rough=0.7)   # arka pilon
    for x in (-0.6, 0.0, 0.5):
        s.ekle(kutu(0.18, 0.002, 0.003, x, 0.0, 0.201), renk=KOYU)                     # pencere sırası
    for p in ((0.75, -0.3, -0.24), (0.75, -0.3, 0.24), (-0.7, -0.3, -0.24), (-0.7, -0.3, 0.24)):
        s.ekle(silindir(0.08, 0.06, 'z', 18, *p), renk=KOYU, rough=0.9)
        s.ekle(boru([p[0], -0.16, p[2] * 0.7], list(p), 0.012), renk=GRI)
    s.ekle(silindir(0.03, 0.12, 'y', 12, 0.78, 0.42, 0), renk=GRI, metal=0.6)
    s.ekle(silindir(0.03, 0.12, 'y', 12, -0.82, 0.62, 0), renk=GRI, metal=0.6)
    s.parca('rotor_main', (0.78, 0.49, 0.0), pervane(0.82, 3, veter=0.07, kalinlik=0.012, egim=6, gobek=0.06), PERVANE)
    s.parca('rotor_main2', (-0.82, 0.69, 0.0), pervane(0.82, 3, veter=0.07, kalinlik=0.012, egim=6, gobek=0.06), PERVANE)
    s.isik('nav_l', (0.2, 0.0, -0.205), NAV_L)
    s.isik('nav_r', (0.2, 0.0, 0.205), NAV_R)
    s.isik('nav_tail', (-1.2, 0.05, 0.0), NAV_W)
    s.isik('beacon', (0.0, 0.21, 0.0), NAV_L)
    return s


def coax():
    """Küçük koaksiyel helikopter (0,9 m rotor): kapsül gövde, iki karşıt dönen rotor, kızak, kuyruk yüzgeci (kuyruk rotoru yok)."""
    s = Sahne('coax')
    s.ekle(elipsoid(0.22, 0.13, 0.12, x=0.02), renk=MAVI, rough=0.4)
    s.ekle(elipsoid(0.1, 0.08, 0.09, x=0.15, y=0.03), renk=CAM, metal=0.2, rough=0.1)
    s.ekle(boru([-0.15, 0.02, 0], [-0.48, 0.04, 0], 0.018), renk=MAVI)
    s.ekle(zarf([[-0.42, 0.04, -0.004], [-0.42, 0.04, 0.004], [-0.5, 0.04, -0.004], [-0.5, 0.04, 0.004], [-0.48, 0.16, -0.003], [-0.53, 0.16, -0.003],
                 [-0.48, 0.16, 0.003], [-0.53, 0.16, 0.003], [-0.47, -0.05, 0], [-0.51, -0.05, 0]]), renk=BEYAZ)
    s.ekle(kanat(0.2, 0.06, 0.05, 0.01, x=-0.42, y=0.04), renk=BEYAZ)
    s.ekle(silindir(0.012, 0.28, 'y', 12, 0.0, 0.24, 0.0), renk=GRI, metal=0.7, rough=0.25)   # mil
    s.ekle(silindir(0.03, 0.04, 'y', 14, 0.0, 0.14, 0.0), renk=KOYU, metal=0.5)
    s.parca('rotor_lower', (0.0, 0.25, 0.0), pervane(0.45, 2, veter=0.04, kalinlik=0.006, egim=6, gobek=0.025), PERVANE)
    s.parca('rotor_upper', (0.0, 0.36, 0.0), pervane(0.45, 2, veter=0.04, kalinlik=0.006, egim=6, gobek=0.025), PERVANE)
    for z in (-0.1, 0.1):
        s.ekle(boru([-0.15, -0.2, z], [0.18, -0.2, z], 0.008), renk=KOYU)
        for x in (-0.08, 0.1):
            s.ekle(boru([x, -0.08, z * 0.5], [x, -0.2, z], 0.006), renk=KOYU)
    s.isik('nav_l', (0.0, 0.0, -0.125), NAV_L)
    s.isik('nav_r', (0.0, 0.0, 0.125), NAV_R)
    s.isik('nav_tail', (-0.53, 0.1, 0.0), NAV_W)
    return s


MODELLER = {'octa': octa, 'y6': y6, 'cargo_x8': cargo_x8, 'tailsitter': tailsitter, 'trainer': trainer, 'heli_tandem': heli_tandem, 'coax': coax}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=str(KOK / 'Content' / 'Simumav' / 'ModelsSrc'))
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    sec = [x for x in a.only.split(',') if x] or list(MODELLER)
    for ad in sec:
        s = MODELLER[ad]()
        tri = sum(len(g.faces) for g in s.sc.geometry.values())
        if tri > TRI_BUTCE:
            print(f'UYARI {ad}: {tri} üçgen > {TRI_BUTCE}', file=sys.stderr)
        data = s.sc.export(file_type='glb')
        yol = out / f'smv_{ad}.glb'
        yol.write_bytes(data)
        lo, hi = s.sc.bounds
        print(f'{yol.name}: {len(data) / 1024:.0f} KB, {tri} üçgen, boyut {np.round(hi - lo, 2).tolist()} m')


if __name__ == '__main__':
    main()
