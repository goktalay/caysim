# Simumav — Editor'da HEADLESS çalışan varlık üretici (UE Python). Content/ altındaki ikili varlıklar elle değil bu betikle üretilir:
#   /Game/Maps/SimumavMain                 boş harita (dünya, ışık, gök, dron, arayüz çalışma anında C++'tan kurulur)
#   /Game/Simumav/Materials/M_SmvTerrain   prosedürel arazi: köşe rengi × doku gürültüsü (Color parametresi)
#   /Game/Simumav/Materials/M_SmvGlow      halka/kapı: Color × Glow yayılım (emissive)
#   /Game/Simumav/Materials/M_SmvPbr(2S)   GLB dron gövdeleri (çalışma anı ProceduralMesh): BaseColor × BaseColorTex, Metallic, Roughness, Emissive
#   /Game/Simumav/Materials/M_SmvPropDisc  dönen pervane bulanık diski (yarı saydam, Opacity)
#   /Game/Simumav/Materials/M_SmvTerrainWx arazi + hava (Wetness: koyulaşma, parlaklık, su birikintisi; Snow: düz yüzeylerde kar)
#   /Game/Simumav/Models/<ad>/...          Content/Simumav/ModelsSrc/*.glb (Interchange glTF; idempotent, kaynak sha1 etiketi)
# Ayrıca Ultra Dynamic Sky / Weather değişken dökümü (USmvAtmosphereLibrary; "UDSPROP" satırları) — atmosfer ad listesinin doğrulaması.
# Çalıştırma (Windows, şirket bilgisayarı / CI):
#   UnrealEditor-Cmd.exe <yol>\Simumav.uproject -ExecutePythonScript="<yol>\Scripts\Python\simumav_varliklar.py" -unattended -nop4 -nosplash -NullRHI -stdout
# Betik idempotenttir: varlık varsa yeniden oluşturmaz (SIMUMAV_YENIDEN=1 ortam değişkeniyle zorla).
import glob
import hashlib
import json
import os
import struct
import unreal

YENIDEN = os.environ.get("SIMUMAV_YENIDEN") == "1"
at = unreal.AssetToolsHelpers.get_asset_tools()
mel = unreal.MaterialEditingLibrary
eal = unreal.EditorAssetLibrary


def log(s):
    unreal.log("[simumav_varliklar] " + s)


def malzeme(yol, ad):
    tam = f"{yol}/{ad}"
    if eal.does_asset_exist(tam):
        if not YENIDEN:
            log(f"var: {tam}")
            return None
        eal.delete_asset(tam)
    m = at.create_asset(ad, yol, unreal.Material, unreal.MaterialFactoryNew())
    log(f"oluşturuldu: {tam}")
    return m


SURUM_ETIKETI = "smv_surum"


def malzeme_v(yol, ad, surum):
    """Sürümlü malzeme: etiket farklıysa (ya da SIMUMAV_YENIDEN) yeniden üretilir."""
    tam = f"{yol}/{ad}"
    if eal.does_asset_exist(tam):
        mevcut = eal.load_asset(tam)
        etiket = eal.get_metadata_tag(mevcut, SURUM_ETIKETI) if mevcut else ""
        if etiket == surum and not YENIDEN:
            log(f"var: {tam} (s{surum})")
            return None
        eal.delete_asset(tam)
    m = at.create_asset(ad, yol, unreal.Material, unreal.MaterialFactoryNew())
    eal.set_metadata_tag(m, SURUM_ETIKETI, surum)
    log(f"oluşturuldu: {tam} (s{surum})")
    return m


def pbr_malzemesi(ad, iki_yuz):
    m = malzeme_v("/Game/Simumav/Materials", ad, "1")
    if m is None:
        return
    if iki_yuz:
        m.set_editor_property("two_sided", True)
    renk = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -900, -100)
    renk.set_editor_property("parameter_name", "BaseColor")
    renk.set_editor_property("default_value", unreal.LinearColor(0.6, 0.6, 0.6, 1.0))
    doku = mel.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -900, 100)
    doku.set_editor_property("parameter_name", "BaseColorTex")
    beyaz = unreal.load_asset("/Engine/EngineResources/WhiteSquareTexture")
    if beyaz:
        doku.set_editor_property("texture", beyaz)
    kullan = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -900, 320)
    kullan.set_editor_property("parameter_name", "UseTex")
    kullan.set_editor_property("default_value", 0.0)
    carp = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -600, 40)
    mel.connect_material_expressions(renk, "", carp, "A")
    mel.connect_material_expressions(doku, "RGB", carp, "B")
    lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -350, 0)
    mel.connect_material_expressions(renk, "", lerp, "A")
    mel.connect_material_expressions(carp, "", lerp, "B")
    mel.connect_material_expressions(kullan, "", lerp, "Alpha")
    mel.connect_material_property(lerp, "", unreal.MaterialProperty.MP_BASE_COLOR)
    for i, (pad, mp, dv) in enumerate((("Metallic", unreal.MaterialProperty.MP_METALLIC, 0.1), ("Roughness", unreal.MaterialProperty.MP_ROUGHNESS, 0.6))):
        e = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -350, 250 + i * 120)
        e.set_editor_property("parameter_name", pad)
        e.set_editor_property("default_value", dv)
        mel.connect_material_property(e, "", mp)
    em = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -350, 520)
    em.set_editor_property("parameter_name", "Emissive")
    em.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 1.0))
    mel.connect_material_property(em, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def pervane_diski():
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvPropDisc", "1")
    if m is None:
        return
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("two_sided", True)
    renk = mel.create_material_expression(m, unreal.MaterialExpressionConstant3Vector, -500, 0)
    renk.set_editor_property("constant", unreal.LinearColor(0.025, 0.025, 0.03, 1.0))
    mel.connect_material_property(renk, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
    op.set_editor_property("parameter_name", "Opacity")
    op.set_editor_property("default_value", 0.3)
    mel.connect_material_property(op, "", unreal.MaterialProperty.MP_OPACITY)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def hava_arazi_malzemesi():
    """M_SmvTerrain + hava: Wetness (koyulaşma, düşük pürüzlülük, gürültü maskeli su birikintileri), Snow (yukarı bakan yüzeyde kar)."""
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvTerrainWx", "1")
    if m is None:
        return
    E = lambda cls, x, y: mel.create_material_expression(m, cls, x, y)  # noqa: E731
    vc = E(unreal.MaterialExpressionVertexColor, -1400, 0)
    renk = E(unreal.MaterialExpressionVectorParameter, -1400, 150)
    renk.set_editor_property("parameter_name", "Color")
    renk.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    c1 = E(unreal.MaterialExpressionMultiply, -1150, 60)
    mel.connect_material_expressions(vc, "RGB", c1, "A")
    mel.connect_material_expressions(renk, "", c1, "B")
    gur = E(unreal.MaterialExpressionNoise, -1400, 350)
    gur.set_editor_property("scale", 0.02)
    koyu = E(unreal.MaterialExpressionConstant, -1150, 300); koyu.set_editor_property("r", 0.75)
    bir = E(unreal.MaterialExpressionConstant, -1150, 360); bir.set_editor_property("r", 1.0)
    l1 = E(unreal.MaterialExpressionLinearInterpolate, -950, 300)
    mel.connect_material_expressions(koyu, "", l1, "A"); mel.connect_material_expressions(bir, "", l1, "B"); mel.connect_material_expressions(gur, "", l1, "Alpha")
    taban = E(unreal.MaterialExpressionMultiply, -800, 100)
    mel.connect_material_expressions(c1, "", taban, "A"); mel.connect_material_expressions(l1, "", taban, "B")
    islak = E(unreal.MaterialExpressionScalarParameter, -1400, 600); islak.set_editor_property("parameter_name", "Wetness"); islak.set_editor_property("default_value", 0.0)
    kar = E(unreal.MaterialExpressionScalarParameter, -1400, 800); kar.set_editor_property("parameter_name", "Snow"); kar.set_editor_property("default_value", 0.0)
    # ıslak: renk ×0,55
    yarim = E(unreal.MaterialExpressionConstant, -800, 260); yarim.set_editor_property("r", 0.55)
    koyulas = E(unreal.MaterialExpressionMultiply, -650, 200)
    mel.connect_material_expressions(taban, "", koyulas, "A"); mel.connect_material_expressions(yarim, "", koyulas, "B")
    l2 = E(unreal.MaterialExpressionLinearInterpolate, -450, 100)
    mel.connect_material_expressions(taban, "", l2, "A"); mel.connect_material_expressions(koyulas, "", l2, "B"); mel.connect_material_expressions(islak, "", l2, "Alpha")
    # kar maskesi: Snow × saturate(normal.z·2 − 1)
    nrm = E(unreal.MaterialExpressionVertexNormalWS, -1400, 950)
    maske = E(unreal.MaterialExpressionComponentMask, -1200, 950)
    maske.set_editor_property("r", False); maske.set_editor_property("g", False); maske.set_editor_property("b", True); maske.set_editor_property("a", False)
    mel.connect_material_expressions(nrm, "", maske, "")
    iki = E(unreal.MaterialExpressionConstant, -1200, 1050); iki.set_editor_property("r", 2.0)
    m2 = E(unreal.MaterialExpressionMultiply, -1050, 980)
    mel.connect_material_expressions(maske, "", m2, "A"); mel.connect_material_expressions(iki, "", m2, "B")
    eksi = E(unreal.MaterialExpressionConstant, -1050, 1080); eksi.set_editor_property("r", -1.0)
    top = E(unreal.MaterialExpressionAdd, -900, 1000)
    mel.connect_material_expressions(m2, "", top, "A"); mel.connect_material_expressions(eksi, "", top, "B")
    doy = E(unreal.MaterialExpressionSaturate, -780, 1000)
    mel.connect_material_expressions(top, "", doy, "")
    karm = E(unreal.MaterialExpressionMultiply, -650, 900)
    mel.connect_material_expressions(kar, "", karm, "A"); mel.connect_material_expressions(doy, "", karm, "B")
    beyaz = E(unreal.MaterialExpressionConstant3Vector, -450, 300)
    beyaz.set_editor_property("constant", unreal.LinearColor(0.88, 0.9, 0.94, 1.0))
    l3 = E(unreal.MaterialExpressionLinearInterpolate, -250, 150)
    mel.connect_material_expressions(l2, "", l3, "A"); mel.connect_material_expressions(beyaz, "", l3, "B"); mel.connect_material_expressions(karm, "", l3, "Alpha")
    mel.connect_material_property(l3, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # pürüzlülük: kuru 0,92 → ıslak 0,35 → su birikintisi (iri gürültü maskesi) 0,06; kar 0,75
    gur2 = E(unreal.MaterialExpressionNoise, -1400, 1250); gur2.set_editor_property("scale", 0.004)
    esik = E(unreal.MaterialExpressionConstant, -1200, 1350); esik.set_editor_property("r", -0.58)
    t2 = E(unreal.MaterialExpressionAdd, -1050, 1280)
    mel.connect_material_expressions(gur2, "", t2, "A"); mel.connect_material_expressions(esik, "", t2, "B")
    alti = E(unreal.MaterialExpressionConstant, -1050, 1380); alti.set_editor_property("r", 7.0)
    m3 = E(unreal.MaterialExpressionMultiply, -900, 1300)
    mel.connect_material_expressions(t2, "", m3, "A"); mel.connect_material_expressions(alti, "", m3, "B")
    d3 = E(unreal.MaterialExpressionSaturate, -780, 1300)
    mel.connect_material_expressions(m3, "", d3, "")
    birik = E(unreal.MaterialExpressionMultiply, -650, 1250)
    mel.connect_material_expressions(d3, "", birik, "A"); mel.connect_material_expressions(islak, "", birik, "B")
    kuru = E(unreal.MaterialExpressionConstant, -650, 600); kuru.set_editor_property("r", 0.92)
    ip = E(unreal.MaterialExpressionConstant, -650, 660); ip.set_editor_property("r", 0.35)
    r1 = E(unreal.MaterialExpressionLinearInterpolate, -450, 620)
    mel.connect_material_expressions(kuru, "", r1, "A"); mel.connect_material_expressions(ip, "", r1, "B"); mel.connect_material_expressions(islak, "", r1, "Alpha")
    su = E(unreal.MaterialExpressionConstant, -450, 760); su.set_editor_property("r", 0.06)
    r2 = E(unreal.MaterialExpressionLinearInterpolate, -300, 700)
    mel.connect_material_expressions(r1, "", r2, "A"); mel.connect_material_expressions(su, "", r2, "B"); mel.connect_material_expressions(birik, "", r2, "Alpha")
    kr = E(unreal.MaterialExpressionConstant, -300, 820); kr.set_editor_property("r", 0.75)
    r3 = E(unreal.MaterialExpressionLinearInterpolate, -150, 760)
    mel.connect_material_expressions(r2, "", r3, "A"); mel.connect_material_expressions(kr, "", r3, "B"); mel.connect_material_expressions(karm, "", r3, "Alpha")
    mel.connect_material_property(r3, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def glb_dugum_adlari(yol):
    """GLB JSON parçasından hareketli düğüm adları (pervane / yüzey / teker / ışık) — günlük doğrulaması."""
    try:
        with open(yol, "rb") as f:
            d = f.read()
        n = struct.unpack_from("<I", d, 12)[0]
        j = json.loads(d[20:20 + n])
        adlar = [x.get("name", "") for x in j.get("nodes", [])]
        on = ("rotor", "prop", "blade", "aileron", "elevon", "elevator", "rudder", "ruddervator", "flap", "wheel", "steer", "nav_", "beacon")
        hareketli = sorted({a for a in adlar if a.lower().startswith(on) and not a.rsplit("_", 1)[-1][:1] in ("v", "g") or a.lower() in ("prop", "propeller")})
        return len(adlar), hareketli
    except Exception as e:  # noqa: BLE001
        return 0, [f"okunamadi: {e}"]


def modeller():
    """Content/Simumav/ModelsSrc/*.glb → /Game/Simumav/Models/<ad> (Interchange glTF). Kaynak sha1 statik ağ etiketinde; değişmediyse atlanır."""
    kaynak = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()), "Simumav", "ModelsSrc")
    dosyalar = sorted(glob.glob(os.path.join(kaynak, "*.glb")))
    log(f"modeller: {len(dosyalar)} GLB ({kaynak})")
    tamam = 0
    for f in dosyalar:
        ad = os.path.splitext(os.path.basename(f))[0]
        hedef = f"/Game/Simumav/Models/{ad}"
        with open(f, "rb") as fh:
            h = hashlib.sha1(fh.read()).hexdigest()
        nd, hareketli = glb_dugum_adlari(f)
        mevcut = eal.list_assets(hedef, recursive=True, include_folder=False) if eal.does_directory_exist(hedef) else []
        if mevcut and not YENIDEN:
            etiketli = False
            for p in mevcut:
                a = eal.load_asset(p.split(".")[0])
                if a and isinstance(a, unreal.StaticMesh) and eal.get_metadata_tag(a, "smv_kaynak") == h:
                    etiketli = True
                    break
            if etiketli:
                log(f"MODEL {ad}: var ({len(mevcut)} varlık, kaynak {h[:10]}); düğüm {nd}; hareketli: {', '.join(hareketli) or '-'}")
                tamam += 1
                continue
            eal.delete_directory(hedef)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", f)
        t.set_editor_property("destination_path", hedef)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        try:
            at.import_asset_tasks([t])
        except Exception as e:  # noqa: BLE001
            unreal.log_warning(f"[simumav_varliklar] MODEL {ad}: içe aktarılamadı: {e}")
            continue
        yollar = list(t.get_editor_property("imported_object_paths") or [])
        if not yollar and eal.does_directory_exist(hedef):
            yollar = list(eal.list_assets(hedef, recursive=True, include_folder=False))
        say = {}
        for p in yollar:
            a = eal.load_asset(p.split(".")[0])
            if not a:
                continue
            k = a.get_class().get_name()
            say[k] = say.get(k, 0) + 1
            if isinstance(a, unreal.StaticMesh):
                eal.set_metadata_tag(a, "smv_kaynak", h)
                eal.save_loaded_asset(a)
        ozet = ", ".join(f"{v} {k}" for k, v in sorted(say.items())) or "varlık yok"
        if say.get("StaticMesh", 0) or say.get("SkeletalMesh", 0):
            tamam += 1
            log(f"MODEL {ad}: içe aktarıldı → {hedef}: {ozet}; düğüm {nd}; hareketli: {', '.join(hareketli) or '-'}")
        else:
            unreal.log_warning(f"[simumav_varliklar] MODEL {ad}: ağ üretilmedi ({ozet})")
    log(f"modeller tamam: {tamam}/{len(dosyalar)}")


def uds_dokum():
    try:
        lib = getattr(unreal, "SmvAtmosphereLibrary", None)
        if lib is None:
            log("UDS dökümü: SmvAtmosphereLibrary yok (modül yüklenmedi)")
            return
        out = lib.dump_uds_properties()
        log(f"UDS dökümü: {len(out.splitlines())} satır")
    except Exception as e:  # noqa: BLE001
        unreal.log_warning(f"[simumav_varliklar] UDS dökümü başarısız: {e}")


def arazi_malzemesi():
    m = malzeme("/Game/Simumav/Materials", "M_SmvTerrain")
    if m is None:
        return
    vc = mel.create_material_expression(m, unreal.MaterialExpressionVertexColor, -600, 0)
    renk = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -600, 200)
    renk.set_editor_property("parameter_name", "Color")
    renk.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    carp = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -350, 80)
    mel.connect_material_expressions(vc, "RGB", carp, "A")
    mel.connect_material_expressions(renk, "", carp, "B")
    # ince ölçekli gürültü → çimen/toprak çeşitliliği (doku varlığı gerektirmez)
    gur = mel.create_material_expression(m, unreal.MaterialExpressionNoise, -600, 400)
    gur.set_editor_property("scale", 0.02)
    lerp = mel.create_material_expression(m, unreal.MaterialExpressionLinearInterpolate, -150, 200)
    koyu = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -350, 300)
    koyu.set_editor_property("r", 0.75)
    bir = mel.create_material_expression(m, unreal.MaterialExpressionConstant, -350, 360)
    bir.set_editor_property("r", 1.0)
    mel.connect_material_expressions(koyu, "", lerp, "A")
    mel.connect_material_expressions(bir, "", lerp, "B")
    mel.connect_material_expressions(gur, "", lerp, "Alpha")
    son = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, 50, 120)
    mel.connect_material_expressions(carp, "", son, "A")
    mel.connect_material_expressions(lerp, "", son, "B")
    mel.connect_material_property(son, "", unreal.MaterialProperty.MP_BASE_COLOR)
    puruz = mel.create_material_expression(m, unreal.MaterialExpressionConstant, 50, 300)
    puruz.set_editor_property("r", 0.92)
    mel.connect_material_property(puruz, "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def parlak_malzeme():
    m = malzeme("/Game/Simumav/Materials", "M_SmvGlow")
    if m is None:
        return
    renk = mel.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -500, 0)
    renk.set_editor_property("parameter_name", "Color")
    renk.set_editor_property("default_value", unreal.LinearColor(1.0, 0.45, 0.05, 1.0))
    parla = mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -500, 200)
    parla.set_editor_property("parameter_name", "Glow")
    parla.set_editor_property("default_value", 2.0)
    carp = mel.create_material_expression(m, unreal.MaterialExpressionMultiply, -250, 120)
    mel.connect_material_expressions(renk, "", carp, "A")
    mel.connect_material_expressions(parla, "", carp, "B")
    mel.connect_material_property(renk, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(carp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)


def harita():
    yol = "/Game/Maps/SimumavMain"
    if eal.does_asset_exist(yol) and not YENIDEN:
        log(f"var: {yol}")
        return
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not les.new_level(yol):
        unreal.log_error(f"harita oluşturulamadı: {yol}")
        return
    # Oyuncu başlangıcı (piyon kamerası dron'a bağlanır; konum önemsiz)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    eas.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 200), unreal.Rotator(0, 0, 0))
    les.save_current_level()
    log(f"oluşturuldu: {yol}")


def main():
    arazi_malzemesi()
    parlak_malzeme()
    for adim in (lambda: pbr_malzemesi("M_SmvPbr", False), lambda: pbr_malzemesi("M_SmvPbr2S", True), pervane_diski, hava_arazi_malzemesi):
        try:
            adim()
        except Exception as e:  # noqa: BLE001 — eksik malzeme oyunu durdurmaz (yedek malzemeler)
            unreal.log_warning(f"[simumav_varliklar] malzeme üretilemedi: {e}")
    harita()
    if os.environ.get("SIMUMAV_MODELLER", "1") == "1":
        try:
            modeller()
        except Exception as e:  # noqa: BLE001
            unreal.log_warning(f"[simumav_varliklar] model içe aktarma başarısız: {e}")
    uds_dokum()
    log("tamam")


try:
    main()
finally:
    # -ExecutePythonScript editörü açık bırakır; CI'da kapanmalı
    if os.environ.get("SIMUMAV_EDITOR_ACIK") != "1":
        unreal.SystemLibrary.quit_editor()
