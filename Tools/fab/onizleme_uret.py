#!/usr/bin/env python3
"""Uçuş haritası önizleme görselleri (Content/Simumav/MapPreviews/<anahtar>.png, 640x360).
Lisanslı Fab içeriğinden ekran görüntüsü DEĞİL: kodla çizilmiş sade çizimler (depoya girebilir). Kayıt tablosu:
Source/Simumav/Private/SmvMapRegistry.cpp. Kullanım: python3 Tools/fab/onizleme_uret.py"""
import os
import random

from PIL import Image, ImageDraw, ImageFilter

G, Y = 640, 360
KOK = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Content', 'Simumav', 'MapPreviews')


def gok(d, ust, alt, ufuk=Y):
    for y in range(ufuk):
        t = y / max(1, ufuk - 1)
        d.line([(0, y), (G, y)], fill=tuple(int(ust[i] + (alt[i] - ust[i]) * t) for i in range(3)))


def zemin(d, ufuk, ust, alt):
    for y in range(ufuk, Y):
        t = (y - ufuk) / max(1, Y - ufuk - 1)
        d.line([(0, y), (G, y)], fill=tuple(int(ust[i] + (alt[i] - ust[i]) * t) for i in range(3)))


def dron(d, x, y, s=1.0, renk=(235, 235, 240)):
    """Küçük quad silueti (ortak imza)."""
    k = 18 * s
    d.line([(x - k, y - k * 0.35), (x + k, y + k * 0.35)], fill=renk, width=max(2, int(3 * s)))
    d.line([(x - k, y + k * 0.35), (x + k, y - k * 0.35)], fill=renk, width=max(2, int(3 * s)))
    for dx, dy in ((-k, -k * 0.35), (k, k * 0.35), (-k, k * 0.35), (k, -k * 0.35)):
        d.ellipse([x + dx - 9 * s, y + dy - 2.5 * s, x + dx + 9 * s, y + dy + 2.5 * s], outline=renk, width=max(1, int(2 * s)))
    d.rectangle([x - 5 * s, y - 4 * s, x + 5 * s, y + 4 * s], fill=renk)


def kaydet(img, ad):
    os.makedirs(KOK, exist_ok=True)
    img.save(os.path.join(KOK, ad + '.png'), optimize=True)
    print('yazildi', ad)


def orman():
    r = random.Random(1)
    im = Image.new('RGB', (G, Y))
    d = ImageDraw.Draw(im)
    gok(d, (120, 170, 215), (215, 228, 225), 210)
    for kat, (ust, renk) in enumerate(((150, (70, 105, 95)), (185, (45, 85, 60)), (230, (28, 62, 40)))):
        for _ in range(70 + kat * 25):
            x = r.uniform(-20, G + 20)
            h = r.uniform(40, 90) * (1 + kat * 0.45)
            taban = ust + r.uniform(0, 40) + kat * 25
            w = h * 0.32
            d.polygon([(x, taban - h), (x - w, taban), (x + w, taban)], fill=renk)
        if kat == 0:
            zemin(d, 205, (60, 95, 70), (30, 60, 38))
    dron(d, 450, 95, 1.3)
    kaydet(im, 'orman')


def yikik():
    r = random.Random(2)
    im = Image.new('RGB', (G, Y))
    d = ImageDraw.Draw(im)
    gok(d, (150, 150, 150), (205, 195, 180), 230)
    zemin(d, 230, (120, 110, 98), (80, 72, 64))
    for i in range(7):
        x = 40 + i * 85 + r.uniform(-10, 10)
        w = r.uniform(50, 80)
        h = r.uniform(90, 190)
        top = 240 - h
        d.rectangle([x, top, x + w, 240], fill=(98, 94, 90))
        # kırık üst kenar
        pts = [(x, top)]
        for k in range(1, 6):
            pts.append((x + w * k / 6, top - r.uniform(-25, 10)))
        pts += [(x + w, top), (x + w, top + 30), (x, top + 30)]
        d.polygon(pts, fill=(98, 94, 90))
        for fy in range(int(top + 15), 230, 28):
            for fx in range(int(x + 8), int(x + w - 10), 18):
                if r.random() < 0.8:
                    d.rectangle([fx, fy, fx + 9, fy + 14], fill=(40, 38, 36))
        # demir çubuklar
        for _ in range(3):
            bx = x + r.uniform(5, w - 5)
            d.line([(bx, top - 5), (bx + r.uniform(-6, 6), top - 30)], fill=(70, 50, 40), width=2)
    # vinç
    d.line([(560, 240), (560, 60)], fill=(200, 150, 40), width=6)
    d.line([(470, 64), (630, 64)], fill=(200, 150, 40), width=5)
    dron(d, 300, 80, 1.3)
    kaydet(im, 'yikik')


def sehir():
    r = random.Random(3)
    im = Image.new('RGB', (G, Y))
    d = ImageDraw.Draw(im)
    gok(d, (40, 70, 120), (230, 160, 110), 260)
    for kat, renk in ((0, (70, 80, 105)), (1, (45, 50, 70))):
        x = -10
        while x < G:
            w = r.uniform(35, 75)
            h = r.uniform(80, 220) * (0.75 if kat == 0 else 1.0)
            top = 270 - h
            d.rectangle([x, top, x + w, 300], fill=renk)
            if kat == 1:
                for fy in range(int(top + 8), 290, 12):
                    for fx in range(int(x + 5), int(x + w - 5), 9):
                        if r.random() < 0.45:
                            d.rectangle([fx, fy, fx + 4, fy + 6], fill=(255, 215, 140))
            x += w + r.uniform(2, 12)
    zemin(d, 295, (35, 35, 40), (20, 20, 24))
    for k in range(0, G, 60):
        d.rectangle([k, 325, k + 30, 329], fill=(210, 200, 120))
    dron(d, 330, 110, 1.3)
    kaydet(im, 'sehir')


def rafineri():
    r = random.Random(4)
    im = Image.new('RGB', (G, Y))
    d = ImageDraw.Draw(im)
    gok(d, (90, 120, 160), (235, 200, 150), 250)
    zemin(d, 250, (95, 90, 80), (60, 56, 50))
    # tanklar
    for i, x in enumerate((40, 150, 260)):
        d.rectangle([x, 190, x + 90, 255], fill=(200, 200, 195))
        d.ellipse([x, 180, x + 90, 200], fill=(220, 220, 215))
        d.line([(x, 215), (x + 90, 215)], fill=(160, 160, 155), width=2)
    # damıtma kuleleri ve borular
    for x, h in ((390, 190), (440, 150), (480, 210)):
        d.rectangle([x, 255 - h, x + 22, 255], fill=(150, 150, 155))
        for yy in range(255 - h + 10, 250, 22):
            d.line([(x - 6, yy), (x + 28, yy)], fill=(110, 110, 115), width=2)
    d.rectangle([540, 70, 552, 255], fill=(170, 60, 50))
    # alev
    d.polygon([(546, 70), (538, 50), (546, 30), (554, 50)], fill=(255, 150, 40))
    for yy in (230, 238, 246):
        d.line([(130, yy), (560, yy)], fill=(120, 115, 105), width=3)
    dron(d, 280, 90, 1.3)
    im = im.filter(ImageFilter.SMOOTH)
    kaydet(im, 'rafineri')


def askeri():
    im = Image.new('RGB', (G, Y))
    d = ImageDraw.Draw(im)
    gok(d, (130, 165, 200), (220, 215, 195), 240)
    zemin(d, 240, (150, 140, 105), (100, 92, 68))
    # pist
    d.polygon([(250, 360), (390, 360), (340, 240), (300, 240)], fill=(70, 70, 72))
    for k in range(6):
        yy = 250 + k * 18
        d.rectangle([318, yy, 322, yy + 8], fill=(230, 230, 230))
    # hangarlar (yarım daire)
    for x in (30, 140, 440, 540):
        d.pieslice([x, 200, x + 90, 280], 180, 360, fill=(115, 120, 105))
        d.rectangle([x, 239, x + 90, 262], fill=(115, 120, 105))
        d.rectangle([x + 25, 240, x + 65, 262], fill=(60, 62, 55))
    # kule
    d.rectangle([600, 150, 616, 250], fill=(170, 170, 160))
    d.rectangle([590, 135, 626, 152], fill=(80, 110, 130))
    # çit
    for x in range(0, G, 14):
        d.line([(x, 285), (x, 300)], fill=(90, 90, 90), width=1)
    d.line([(0, 288), (G, 288)], fill=(110, 110, 110), width=1)
    dron(d, 320, 110, 1.3)
    kaydet(im, 'askeri')


if __name__ == '__main__':
    for f in (orman, yikik, sehir, rafineri, askeri):
        f()
