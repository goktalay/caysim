# Simumav — Fab FBX/OBJ ürünlerini (VaultCache\FabLibrary) projeye içe aktaran Editor Python betiği (headless).
# Tools/fab/fab-hazirla.ps1 çağırır; lisanslı dosyalar depoya girmez, yalnız runner diskinden okunur.
# Girdi : FAB_IS_LISTESI = iş listesi JSON yolu  [{"kaynak": "...\\drone.fbx", "hedef": "/Game/Fab/Drone", "ad": "SK_FabDrone", "meta": "...\\metadata"}]
# Çıktı : FAB_SONUC      = sonuç JSON yolu       [{"hedef": ..., "ok": bool, "iskelet": bool, "varliklar": [...], "kemikler": [...]}]
# İdempotent: hedef klasör her işte silinip yeniden içe aktarılır; hangi işin gerektiğine fab-hazirla.ps1 kaynak imzasıyla karar verir.
import json
import os
import unreal

eal = unreal.EditorAssetLibrary
at = unreal.AssetToolsHelpers.get_asset_tools()


def log(s):
    unreal.log("[fab_import] " + s)


def uyar(s):
    unreal.log_warning("[fab_import] " + s)


def meta_ozet(yol):
    """Ürün metadata dosyasından kısa özet (yalnız üst düzey anahtarlar ve kısa metin değerleri)."""
    if not yol or not os.path.isfile(yol):
        return
    try:
        with open(yol, "rb") as f:
            ham = f.read()
        # Launcher metadata dosyası UTF-16 (BOM'lu) olabilir
        if ham[:2] in (b"\xff\xfe", b"\xfe\xff"):
            t = ham.decode("utf-16", errors="replace")
        else:
            t = ham.decode("utf-8-sig", errors="replace")
        try:
            d = json.loads(t)
        except Exception:
            log("metadata (json degil, ilk 300 karakter): " + t[:300].replace("\n", " "))
            return
        if isinstance(d, dict):
            for k, v in list(d.items())[:25]:
                if isinstance(v, (str, int, float, bool)):
                    log(f"metadata {k} = {str(v)[:160]}")
                else:
                    log(f"metadata {k} : {type(v).__name__} ({len(v) if hasattr(v, '__len__') else '-'})")
    except Exception as e:  # noqa: BLE001
        uyar(f"metadata okunamadi: {e}")


def simgeler(j):
    """Tuş / kumanda simge paketi (Joystix): kaynak klasördeki PNG'ler ya da .unitypackage içindeki PNG'ler → hedef (UI dokusu,
    mip yok, UserInterface2D sıkıştırma). Alt klasör adları ve dosya adları korunur (oyun adlardan eşler: SmvInputGlyphs)."""
    import tarfile
    import tempfile
    kaynak, hedef = j["kaynak"], j["hedef"]
    pngler = []
    for kok, _, dosyalar in os.walk(kaynak):
        for d in dosyalar:
            tam = os.path.join(kok, d)
            if d.lower().endswith(".png") and d.lower() != "thumbnail.png":
                pngler.append((os.path.relpath(tam, kaynak), tam))
            elif d.lower().endswith(".unitypackage"):
                gecici = tempfile.mkdtemp(prefix="smv_joystix_")
                try:
                    with tarfile.open(tam, "r:gz") as tf:
                        uyeler = {m.name: m for m in tf.getmembers()}
                        for ad, m in uyeler.items():
                            if not ad.endswith("/pathname"):
                                continue
                            yol = tf.extractfile(m).read().decode("utf-8", "replace").splitlines()[0].strip()
                            veri = uyeler.get(ad[: -len("pathname")] + "asset")
                            if not yol.lower().endswith(".png") or veri is None:
                                continue
                            hedef_dosya = os.path.join(gecici, *yol.split("/")[-3:])
                            os.makedirs(os.path.dirname(hedef_dosya), exist_ok=True)
                            with open(hedef_dosya, "wb") as f:
                                f.write(tf.extractfile(veri).read())
                            pngler.append((os.path.relpath(hedef_dosya, gecici), hedef_dosya))
                except Exception as e:  # noqa: BLE001
                    uyar(f"unitypackage okunamadi ({tam}): {e}")
    log(f"simge paketi: {len(pngler)} PNG ({kaynak})")
    if not pngler:
        return {"hedef": hedef, "ok": False, "iskelet": False, "varliklar": [], "kemikler": []}
    if eal.does_directory_exist(hedef):
        eal.delete_directory(hedef)
    gorevler = []
    for rel, tam in pngler:
        alt = "/".join(p for p in os.path.dirname(rel).replace("\\", "/").split("/") if p and p != ".")
        alt = "".join(c if c.isalnum() or c in "_/" else "_" for c in alt)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", tam)
        t.set_editor_property("destination_path", hedef + ("/" + alt if alt else ""))
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", False)
        gorevler.append(t)
    at.import_asset_tasks(gorevler)
    say = 0
    for t in gorevler:
        for yol in list(t.get_editor_property("imported_object_paths") or []):
            a = eal.load_asset(yol.split(".")[0])
            if isinstance(a, unreal.Texture2D):
                try:
                    a.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
                    a.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
                    a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
                except Exception as e:  # noqa: BLE001
                    uyar(f"doku ayari: {e}")
                eal.save_loaded_asset(a)
                say += 1
    log(f"simge paketi: {say} doku → {hedef}")
    return {"hedef": hedef, "ok": say > 0, "iskelet": False, "varliklar": [], "kemikler": []}


def gorev(kaynak, hedef, ad, iskelet):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", kaynak)
    t.set_editor_property("destination_path", hedef)
    t.set_editor_property("destination_name", ad)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    if kaynak.lower().endswith(".fbx"):
        o = unreal.FbxImportUI()
        o.set_editor_property("import_mesh", True)
        o.set_editor_property("import_as_skeletal", iskelet)
        o.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH if iskelet else unreal.FBXImportType.FBXIT_STATIC_MESH)
        o.set_editor_property("import_materials", True)
        o.set_editor_property("import_textures", True)
        o.set_editor_property("import_animations", False)
        o.set_editor_property("create_physics_asset", False)
        if not iskelet:
            try:
                o.static_mesh_import_data.set_editor_property("combine_meshes", True)
            except Exception:  # noqa: BLE001
                pass
        t.set_editor_property("options", o)
    return t


def varliklar(hedef):
    out = []
    for p in eal.list_assets(hedef, recursive=True, include_folder=False):
        d = eal.find_asset_data(p)
        try:
            cls = str(d.asset_class_path.asset_name)
        except Exception:  # noqa: BLE001
            cls = str(d.get_editor_property("asset_class"))
        out.append((p.split(".")[0], cls))
    return out


def kemikler(sk):
    """Kemik adları: geçici SkeletalMeshActor üzerinden (editör dünyası)."""
    adlar = []
    try:
        eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
        a = eas.spawn_actor_from_object(sk, unreal.Vector(0, 0, -100000))
        if a:
            c = a.get_editor_property("skeletal_mesh_component")
            for i in range(c.get_num_bones()):
                adlar.append(str(c.get_bone_name(i)))
            eas.destroy_actor(a)
    except Exception as e:  # noqa: BLE001
        uyar(f"kemik listesi alinamadi: {e}")
    return adlar


def bir_is(j):
    kaynak, hedef, ad = j["kaynak"], j["hedef"], j.get("ad", "SK_FabAsset")
    log(f"=== {kaynak} -> {hedef}/{ad}")
    meta_ozet(j.get("meta"))
    if not os.path.isfile(kaynak):
        uyar(f"kaynak yok: {kaynak}")
        return {"hedef": hedef, "ok": False}
    if eal.does_directory_exist(hedef):
        eal.delete_directory(hedef)
    if j.get("statik"):
        # sahne dekoru: yalnız statik ağ (birleştirilmiş), iskelet/animasyon yok
        denemeler = [(False, None)]
    else:
        denemeler = [(True, None)]
    if kaynak.lower().endswith(".fbx") and not j.get("statik"):
        # Interchange FBX çevirmeni FbxImportUI seçeneklerini yok sayarsa eski FBX içe aktarıcıyla bir daha dene
        denemeler.append((True, "Interchange.FeatureFlags.Import.FBX 0"))
        denemeler.append((False, None))
    sonuc = {"hedef": hedef, "ok": False, "iskelet": False, "varliklar": [], "kemikler": []}
    for iskelet, cvar in denemeler:
        if cvar:
            unreal.SystemLibrary.execute_console_command(None, cvar)
            log(f"konsol: {cvar}")
        t = gorev(kaynak, hedef, ad if iskelet else ad.replace("SK_", "SM_"), iskelet)
        at.import_asset_tasks([t])
        v = varliklar(hedef)
        for p, c in v:
            log(f"varlik: {c:<22} {p}")
        sks = [p for p, c in v if c == "SkeletalMesh"]
        sms = [p for p, c in v if c == "StaticMesh"]
        if sks:
            sk = unreal.load_asset(sks[0])
            sonuc.update(ok=True, iskelet=True, varliklar=[p for p, _ in v])
            try:
                b = sk.get_bounds()
                e = b.box_extent
                log(f"sinir kutusu (cm): {2 * e.x:.1f} x {2 * e.y:.1f} x {2 * e.z:.1f}, merkez {b.origin}")
            except Exception as ex:  # noqa: BLE001
                uyar(f"sinir alinamadi: {ex}")
            try:
                for i, m in enumerate(sk.get_editor_property("materials")):
                    mi = m.get_editor_property("material_interface")
                    log(f"malzeme {i}: {m.get_editor_property('material_slot_name')} -> {mi.get_path_name() if mi else '-'}")
            except Exception as ex:  # noqa: BLE001
                uyar(f"malzemeler okunamadi: {ex}")
            dokular = [p for p, c in v if c.startswith("Texture")]
            log(f"doku sayisi: {len(dokular)}" + ("" if dokular else " (FBX'te gomulu doku yok; malzemeler duz renk)"))
            k = kemikler(sk)
            sonuc["kemikler"] = k
            log(f"kemikler ({len(k)}): {', '.join(k)}")
            pervane = [x for x in k if any(s in x.lower() for s in ("prop", "rotor", "blade", "pervane", "fan"))]
            log(f"pervane kemikleri: {', '.join(pervane) if pervane else '(bulunamadi - pervaneler donmez)'}")
            break
        if sms and not iskelet:
            sonuc.update(ok=True, iskelet=False, varliklar=[p for p, _ in v])
            try:
                sm = unreal.load_asset(sms[0])
                e = sm.get_bounds().box_extent
                log(f"statik ag: {sms[0]} ({len(sms)} ag), sinir (cm): {2 * e.x:.0f} x {2 * e.y:.0f} x {2 * e.z:.0f}")
            except Exception as ex:  # noqa: BLE001
                uyar(f"sinir alinamadi: {ex}")
            log(f"doku sayisi: {len([p for p, c in v if c.startswith('Texture')])}")
            if not j.get("statik"):
                uyar("yalniz statik ag olustu (iskelet yok) - govde statik, pervaneler donmez")
            break
        uyar(f"deneme basarisiz (iskelet={iskelet}, cvar={cvar}); {len(v)} varlik")
        if eal.does_directory_exist(hedef):
            eal.delete_directory(hedef)
    eal.save_directory(hedef, only_if_is_dirty=True, recursive=True)
    return sonuc


def uds_incele():
    """Ultra Dynamic Sky: oyunun adla bağladığı değişkenlerin varlığı ve hava ön ayarları (yalnız günlük)."""
    sonuc = {"hedef": "/Game/UltraDynamicSky", "ok": True}
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    varliklar = ar.get_assets_by_path("/Game/UltraDynamicSky", recursive=True)
    for ad, adaylar in (("Ultra_Dynamic_Sky", ["Time of Day", "Animate Time of Day", "Cloud Coverage", "Fog"]),
                        ("Ultra_Dynamic_Weather", ["Weather", "Random Weather Variation"])):
        bp = [a for a in varliklar if str(a.asset_name) == ad]
        if not bp:
            uyar(f"UDS: {ad} bulunamadi")
            continue
        yol = str(bp[0].package_name)
        log(f"UDS: {ad} -> {yol}")
        try:
            cls = unreal.load_object(None, f"{yol}.{ad}_C")
            cdo = unreal.get_default_object(cls)
            for d in adaylar:
                bulundu = None
                for aday in (d, d.replace(" ", "_"), d.replace(" ", ""), d.lower().replace(" ", "_")):
                    try:
                        bulundu = (aday, cdo.get_editor_property(aday))
                        break
                    except Exception:  # noqa: BLE001
                        pass
                log(f"UDS:   {d}: " + (f"VAR ({bulundu[0]} = {str(bulundu[1])[:80]})" if bulundu else "bulunamadi"))
        except Exception as e:  # noqa: BLE001
            uyar(f"UDS: {ad} incelenemedi: {e}")
    hava = sorted({str(a.asset_name) for a in varliklar if "Weather" in str(a.asset_class_path.asset_name) and "Ultra_Dynamic" not in str(a.asset_name)})
    log(f"UDS hava on ayarlari ({len(hava)}): {', '.join(hava[:40])}")
    return sonuc


# Uçak/VTOL motor sesleri (Aircraft Engine Sounds): seçilen döngülerin SoundWave'i /Game/Fab/Sounds/SC_Engine_<tür> olarak
# kopyalanır ve döngü açılır (oyun bu yoldan yükler; /Game/Fab her zaman pişirilir, paketin geri kalanı pakete girmez).
# Seçim adayları sırayla denenir; oyunda DefaultGame.ini EngineSound<tür>=/Game/... ile başka bir ses seçilebilir.
SES_SECIMI = {
    "Prop": ["Cue_engine05_loop_Cue", "Cue_Engine05Loop01_Cue", "Cue_engine01_loop_Cue", "Cue_Engine01Loop-01_Cue", "Cue_EngineLoop01_Cue"],
    "Pusher": ["Cue_engine03_loop_Cue", "Cue_Engine03Loop_Cue", "Cue_engine02_loop_Cue", "Cue_Engine02Loop-01_Cue"],
    "Jet": ["Cue_JetTurbine03_loop_Cue", "Cue_JetTurbine01_loop_Cue", "Cue_JetTurbine02_loop_Cue"],
}


def ses_kopyala(j):
    kok = j.get("kok", "/Game/cplomedia_AircraftEngineSounds")
    hedef_kok = j.get("hedef", "/Game/Fab/Sounds")
    sonuc = {"hedef": hedef_kok, "ok": False}
    ar = unreal.AssetRegistryHelpers.get_asset_registry()
    try:
        ar.scan_paths_synchronous([kok], True)
    except Exception:  # noqa: BLE001
        pass
    v = ar.get_assets_by_path(kok, recursive=True)
    cues = {str(a.asset_name): str(a.package_name) for a in v if "SoundCue" in str(a.asset_class_path.asset_name)}
    dalgalar = {str(a.package_name) for a in v if "SoundWave" in str(a.asset_class_path.asset_name)}
    log(f"motor sesleri: {len(cues)} cue, {len(dalgalar)} dalga")
    log("cue adlari: " + ", ".join(sorted(cues)))
    secenek = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
    tamam = 0
    for tur, adaylar in SES_SECIMI.items():
        hedef = f"{hedef_kok}/SC_Engine_{tur}"
        kaynak = next((cues[a] for a in adaylar if a in cues), None)
        if not kaynak:
            jet = tur == "Jet"
            for ad in sorted(cues):
                n = ad.lower()
                if "loop" in n and "inside" not in n and "damaged" not in n and (("jet" in n) == jet):
                    kaynak = cues[ad]
                    break
        if not kaynak:
            uyar(f"motor sesi bulunamadi: {tur}")
            continue
        if eal.does_asset_exist(hedef):
            eal.delete_asset(hedef)
        # cue'nun kullandigi dalga (dongu acilabilsin diye dalga kopyalanir); bulunamazsa cue kopyalanir
        dalga = None
        try:
            for d in ar.get_dependencies(kaynak, secenek) or []:
                if str(d) in dalgalar:
                    dalga = str(d)
                    break
        except Exception as e:  # noqa: BLE001
            uyar(f"bagimlilik okunamadi ({kaynak}): {e}")
        kopya = None
        if dalga:
            kopya = eal.duplicate_asset(dalga, hedef)
            if kopya:
                try:
                    kopya.set_editor_property("looping", True)
                except Exception as e:  # noqa: BLE001
                    uyar(f"dongu acilamadi: {e}")
        if not kopya:
            kopya = eal.duplicate_asset(kaynak, hedef)
        if kopya:
            eal.save_asset(hedef, only_if_is_dirty=False)
            log(f"motor sesi {tur}: {dalga or kaynak} -> {hedef}")
            tamam += 1
        else:
            uyar(f"motor sesi kopyalanamadi: {kaynak}")
    sonuc["ok"] = tamam > 0
    return sonuc


def main():
    yol = os.environ.get("FAB_IS_LISTESI")
    if not yol or not os.path.isfile(yol):
        log("is listesi yok; yapilacak bir sey yok")
        return
    with open(yol, encoding="utf-8-sig") as f:
        isler = json.load(f)
    if isinstance(isler, dict):
        isler = [isler]
    sonuclar = []
    for j in isler:
        try:
            tur = j.get("tur")
            sonuclar.append(uds_incele() if tur == "uds" else ses_kopyala(j) if tur == "ses" else simgeler(j) if tur == "simge" else bir_is(j))
        except Exception as e:  # noqa: BLE001
            unreal.log_error(f"[fab_import] hata: {j.get('kaynak')}: {e}")
            sonuclar.append({"hedef": j.get("hedef"), "ok": False})
    out = os.environ.get("FAB_SONUC")
    if out:
        with open(out, "w", encoding="utf-8") as f:
            json.dump(sonuclar, f, ensure_ascii=False, indent=1)
    log("tamam")


try:
    main()
finally:
    if os.environ.get("SIMUMAV_EDITOR_ACIK") != "1":
        unreal.SystemLibrary.quit_editor()
