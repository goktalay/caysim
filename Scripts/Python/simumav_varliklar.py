# Simumav — Editor'da HEADLESS çalışan varlık üretici (UE Python). Content/ altındaki ikili varlıklar elle değil bu betikle üretilir:
#   /Game/Maps/SimumavMain                 boş harita (dünya, ışık, gök, dron, arayüz çalışma anında C++'tan kurulur)
#   /Game/Simumav/Materials/M_SmvTerrain   prosedürel arazi: köşe rengi × doku gürültüsü (Color parametresi)
#   /Game/Simumav/Materials/M_SmvGlow      halka/kapı: Color × Glow yayılım (emissive)
#   /Game/Simumav/Materials/M_SmvPbr(2S)   GLB dron gövdeleri (çalışma anı ProceduralMesh): BaseColor × BaseColorTex, Metallic, Roughness, Emissive
#   /Game/Simumav/Materials/M_SmvPropDisc  dönen pervane bulanık diski (yarı saydam, Opacity)
#   /Game/Simumav/Materials/M_SmvTerrainWx arazi + hava (Wetness: koyulaşma, parlaklık, su birikintisi; Snow: düz yüzeylerde kar)
#   /Game/Simumav/Materials/M_SmvTerrainHyper  Hyper Auto Landscape dokularıyla eğim/yükseklik otomatik arazi (Fab paketi varsa)
#   /Game/Simumav/Materials/M_SmvStreet    şehir asfaltı / kaldırım taşı (dünya hizalı gürültü + karo derzi)
#   /Game/Simumav/Materials/M_SmvFacade    bölge haritası binaları: kat / pencere ızgarası (UV metre), gece yanan pencereler, çatı (Windows = 0)
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
    # s2: iskelet ağı kullanım bayrağı (MetaHuman pilot gövdesi tulum rengi; bayraksız malzeme pakette varsayılana düşüyordu)
    m = malzeme_v("/Game/Simumav/Materials", ad, "2")
    if m is None:
        return
    for kullanim in (unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
        try:
            mel.set_material_usage(m, kullanim)
        except Exception as e:  # noqa: BLE001
            log(f"{ad}: kullanım bayrağı yazılamadı ({e})")
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


class Graf:
    """Malzeme grafiği kısaltmaları (düğüm konumları yalnız görünüm için sırayla verilir)."""

    def __init__(self, m):
        self.m = m
        self.n = 0

    def e(self, cls, **ozellik):
        self.n += 1
        x = mel.create_material_expression(self.m, cls, -2400 + (self.n % 12) * 180, -1200 + (self.n // 12) * 140)
        for k, v in ozellik.items():
            x.set_editor_property(k, v)
        return x

    def bag(self, a, b, pin="", acik=""):
        mel.connect_material_expressions(a, acik, b, pin)
        return b

    def c(self, v):
        return self.e(unreal.MaterialExpressionConstant, r=float(v))

    def ikili(self, cls, a, b):
        x = self.e(cls)
        self.bag(a if not isinstance(a, (int, float)) else self.c(a), x, "A")
        self.bag(b if not isinstance(b, (int, float)) else self.c(b), x, "B")
        return x

    def mul(self, a, b):
        return self.ikili(unreal.MaterialExpressionMultiply, a, b)

    def add(self, a, b):
        return self.ikili(unreal.MaterialExpressionAdd, a, b)

    def sub(self, a, b):
        return self.ikili(unreal.MaterialExpressionSubtract, a, b)

    def lerp(self, a, b, t):
        x = self.e(unreal.MaterialExpressionLinearInterpolate)
        self.bag(a if not isinstance(a, (int, float)) else self.c(a), x, "A")
        self.bag(b if not isinstance(b, (int, float)) else self.c(b), x, "B")
        self.bag(t, x, "Alpha")
        return x

    def sat(self, a):
        return self.bag(a, self.e(unreal.MaterialExpressionSaturate))

    def abs(self, a):
        return self.bag(a, self.e(unreal.MaterialExpressionAbs))

    def frac(self, a):
        return self.bag(a, self.e(unreal.MaterialExpressionFrac))

    def maske(self, a, r=False, g=False, b=False, al=False):
        return self.bag(a, self.e(unreal.MaterialExpressionComponentMask, r=r, g=g, b=b, a=al))

    def ekle(self, a, b):
        x = self.e(unreal.MaterialExpressionAppendVector)
        self.bag(a, x, "A")
        self.bag(b, x, "B")
        return x

    def skaler(self, ad, v):
        return self.e(unreal.MaterialExpressionScalarParameter, parameter_name=ad, default_value=float(v))

    def vektor(self, ad, r, g, b):
        return self.e(unreal.MaterialExpressionVectorParameter, parameter_name=ad, default_value=unreal.LinearColor(r, g, b, 1.0))


def ornek_tipi(t, normal, sanal):
    ST = unreal.MaterialSamplerType
    TC = unreal.TextureCompressionSettings
    try:
        cs = t.get_editor_property("compression_settings")
    except Exception:  # noqa: BLE001
        cs = None
    if normal or cs == TC.TC_NORMALMAP:
        return ST.SAMPLERTYPE_VIRTUAL_NORMAL if sanal else ST.SAMPLERTYPE_NORMAL
    if cs == TC.TC_MASKS:
        return ST.SAMPLERTYPE_VIRTUAL_MASKS if sanal else ST.SAMPLERTYPE_MASKS
    if cs in (TC.TC_GRAYSCALE, TC.TC_ALPHA):
        srgb = bool(t.get_editor_property("srgb"))
        if sanal:
            return ST.SAMPLERTYPE_VIRTUAL_GRAYSCALE if srgb else ST.SAMPLERTYPE_VIRTUAL_LINEAR_GRAYSCALE
        return ST.SAMPLERTYPE_GRAYSCALE if srgb else ST.SAMPLERTYPE_LINEAR_GRAYSCALE
    srgb = bool(t.get_editor_property("srgb"))
    if sanal:
        return ST.SAMPLERTYPE_VIRTUAL_COLOR if srgb else ST.SAMPLERTYPE_VIRTUAL_LINEAR_COLOR
    return ST.SAMPLERTYPE_COLOR if srgb else ST.SAMPLERTYPE_LINEAR_COLOR


def malzeme_derlendi(m):
    """Derleme başarısını yönerge sayısıyla denetle (hatalı grafikte piksel gölgelendirici yönergesi 0)."""
    try:
        st = mel.get_statistics(m)
        return int(st.get_editor_property("num_pixel_shader_instructions")) > 0
    except Exception as e:  # noqa: BLE001
        log(f"malzeme istatistiği okunamadı ({e}) — başarılı sayıldı")
        return True


HYPER = "/Game/Hyper/ResourcePack"
HYPER_DOKU = {
    "cimD": HYPER + "/Environments/Foliage/Grass/GrassPatch_01/T_Grass_Blobby_Tiling_D",
    "cimN": HYPER + "/Environments/Foliage/Grass/GrassPatch_01/T_Grass_Blobby_Tiling_N",
    "ormanD": HYPER + "/Tileable_Materials/Landscape/forest_ground_04/textures/T_forest_ground_04_diff",
    "ormanN": HYPER + "/Tileable_Materials/Landscape/forest_ground_04/textures/T_forest_ground_04_nor",
    "kumD": HYPER + "/Tileable_Materials/sand_02/textures/T_sand_02_diff",
    "kumN": HYPER + "/Tileable_Materials/sand_02/textures/T_sand_02_nor",
    "kayaD": HYPER + "/Tileable_Materials/Landscape/RockWall/Textures/T_RockCliff_4K_Color",
    "kayaN": HYPER + "/Tileable_Materials/Landscape/RockWall/Textures/T_RockCliff_4K_Normal",
    "karD": HYPER + "/Tileable_Materials/Landscape/Snow/Textures/T_Snow_Color",
    "karN": HYPER + "/Tileable_Materials/Landscape/Snow/Textures/T_Snow_Normal",
    "makro": HYPER + "/Tileable_Materials/Landscape/Grass/Textures/T_MacroVariation",
    "gurultu": HYPER + "/Environments/Textures/T_TilingNoise06",
}


def hyper_arazi_malzemesi():
    """M_SmvTerrainHyper — Hyper Auto Landscape Material v4 dokularıyla EĞİM / YÜKSEKLİK tabanlı otomatik arazi malzemesi
    (prosedürel arazi ProceduralMesh olduğu için Hyper'ın Landscape katman malzemesi doğrudan kullanılamaz; aynı fikir burada:
    düz yüzey çimen/orman toprağı (makro desenle karışık), dik yamaç kaya (iki düzlemli izdüşüm), SnowLine üstü ve Snow
    parametresiyle kar, Sand ile çöl kumu, Wetness ile ıslak zemin). Uzakta iri ölçekli örnek (desen tekrarı azalır)."""
    eksik = [k for k, y in HYPER_DOKU.items() if not eal.does_asset_exist(y)]
    if eksik:
        log("Hyper dokuları yok (" + ", ".join(eksik) + ") — M_SmvTerrainHyper üretilmedi (M_SmvTerrainWx kalır)")
        return
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvTerrainHyper", "7")
    if m is None:
        return
    try:
        vt_acik = bool(unreal.get_default_object(unreal.RendererSettings).get_editor_property("virtual_textures"))
    except Exception:  # noqa: BLE001
        vt_acik = False
    dokular = {k: eal.load_asset(y) for k, y in HYPER_DOKU.items()}
    # örnekleyici türü dokunun gerçek durumundan: sanal doku akışı yalnız proje VT desteği açıksa (-NullRHI'de derleme istatistiği
    # güvenilir olmadığından deneme-yanılma yapılmaz; ilk sürümde "Virtual Normal, should be Normal" hatasıyla varsayılan malzemeye düşmüştü)
    for deneme in range(1):
        g = Graf(m)
        wp = g.e(unreal.MaterialExpressionWorldPosition)
        xy = g.maske(wp, r=True, g=True)
        z = g.maske(wp, b=True)
        nrm = g.e(unreal.MaterialExpressionVertexNormalWS)
        nx = g.abs(g.maske(nrm, r=True))
        ny = g.abs(g.maske(nrm, g=True))
        nz = g.maske(nrm, b=True)
        cam = g.e(unreal.MaterialExpressionCameraPositionWS)
        uz = g.e(unreal.MaterialExpressionDistance)
        g.bag(wp, uz, "A"); g.bag(cam, uz, "B")
        uzak = g.sat(g.mul(g.sub(uz, 2500.0), 1.0 / 15000.0))

        def doku(ad, uv, normal=False):
            t = dokular[ad]
            sanal = bool(t.get_editor_property("virtual_texture_streaming"))  # proje VT desteği açık (T_Grass_Blobby_Tiling_N sanal)
            x = g.e(unreal.MaterialExpressionTextureSample, texture=t, sampler_type=ornek_tipi(t, normal, sanal),
                    sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
            g.bag(uv, x, "UVs")
            return x

        def uv(olcek_cm):
            return g.mul(xy, 1.0 / olcek_cm)

        # s6: dokular yalnız AYRINTI (parlaklık) için; katman rengi parametreden — duman testinde Hyper dokularının kendi tonu
        # uzakta beyazımsı / kırmızımsı görünüyordu. ayrinti = clamp(parlaklık × kazanç, 0,55, 1,45)
        lumv = g.e(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.3, 0.59, 0.11, 1.0))

        def ayr(tex, kazanc):
            d = g.e(unreal.MaterialExpressionDotProduct)
            g.bag(g.maske(tex, r=True, g=True, b=True), d, "A")
            g.bag(lumv, d, "B")
            k = g.e(unreal.MaterialExpressionClamp, min_default=0.55, max_default=1.45)
            g.bag(g.mul(d, kazanc), k, "")
            return k

        makro = g.sat(g.mul(g.sub(g.maske(doku("makro", uv(30000.0)), r=True), 0.35), 2.5))
        genis = g.lerp(0.85, 1.1, g.maske(doku("gurultu", uv(9000.0)), r=True))
        cim = g.mul(g.vektor("GrassColor", 0.13, 0.2, 0.065), g.lerp(ayr(doku("cimD", uv(300.0)), 2.2), ayr(doku("cimD", uv(1800.0)), 2.2), uzak))
        orman = g.mul(g.vektor("ForestColor", 0.17, 0.14, 0.08), g.lerp(ayr(doku("ormanD", uv(420.0)), 2.5), ayr(doku("ormanD", uv(2600.0)), 2.5), uzak))
        zemin = g.mul(g.lerp(cim, orman, makro), genis)
        kum = g.mul(g.vektor("SandColor", 0.56, 0.45, 0.31), g.lerp(ayr(doku("kumD", uv(380.0)), 1.8), ayr(doku("kumD", uv(2300.0)), 1.8), uzak))
        kum_on = g.skaler("Sand", 0.0)
        zemin = g.lerp(zemin, g.mul(kum, genis), kum_on)
        zemin = g.mul(zemin, g.vektor("Tint", 1.0, 1.0, 1.0))
        # kaya: iki düzlemli izdüşüm (YZ ve XZ), yüzey yönüne göre karışım
        uvA = g.mul(g.ekle(g.maske(wp, g=True), z), 1.0 / 900.0)
        uvB = g.mul(g.ekle(g.maske(wp, r=True), z), 1.0 / 900.0)
        wB = g.sat(g.ikili(unreal.MaterialExpressionDivide, nx, g.add(g.add(nx, ny), 0.001)))
        kaya = g.mul(g.vektor("RockColor", 0.3, 0.28, 0.25), g.lerp(ayr(doku("kayaD", uvB), 2.2), ayr(doku("kayaD", uvA), 2.2), wB))
        kayaN = g.lerp(doku("kayaN", uvB, True), doku("kayaN", uvA, True), wB)
        egim = g.sub(1.0, nz)
        kaya_m = g.sat(g.add(g.mul(g.sub(egim, g.skaler("RockSlope", 0.22)), g.skaler("RockSharp", 5.0)), g.mul(g.sub(makro, 0.5), 0.3)))
        duz = g.sat(g.sub(g.mul(nz, 2.0), 1.0))
        kar_h = g.mul(g.skaler("Snow", 0.0), duz)
        kar_y = g.mul(g.sat(g.ikili(unreal.MaterialExpressionDivide, g.sub(z, g.skaler("SnowLine", 1.0e8)), g.skaler("SnowFade", 4000.0))), g.sat(g.sub(g.mul(nz, 2.5), 1.2)))
        kar_m = g.ikili(unreal.MaterialExpressionMax, kar_h, kar_y)
        renk = g.lerp(zemin, kaya, kaya_m)
        renk = g.lerp(renk, g.mul(g.vektor("SnowColor", 0.82, 0.85, 0.9), ayr(doku("karD", uv(500.0)), 1.1)), kar_m)
        islak = g.skaler("Wetness", 0.0)
        renk = g.mul(renk, g.lerp(1.0, 0.55, islak))
        mel.connect_material_property(renk, "", unreal.MaterialProperty.MP_BASE_COLOR)
        zn = g.lerp(doku("cimN", uv(300.0), True), doku("ormanN", uv(420.0), True), makro)
        zn = g.lerp(zn, doku("kumN", uv(380.0), True), kum_on)
        nn = g.lerp(zn, kayaN, kaya_m)
        nn = g.lerp(nn, doku("karN", uv(500.0), True), kar_m)
        # s7: normal haritaları düzleştirilir (NormalStrength; uzakta daha da) ve yansıma düşük — duman testinde gökyüzü yansıması
        # ve iri ölçekli normal kabartısı zemini beyaz-mor lekeli gösteriyordu
        guc = g.mul(g.skaler("NormalStrength", 0.45), g.lerp(1.0, 0.35, uzak))
        duzN = g.e(unreal.MaterialExpressionConstant3Vector, constant=unreal.LinearColor(0.0, 0.0, 1.0, 1.0))
        nn = g.lerp(duzN, nn, guc)
        mel.connect_material_property(nn, "", unreal.MaterialProperty.MP_NORMAL)
        mel.connect_material_property(g.skaler("Specular", 0.2), "", unreal.MaterialProperty.MP_SPECULAR)
        pr = g.lerp(0.93, 0.78, kaya_m)
        pr = g.lerp(pr, 0.55, kar_m)
        pr = g.lerp(pr, 0.3, islak)
        mel.connect_material_property(pr, "", unreal.MaterialProperty.MP_ROUGHNESS)
        mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_SmvTerrainHyper hazır (Hyper dokuları, eğim/yükseklik karışımı)")


def sokak_malzemesi():
    """M_SmvStreet — kodla kurulan şehrin asfalt / kaldırım / meydan yüzeyleri: dünya hizalı gürültü (Hyper T_TilingNoise06 ya da
    motor gürültüsü), TileCm > 0 ise karo derzleri (kaldırım taşı), Color / Rough parametreleri."""
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvStreet", "2")
    if m is None:
        return
    g = Graf(m)
    wp = g.e(unreal.MaterialExpressionWorldPosition)
    xy = g.maske(wp, r=True, g=True)
    gt = eal.load_asset(HYPER_DOKU["gurultu"]) if eal.does_asset_exist(HYPER_DOKU["gurultu"]) else None

    def gur(olcek):
        if gt:
            x = g.e(unreal.MaterialExpressionTextureSample, texture=gt, sampler_type=ornek_tipi(gt, False, False),
                    sampler_source=unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
            g.bag(g.mul(xy, 1.0 / olcek), x, "UVs")
            return g.maske(x, r=True)
        n = g.e(unreal.MaterialExpressionNoise, scale=100.0 / olcek)
        g.bag(wp, n, "Position")
        return n

    renk = g.vektor("Color", 0.05, 0.05, 0.055)
    ince = g.lerp(0.72, 1.12, gur(240.0))
    iri = g.lerp(0.88, 1.06, gur(2600.0))
    taban = g.mul(g.mul(renk, ince), iri)
    karo = g.skaler("TileCm", 0.0)
    kv = g.frac(g.ikili(unreal.MaterialExpressionDivide, xy, g.ikili(unreal.MaterialExpressionMax, karo, 1.0)))
    kx = g.maske(kv, r=True)
    ky = g.maske(kv, g=True)
    kenar = g.ikili(unreal.MaterialExpressionMin, g.ikili(unreal.MaterialExpressionMin, kx, g.sub(1.0, kx)), g.ikili(unreal.MaterialExpressionMin, ky, g.sub(1.0, ky)))
    derz = g.sub(1.0, g.sat(g.ikili(unreal.MaterialExpressionDivide, g.mul(kenar, karo), 2.5)))
    karo_on = g.sat(karo)
    son = g.mul(taban, g.lerp(1.0, 0.55, g.mul(derz, karo_on)))
    mel.connect_material_property(son, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(g.lerp(g.skaler("Rough", 0.85), 0.95, g.mul(derz, karo_on)), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log(f"M_SmvStreet hazır ({'Hyper gürültü dokusu' if gt else 'motor gürültüsü'})")


def cephe_malzemesi():
    """M_SmvFacade — bölge haritası (OSM) binaları: UV metre (U duvar boyunca, V yükseklik). Windows = 1 → kat / pencere ızgarası
    (BayM × FloorM hücre, cam: koyu, parlak; gece Night > 0 iken hücre başına rastgele yanan pencereler), 0 → çatı. Duvar rengi köşe rengi × Color
    × dünya gürültüsü (sıva lekeleri)."""
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvFacade", "1")
    if m is None:
        return
    g = Graf(m)
    uv = g.e(unreal.MaterialExpressionTextureCoordinate, coordinate_index=0)
    hucre = g.ekle(g.skaler("BayM", 3.2), g.skaler("FloorM", 3.1))
    q = g.ikili(unreal.MaterialExpressionDivide, uv, hucre)
    f = g.frac(q)
    fx = g.maske(f, r=True)
    fy = g.maske(f, g=True)
    pw = g.sat(g.mul(g.sub(0.29, g.abs(g.sub(fx, 0.5))), 40.0))
    ph = g.sat(g.mul(g.sub(0.27, g.abs(g.sub(fy, 0.56))), 40.0))
    # zemin kat (V < FloorM): pencereler yere yakın, vitrin gibi (yükseklik maskesi daha uzun)
    zemin = g.sat(g.mul(g.sub(1.0, g.maske(q, g=True)), 1000.0))
    ph_z = g.sat(g.mul(g.sub(0.36, g.abs(g.sub(fy, 0.48))), 40.0))
    pen = g.mul(g.mul(pw, g.lerp(ph, ph_z, zemin)), g.skaler("Windows", 1.0))
    wp = g.e(unreal.MaterialExpressionWorldPosition)
    gur = g.e(unreal.MaterialExpressionNoise, scale=0.004, quality=1, levels=3, output_min=0.0, output_max=1.0)
    g.bag(wp, gur, "Position")
    vc = g.e(unreal.MaterialExpressionVertexColor)
    duvar = g.mul(g.mul(vc, g.vektor("Color", 1.0, 1.0, 1.0)), g.lerp(0.82, 1.06, gur))
    cam = g.vektor("GlassColor", 0.035, 0.05, 0.065)
    taban = g.lerp(duvar, cam, pen)
    mel.connect_material_property(taban, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(g.lerp(g.skaler("Rough", 0.88), 0.07, pen), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.mul(pen, 0.35), "", unreal.MaterialProperty.MP_METALLIC)
    # gece: hücre kimliğinden (taban hücre koordinatı) rastgele yanan pencereler
    tam = g.bag(q, g.e(unreal.MaterialExpressionFloor))
    rnd = g.e(unreal.MaterialExpressionNoise, scale=1.0, quality=1, levels=1, output_min=0.0, output_max=1.0)
    g.bag(g.ekle(g.mul(tam, 37.13), g.c(0.0)), rnd, "Position")
    yanar = g.sat(g.mul(g.sub(rnd, 0.55), 12.0))
    isik = g.mul(g.mul(g.mul(pen, yanar), g.skaler("Night", 0.0)), g.vektor("LitColor", 2.4, 1.7, 1.0))
    mel.connect_material_property(isik, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_SmvFacade hazır")


def cesium_bina_malzemesi():
    """M_SmvCesiumBina — Cesium OSM 3B binaları (dokusuz beyaz kutular) için dünya-konumlu cephe: duvarlarda kat / pencere ızgarası
    (yatay eksen duvar normaline göre dünya X ya da Y, dikey eksen dünya Z; BayM × FloorM hücre), çatıda koyu gri. Duvar rengi binadan
    binaya düşük frekanslı dünya gürültüsüyle bej / kum / açık gri arasında değişir (kullanıcı 2026-11-04: binalar beyaz kutu)."""
    m = malzeme_v("/Game/Simumav/Materials", "M_SmvCesiumBina", "1")
    if m is None:
        return
    g = Graf(m)
    wp = g.e(unreal.MaterialExpressionWorldPosition)
    n = g.e(unreal.MaterialExpressionVertexNormalWS)
    nx = g.abs(g.maske(n, r=True))
    ny = g.abs(g.maske(n, g=True))
    nz = g.abs(g.maske(n, b=True))
    duvar_m = g.sat(g.mul(g.sub(0.55, nz), 10.0))           # dik yüz = duvar
    yon = g.sat(g.mul(g.sub(ny, nx), 20.0))                  # normal Y'ye bakıyorsa duvar X boyunca uzanır
    yatay = g.lerp(g.maske(wp, g=True), g.maske(wp, r=True), yon)
    uv = g.ekle(g.mul(yatay, 0.01), g.mul(g.maske(wp, b=True), 0.01))   # metre
    hucre = g.ekle(g.skaler("BayM", 3.4), g.skaler("FloorM", 3.2))
    q = g.ikili(unreal.MaterialExpressionDivide, uv, hucre)
    f = g.frac(q)
    fx = g.maske(f, r=True)
    fy = g.maske(f, g=True)
    pw = g.sat(g.mul(g.sub(0.27, g.abs(g.sub(fx, 0.5))), 30.0))
    ph = g.sat(g.mul(g.sub(0.25, g.abs(g.sub(fy, 0.55))), 30.0))
    pen = g.mul(g.mul(pw, ph), duvar_m)
    # binadan binaya renk: ~60 m ölçekli gürültü; yüzeyde sıva lekeleri
    gur = g.e(unreal.MaterialExpressionNoise, scale=0.00015, quality=1, levels=2, output_min=0.0, output_max=1.0)
    g.bag(wp, gur, "Position")
    leke = g.e(unreal.MaterialExpressionNoise, scale=0.004, quality=1, levels=3, output_min=0.0, output_max=1.0)
    g.bag(wp, leke, "Position")
    renk = g.lerp(g.vektor("WallA", 0.72, 0.66, 0.56), g.vektor("WallB", 0.56, 0.57, 0.6), gur)
    renk = g.mul(renk, g.lerp(0.84, 1.05, leke))
    # kat çizgisi (döşeme bandı): hücrenin alt %6'sı biraz koyu
    bant = g.sat(g.mul(g.sub(0.06, fy), 40.0))
    renk = g.mul(renk, g.lerp(1.0, 0.82, g.mul(bant, duvar_m)))
    cati = g.mul(g.vektor("Roof", 0.3, 0.29, 0.28), g.lerp(0.85, 1.1, leke))
    yuzey = g.lerp(cati, renk, duvar_m)
    cam = g.vektor("GlassColor", 0.04, 0.055, 0.07)
    taban = g.lerp(yuzey, cam, pen)
    mel.connect_material_property(taban, "", unreal.MaterialProperty.MP_BASE_COLOR)
    mel.connect_material_property(g.lerp(0.9, 0.08, pen), "", unreal.MaterialProperty.MP_ROUGHNESS)
    mel.connect_material_property(g.mul(pen, 0.3), "", unreal.MaterialProperty.MP_METALLIC)
    mel.recompile_material(m)
    eal.save_loaded_asset(m)
    log("M_SmvCesiumBina hazır")


def kalabalik_nanite_kapat():
    """City Sample Crowds iskeletli ağlarında Nanite açıksa kapatılır: duman testinde yakın plan yayalar hiç çizilmedi ve baş malzemesi
    (M_Crowd_Head_v2) Nanite köşe üreticisi için derlenemedi. Varlık yoksa / Nanite kapalıysa hiçbir şey yapılmaz."""
    kok = "/Game/CitySampleCrowd/Character"
    if not eal.does_directory_exist(kok):
        return
    n = k = 0
    for yol in eal.list_assets(kok, recursive=True, include_folder=False):
        veri = eal.find_asset_data(yol)
        if not veri or str(veri.asset_class_path.asset_name) != "SkeletalMesh":
            continue
        n += 1
        m = eal.load_asset(yol)
        try:
            ns = m.get_editor_property("nanite_settings")
        except Exception:  # noqa: BLE001 — bu UE sürümünde iskeletli Nanite alanı yok
            log("kalabalık: iskeletli ağda nanite_settings yok — atlandı")
            return
        if ns.get_editor_property("enabled"):
            ns.set_editor_property("enabled", False)
            m.set_editor_property("nanite_settings", ns)
            eal.save_loaded_asset(m)
            k += 1
    log(f"kalabalık: {n} iskeletli ağ, Nanite kapatılan {k}")


MCC = "/Game/MetaCharacterCreator/Data/MetaHumans"
# gerçekçi MetaHuman yüzleri (ad, kadın mı, yaklaşık ten kademesi 1 açık … 8 koyu). Anime / kara elf / goblin yüzleri alınmaz.
MCC_YUZLER = (
    ("Jelena", True, 1), ("Tuya", True, 2), ("Ada", True, 3), ("Etta", True, 5), ("Asha", True, 6), ("Zuri", True, 8),
    ("Mikel", False, 1), ("Bruce", False, 2), ("Lorenzo", False, 3), ("Jorge", False, 4), ("Isaiah", False, 7), ("Kelvin", False, 8),
)
MCC_GROOMLAR = (
    ("Hair", ("Hair_S_Casual", "Hair_S_BuzzCut", "Hair_S_Messy", "Hair_S_SideSweptFringe", "Hair_S_CurlyFade", "Hair_M_BobCurly", "Hair_M_Mohawk",
              "Hair_L_Straight", "Hair_S_LowPonytail", "Hair_S_UpdoBuns", "Hair_M_BobStraight", "Hair_S_PulledBack")),
    ("Eyebrows", ("Eyebrows_M_Thin", "Eyebrows_M_Natural", "Eyebrows_M_Thick")),
    ("Beards", ("Beard_S_Stubble", "Beard_S_Full", "Goatee_M_Pointed")),
    ("Mustaches", ("Mustache_S_Full",)),
    ("Eyelashes", ("Eyelashes_L_SlightCurl", "Eyelashes_S_Thin")),
)


def pilot_kiti():
    """/Game/Simumav/Pilot/DA_SmvPilotKit — MCC'den seçilen gövde / yüz / groom / bağlama varlıklarına yumuşak başvurular
    (yalnız bunlar pakete pişirilir). Oyun avatar alanlarını bu kite eşler (SmvMetaPilot)."""
    hedef = "/Game/Simumav/Pilot/DA_SmvPilotKit"
    if not eal.does_directory_exist(MCC):
        log("MCC (Meta Character Creator) yok — pilot kiti üretilmedi (temel şekil pilot)")
        return
    sinif = getattr(unreal, "SmvPilotKit", None)
    if sinif is None:
        unreal.log_warning("[simumav_varliklar] SmvPilotKit sınıfı yok (modül yüklenmedi) — pilot kiti atlandı")
        return
    girdiler = []

    def ekle(tur, ad, yol, ek="", kadin=False, ton=3):
        tam = yol + "." + yol.rsplit("/", 1)[-1]
        if not eal.does_asset_exist(yol):
            return False
        nesne = eal.load_asset(yol)
        if not nesne:
            return False
        e = unreal.SmvPilotKitEntry()
        e.set_editor_property("kind", tur)
        e.set_editor_property("name", ad)
        e.set_editor_property("extra", ek)
        e.set_editor_property("female", kadin)
        e.set_editor_property("tone", ton)
        e.set_editor_property("asset", nesne)
        girdiler.append((tur, ad, ek, tam))
        kit_girdileri.append(e)
        return True

    kit_girdileri = []
    ekle("body_m", "Male", MCC + "/Common/Body/SKM_Human_Male")
    ekle("body_f", "Female", MCC + "/Common/Body/SKM_Human_Female", kadin=True)
    yuzler = []
    for ad, kadin, ton in MCC_YUZLER:
        yol = MCC + "/Common/Face/" + ("SKM_FH_" if kadin else "SKM_MH_") + ad
        if ekle("face", ad, yol, kadin=kadin, ton=ton):
            yuzler.append(ad)
        else:
            log(f"MCC yüzü yok: {yol}")
    eksik_b = 0
    for kat, adlar in MCC_GROOMLAR:
        for g in adlar:
            if not ekle("groom", g, f"{MCC}/Grooms/GroomAssets/{kat}/{g}/{g}"):
                log(f"MCC groom yok: {kat}/{g}")
                continue
            kisa = g[5:] if g.startswith("Hair_") else g
            for y in yuzler:
                if not ekle("binding", g, f"{MCC}/Grooms/Bindings/{y}/GB_{kisa}_{y}", ek=y):
                    eksik_b += 1
    imza = hashlib.sha1(json.dumps(girdiler).encode()).hexdigest()
    if eal.does_asset_exist(hedef):
        mevcut = eal.load_asset(hedef)
        if mevcut and eal.get_metadata_tag(mevcut, "smv_kit") == imza and not YENIDEN:
            log(f"var: {hedef} ({len(girdiler)} girdi)")
            return
        eal.delete_asset(hedef)
    fab = unreal.DataAssetFactory()
    fab.set_editor_property("data_asset_class", sinif)
    kit = at.create_asset("DA_SmvPilotKit", "/Game/Simumav/Pilot", sinif, fab)
    if not kit:
        unreal.log_warning("[simumav_varliklar] pilot kiti oluşturulamadı")
        return
    kit.set_editor_property("entries", kit_girdileri)
    eal.set_metadata_tag(kit, "smv_kit", imza)
    eal.save_loaded_asset(kit)
    say = {}
    for t, _, _, _ in girdiler:
        say[t] = say.get(t, 0) + 1
    log(f"pilot kiti üretildi: {hedef} — " + ", ".join(f"{v} {k}" for k, v in sorted(say.items())) + f"; eksik bağlama {eksik_b}; yüzler: {', '.join(yuzler)}")


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


NIAGARA_HEDEF = "/Game/Simumav/FX/NS_SmvMotes"
NIAGARA_TOZ = "/Game/Simumav/FX/NS_SmvDust"
# ana menü sahnesi için havada süzülen ışık zerreleri: motorun Niagara eklentisindeki hazır sistem şablonlarından biri kopyalanır
# (lisanslı içerik değil; Engine/Plugins/FX/Niagara). Tercih sırası ad parçasına göre; hiçbiri yoksa C++ Slate zerrelerinde kalır.
NIAGARA_TERCIH = ("hangingparticulates", "hanging", "particulates", "ambient", "dust", "mote", "floating", "fountain")
# Fab paketlerindeki (Niagara Examples Pack, Speed effects, Ultra Dynamic Sky, Ruined Construction ...) toz / zerre / kor sistemleri:
# varsa NS_SmvDust olarak kopyalanır ve menüde varsayılan açık kullanılır (motor şablonu NS_SmvMotes yalnız MenuNiagara=True ile)
TOZ_TERCIH = ("ambientdust", "dustmote", "mote", "particulate", "floatingdust", "ambient", "dust", "ember", "firefl", "spark", "pollen")
TOZ_HARIC = ("rain", "snow", "splash", "lightning", "puddle", "impact", "explos", "muzzle", "blood", "trail", "wind", "fire_big", "smoke")


def _niagara_sistemleri(kok):
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.scan_paths_synchronous([kok], True)
    except Exception as e:  # noqa: BLE001
        log(f"Niagara tarama {kok}: {e}")
    out = []
    try:
        for a in ar.get_assets_by_path(kok, recursive=True):
            sinif = str(a.asset_class_path.asset_name) if hasattr(a, "asset_class_path") else str(a.asset_class)
            if sinif == "NiagaraSystem":
                out.append(str(a.package_name))
    except Exception as e:  # noqa: BLE001
        log(f"Niagara arama {kok}: {e}")
    return out


def _kopyala(kaynak, hedef):
    if eal.does_asset_exist(hedef):
        eal.delete_asset(hedef)
    if not eal.does_directory_exist("/Game/Simumav/FX"):
        eal.make_directory("/Game/Simumav/FX")
    if eal.duplicate_asset(kaynak, hedef):
        eal.save_asset(hedef)
        return True
    return False


def niagara_sablonu():
    # 1) Fab paketlerinden toz/zerre sistemi (kaynak etiketiyle; kaynak değişmedikçe yeniden kopyalanmaz)
    fab = [y for y in _niagara_sistemleri("/Game") if not y.startswith("/Game/Simumav/")]
    log(f"Fab Niagara sistemleri ({len(fab)}): " + ", ".join(fab[:120]))
    secilen = None
    for t in TOZ_TERCIH:
        for yol in fab:
            ad = yol.rsplit("/", 1)[-1].lower()
            if t in ad and not any(h in ad for h in TOZ_HARIC):
                secilen = yol
                break
        if secilen:
            break
    if secilen:
        mevcut = eal.load_asset(NIAGARA_TOZ) if eal.does_asset_exist(NIAGARA_TOZ) else None
        if mevcut and eal.get_metadata_tag(mevcut, "smv_kaynak") == secilen and not YENIDEN:
            log(f"var: {NIAGARA_TOZ} ({secilen})")
        elif _kopyala(secilen, NIAGARA_TOZ):
            k = eal.load_asset(NIAGARA_TOZ)
            if k:
                eal.set_metadata_tag(k, "smv_kaynak", secilen)
                eal.save_loaded_asset(k)
            log(f"Niagara toz sistemi kopyalandı: {secilen} → {NIAGARA_TOZ}")
    else:
        log("Fab paketlerinde uygun toz/zerre Niagara sistemi yok (Niagara Examples Pack önerilir)")
        if eal.does_asset_exist(NIAGARA_TOZ):
            eal.delete_asset(NIAGARA_TOZ)
    # 2) motor şablonu (yedek; varsayılan kapalı)
    if eal.does_asset_exist(NIAGARA_HEDEF) and not YENIDEN:
        log(f"var: {NIAGARA_HEDEF}")
        return
    adaylar = _niagara_sistemleri("/Niagara")
    log(f"Niagara sistem şablonları ({len(adaylar)}): " + ", ".join(adaylar[:60]))
    secilen = None
    for t in NIAGARA_TERCIH:
        for yol in adaylar:
            if t in yol.rsplit("/", 1)[-1].lower():
                secilen = yol
                break
        if secilen:
            break
    if not secilen:
        log("Niagara şablonu bulunamadı — menü Slate zerreleriyle kalır (Niagara Examples Pack önerilir)")
        return
    if _kopyala(secilen, NIAGARA_HEDEF):
        log(f"Niagara şablonu kopyalandı: {secilen} → {NIAGARA_HEDEF}")
    else:
        log(f"Niagara şablonu kopyalanamadı: {secilen}")


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
    for adim in (lambda: pbr_malzemesi("M_SmvPbr", False), lambda: pbr_malzemesi("M_SmvPbr2S", True), pervane_diski, hava_arazi_malzemesi, niagara_sablonu,
                 hyper_arazi_malzemesi, sokak_malzemesi, cephe_malzemesi, cesium_bina_malzemesi, kalabalik_nanite_kapat, pilot_kiti):
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
