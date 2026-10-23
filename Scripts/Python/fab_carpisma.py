# Simumav — Fab ağlarının ÇARPIŞMA hazırlığı (Editor Python, headless). Tools/fab/fab-hazirla.ps1 (e) bölümü çağırır.
# Amaç: uçuş haritalarında kullanılan statik ağların gerçek fizik sorgularına (gövde süpürmesi, kapsül çakışması, ışın) cevap vermesi.
#   • Basit çarpışması (kutu/küre/kapsül/dışbükey) olmayan ağ → CollisionTraceFlag = UseComplexAsSimple (üçgen ağı basit yerine geçer;
#     statik dünya için doğru ve hızlı; çakışma/MTD sorguları da çalışır). Nanite ağlarında karmaşık çarpışma geri dönüş (fallback) ağından gelir.
#   • Çim / çiçek / çok küçük süs ağları atlanır (dron çimen yaprağına çarpmaz).
#   • Bileşen düzeyi (ISM / HISM / Foliage / LevelInstance içindeki bileşenlerin çarpışma kanalları) OYUN ANINDA açılır:
#     Source/Simumav/Private/SmvPhysics.cpp PrepareLevelCollision (her seviye eklenince; günlükte "Fizik seviye hazırlığı").
# Girdi : FAB_CARPISMA_GIRDI = JSON {"haritalar": ["/Game/.../Harita", ...], "klasorler": ["/Game/...", ...]}
# Çıktı : FAB_CARPISMA_SONUC = JSON {"paketler": {<kök>: {...sayılar}}, "toplam": {...}}; günlükte paket başına satır:
#         "[fab_carpisma] <paket>: N ag - basit X, yalniz karmasik Y, carpismasiz ag sayisi Z (duzeltilen W), onceden duzeltilmis V, kucuk/cim K"
# Kaydedilen ağlar robocopy /XO sayesinde sonraki derlemede kaynaktan ezilmez (kaynak güncellenirse yeniden kopyalanır ve yeniden düzeltilir).
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
CTF = unreal.CollisionTraceFlag
KUCUK_CM = 30.0
CIM = ("grass", "flower", "weed", "clover", "moss", "decal", "pebble", "ivy", "dandelion")


def log(s):
    unreal.log("[fab_carpisma] " + s)


def uyar(s):
    unreal.log_warning("[fab_carpisma] " + s)


def sinif(ad):
    try:
        return str(ad.asset_class_path.asset_name)
    except Exception:  # noqa: BLE001
        return str(getattr(ad, "asset_class", ""))


def paket_varliklari(pkg):
    try:
        return list(ar.get_assets_by_package_name(pkg))
    except Exception:  # noqa: BLE001
        return []


def bagimliliklar(pkg):
    try:
        o = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
                                                  include_searchable_names=False, include_soft_management_references=False,
                                                  include_hard_management_references=False)
        return [str(x) for x in (ar.get_dependencies(unreal.Name(pkg), o) or [])]
    except Exception as e:  # noqa: BLE001
        uyar(f"bagimlilik okunamadi {pkg}: {e}")
        return []


def harici_aktorler(harita):
    """World Partition / OFPA haritası: aktörler ayrı paketlerde (__ExternalActors__/<harita yolu>)."""
    out = []
    goreli = harita[len("/Game"):] if harita.startswith("/Game/") else None
    if not goreli:
        return out
    for kok in ("/Game/__ExternalActors__", "/Game/__ExternalObjects__"):
        try:
            for ad in ar.get_assets_by_path(kok + goreli, recursive=True):
                out.append(str(ad.package_name))
        except Exception:  # noqa: BLE001
            pass
    return out


def haritalardan_aglar(haritalar):
    """Haritalardan (alt seviyeler, LevelInstance haritaları, harici aktörler dahil) ulaşılan /Game statik ağ paketleri."""
    gor, kuyruk, aglar = set(), list(haritalar), set()
    while kuyruk:
        p = kuyruk.pop()
        if p in gor or not p.startswith("/Game/"):
            continue
        gor.add(p)
        varliklar = paket_varliklari(p)
        if any(sinif(a) == "StaticMesh" for a in varliklar):
            aglar.add(p)
            continue   # ağın bağımlılıkları (malzeme/doku) gerekmez
        if any(sinif(a) == "World" for a in varliklar):
            kuyruk.extend(harici_aktorler(p))
        kuyruk.extend(bagimliliklar(p))
    log(f"{len(haritalar)} harita -> {len(gor)} paket tarandi, {len(aglar)} statik ag")
    return aglar


def klasordeki_aglar(klasor):
    out = set()
    try:
        for ad in ar.get_assets_by_path(klasor, recursive=True):
            if sinif(ad) == "StaticMesh":
                out.add(str(ad.package_name))
    except Exception as e:  # noqa: BLE001
        uyar(f"klasor okunamadi {klasor}: {e}")
    return out


def kok_paket(p):
    parca = p.split("/")
    return parca[2] if len(parca) > 2 else p


def isle(paketler):
    sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    try:
        varsayilan = unreal.PhysicsSettings.get_default_object().get_editor_property("default_shape_complexity")
    except Exception:  # noqa: BLE001
        varsayilan = CTF.CTF_USE_SIMPLE_AND_COMPLEX
    sayac = {}
    ornek_yok = {}
    for i, p in enumerate(sorted(paketler)):
        k = kok_paket(p)
        c = sayac.setdefault(k, dict(ag=0, basit=0, yalniz_karmasik=0, carpismasiz=0, duzeltilen=0, onceden=0, kucuk=0, hata=0))
        c["ag"] += 1
        try:
            ad = p.split("/")[-1]
            sm = eal.load_asset(f"{p}.{ad}")
            if not isinstance(sm, unreal.StaticMesh):
                for a in paket_varliklari(p):
                    if sinif(a) == "StaticMesh":
                        sm = eal.load_asset(str(a.package_name) + "." + str(a.asset_name))
                        break
            if not isinstance(sm, unreal.StaticMesh):
                c["hata"] += 1
                continue
            basit = sme.get_simple_collision_count(sm) + sme.get_convex_collision_count(sm)
            bs = sm.get_editor_property("body_setup")
            bayrak = bs.get_editor_property("collision_trace_flag") if bs else CTF.CTF_USE_SIMPLE_AS_COMPLEX
            etkin = varsayilan if bayrak == CTF.CTF_USE_DEFAULT else bayrak
            if basit > 0:
                c["basit"] += 1
                continue
            if etkin == CTF.CTF_USE_COMPLEX_AS_SIMPLE:
                c["onceden"] += 1
                continue
            r = sm.get_bounds().sphere_radius
            adk = sm.get_name().lower()
            if r < KUCUK_CM or any(x in adk for x in CIM):
                c["kucuk"] += 1
                continue
            if etkin == CTF.CTF_USE_SIMPLE_AS_COMPLEX or bs is None:
                c["carpismasiz"] += 1
                ornek_yok.setdefault(k, [])
                if len(ornek_yok[k]) < 6:
                    ornek_yok[k].append(sm.get_name())
            else:
                c["yalniz_karmasik"] += 1
            if bs is None:
                continue
            sm.modify()
            bs.set_editor_property("collision_trace_flag", CTF.CTF_USE_COMPLEX_AS_SIMPLE)
            if eal.save_loaded_asset(sm, False):
                c["duzeltilen"] += 1
            else:
                c["hata"] += 1
        except Exception as e:  # noqa: BLE001
            c["hata"] += 1
            uyar(f"{p}: {e}")
        if i % 200 == 199:
            unreal.SystemLibrary.collect_garbage()
    toplam = {}
    for k, c in sorted(sayac.items()):
        for a, v in c.items():
            toplam[a] = toplam.get(a, 0) + v
        ek = f" (or. {', '.join(ornek_yok[k])})" if ornek_yok.get(k) else ""
        log(f"{k}: {c['ag']} ag - basit {c['basit']}, yalniz karmasik {c['yalniz_karmasik']}, carpismasiz ag sayisi {c['carpismasiz']}"
            f" (duzeltilen {c['duzeltilen']}), onceden duzeltilmis {c['onceden']}, kucuk/cim {c['kucuk']}, hata {c['hata']}{ek}")
    if toplam:
        log(f"TOPLAM: {toplam.get('ag', 0)} ag - basit {toplam.get('basit', 0)}, yalniz karmasik {toplam.get('yalniz_karmasik', 0)}, "
            f"carpismasiz ag sayisi {toplam.get('carpismasiz', 0)}, duzeltilen {toplam.get('duzeltilen', 0)}, onceden {toplam.get('onceden', 0)}, "
            f"kucuk/cim {toplam.get('kucuk', 0)}, hata {toplam.get('hata', 0)}")
    return {"paketler": sayac, "toplam": toplam}


def main():
    yol = os.environ.get("FAB_CARPISMA_GIRDI")
    if not yol or not os.path.isfile(yol):
        log("girdi yok; yapilacak bir sey yok")
        return
    with open(yol, encoding="utf-8-sig") as f:
        g = json.load(f)
    paketler = set()
    haritalar = [h for h in (g.get("haritalar") or []) if isinstance(h, str)]
    if haritalar:
        paketler |= haritalardan_aglar(haritalar)
    for k in g.get("klasorler") or []:
        paketler |= klasordeki_aglar(k)
    log(f"islenecek statik ag: {len(paketler)}")
    sonuc = isle(paketler)
    out = os.environ.get("FAB_CARPISMA_SONUC")
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(sonuc, f, ensure_ascii=False, indent=1)
    log("tamam")


try:
    main()
finally:
    if os.environ.get("SIMUMAV_EDITOR_ACIK") != "1":
        unreal.SystemLibrary.quit_editor()
