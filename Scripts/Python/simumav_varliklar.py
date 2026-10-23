# Simumav — Editor'da HEADLESS çalışan varlık üretici (UE Python). Content/ altındaki ikili varlıklar elle değil bu betikle üretilir:
#   /Game/Maps/SimumavMain                 boş harita (dünya, ışık, gök, dron, arayüz çalışma anında C++'tan kurulur)
#   /Game/Simumav/Materials/M_SmvTerrain   prosedürel arazi: köşe rengi × doku gürültüsü (Color parametresi)
#   /Game/Simumav/Materials/M_SmvGlow      halka/kapı: Color × Glow yayılım (emissive)
# Çalıştırma (Windows, şirket bilgisayarı / CI):
#   UnrealEditor-Cmd.exe <yol>\Simumav.uproject -ExecutePythonScript="<yol>\Scripts\Python\simumav_varliklar.py" -unattended -nop4 -nosplash -NullRHI -stdout
# Betik idempotenttir: varlık varsa yeniden oluşturmaz (SIMUMAV_YENIDEN=1 ortam değişkeniyle zorla).
import os
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
    harita()
    log("tamam")


try:
    main()
finally:
    # -ExecutePythonScript editörü açık bırakır; CI'da kapanmalı
    if os.environ.get("SIMUMAV_EDITOR_ACIK") != "1":
        unreal.SystemLibrary.quit_editor()
