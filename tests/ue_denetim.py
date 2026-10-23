#!/usr/bin/env python3
"""Unreal olmadan kaba C++ / UHT denetimi (derlemenin yerini TUTMAZ; şirket bilgisayarındaki derlemeden önce bariz hataları yakalar).

Denetimler:
  1. UCLASS/USTRUCT/UENUM/UINTERFACE içeren başlık: `<Ad>.generated.h` SON #include, dosya adıyla aynı; GENERATED_BODY var.
  2. Public başlıktaki UCLASS/USTRUCT: modül API makrosu (CAYSIM_API / SIMUMAV_API).
  3. Proje içi #include "..." çözümlenir (Source altında); motor başlıkları bilinen listede olmalı ve sağlayan modül Build.cs'te bulunmalı.
  4. Saf çekirdek (Private/Core|Mavlink|Crypto) Unreal başlığı içermez.
  5. Ayraç / parantez dengesi (yorum ve dizgeler ayıklanarak).
  6. AddDynamic ile bağlanan üye işlevler UFUNCTION olmalı.
  7–12. ue_denetim_ek.py: gölgeleme (C4456/C4457/C4458), [=] lambda, unity derleme ad çakışması, kaldırılmış API'ler,
        karışık float/double FMath (uyarı), Build.cs eklenti modülleri ↔ .uproject Plugins.
Kullanım: python3 tests/ue_denetim.py   (çıkış 0 = temiz)
"""
import os
import re
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ue_denetim_ek as EK  # noqa: E402

KOK = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
SRC = os.path.join(KOK, 'Source')

# motor başlığı → sağlayan modül (yalnız projede kullanılanlar; yeni başlık eklenince buraya da eklenir — bilinçli gözden geçirme)
MOTOR = {
    'CoreMinimal.h': 'Core', 'Modules/ModuleManager.h': 'Core', 'Containers/Ticker.h': 'Core', 'Misc/AES.h': 'Core', 'Misc/FileHelper.h': 'Core',
    'Misc/Paths.h': 'Core', 'HAL/FileManager.h': 'Core', 'HAL/PlatformMisc.h': 'Core', 'HAL/PlatformProcess.h': 'Core', 'HAL/PlatformTime.h': 'Core', 'Components/InputComponent.h': 'Engine',
    'Rendering/SlateRenderer.h': 'SlateCore', 'Misc/ConfigCacheIni.h': 'Core',
    'Styling/SlateTypes.h': 'SlateCore', 'Brushes/SlateRoundedBoxBrush.h': 'SlateCore', 'Widgets/SLeafWidget.h': 'SlateCore', 'Layout/Clipping.h': 'SlateCore',
    'Widgets/Images/SImage.h': 'Slate',
    'Misc/DateTime.h': 'Core', 'Misc/App.h': 'Core', 'Internationalization/Culture.h': 'Core', 'Internationalization/Internationalization.h': 'Core',
    'GenericPlatform/GenericPlatformHttp.h': 'Core',
    'Misc/Base64.h': 'Core', 'Misc/SecureHash.h': 'Core', 'Dom/JsonValue.h': 'Json', 'zlib.h': 'zlib',
    'Engine/Texture2D.h': 'Engine', 'ImageUtils.h': 'Engine', 'Engine/ExponentialHeightFog.h': 'Engine', 'Engine/StaticMeshActor.h': 'Engine',
    'Components/DirectionalLightComponent.h': 'Engine', 'Components/SkyLightComponent.h': 'Engine', 'Components/ExponentialHeightFogComponent.h': 'Engine',
    'Components/SkyAtmosphereComponent.h': 'Engine', 'Components/VolumetricCloudComponent.h': 'Engine', 'CollisionQueryParams.h': 'Engine',
    'Fonts/FontMeasure.h': 'SlateCore', 'Framework/Application/SlateApplication.h': 'Slate',
    'Windows/AllowWindowsPlatformTypes.h': 'Core', 'Windows/HideWindowsPlatformTypes.h': 'Core', 'Windows/WindowsHWrapper.h': 'Core',
    'UObject/Object.h': 'CoreUObject', 'UObject/ConstructorHelpers.h': 'CoreUObject', 'UObject/Interface.h': 'CoreUObject',
    'Dom/JsonObject.h': 'Json', 'Serialization/JsonReader.h': 'Json', 'Serialization/JsonSerializer.h': 'Json', 'Serialization/JsonWriter.h': 'Json',
    'Policies/CondensedJsonPrintPolicy.h': 'Json', 'Policies/PrettyJsonPrintPolicy.h': 'Json',
    'HttpModule.h': 'HTTP', 'Interfaces/IHttpRequest.h': 'HTTP', 'Interfaces/IHttpResponse.h': 'HTTP',
    'IWebSocket.h': 'WebSockets', 'WebSocketsModule.h': 'WebSockets',
    'Sockets.h': 'Sockets', 'SocketSubsystem.h': 'Sockets', 'IPAddress.h': 'Sockets',
    'Engine/GameInstance.h': 'Engine', 'Engine/Engine.h': 'Engine', 'Engine/World.h': 'Engine', 'Engine/GameViewportClient.h': 'Engine',
    'Engine/LocalPlayer.h': 'Engine', 'Engine/StaticMesh.h': 'Engine', 'Engine/TextureRenderTarget2D.h': 'Engine',
    'GameFramework/Actor.h': 'Engine', 'GameFramework/Pawn.h': 'Engine', 'GameFramework/GameModeBase.h': 'Engine', 'GameFramework/PlayerController.h': 'Engine',
    'GameFramework/GameUserSettings.h': 'Engine', 'GameFramework/SaveGame.h': 'Engine', 'GameFramework/SpringArmComponent.h': 'Engine',
    'Camera/CameraComponent.h': 'Engine', 'Components/SceneCaptureComponent2D.h': 'Engine', 'Components/SceneComponent.h': 'Engine',
    'Components/StaticMeshComponent.h': 'Engine', 'Components/InstancedStaticMeshComponent.h': 'Engine', 'Components/PointLightComponent.h': 'Engine',
    'Materials/MaterialInstanceDynamic.h': 'Engine', 'Materials/MaterialInterface.h': 'Engine', 'Kismet/GameplayStatics.h': 'Engine',
    'Kismet/KismetSystemLibrary.h': 'Engine', 'Kismet/KismetMathLibrary.h': 'Engine', 'Subsystems/GameInstanceSubsystem.h': 'Engine',
    'Engine/DirectionalLight.h': 'Engine', 'Engine/SkyLight.h': 'Engine',
    'Components/SynthComponent.h': 'AudioMixer',
    'ProceduralMeshComponent.h': 'ProceduralMeshComponent',
    'NiagaraComponent.h': 'Niagara', 'NiagaraSystem.h': 'Niagara',
    'EnhancedInputComponent.h': 'EnhancedInput', 'EnhancedInputSubsystems.h': 'EnhancedInput', 'InputMappingContext.h': 'EnhancedInput',
    'InputAction.h': 'EnhancedInput', 'InputModifiers.h': 'EnhancedInput',
    'Blueprint/UserWidget.h': 'UMG',
    'Widgets/SCompoundWidget.h': 'Slate', 'Widgets/SBoxPanel.h': 'Slate', 'Widgets/SOverlay.h': 'Slate', 'Widgets/Layout/SBox.h': 'Slate',
    'Widgets/Layout/SBorder.h': 'Slate', 'Widgets/Layout/SScrollBox.h': 'Slate', 'Widgets/Layout/SSpacer.h': 'Slate', 'Widgets/Layout/SUniformGridPanel.h': 'Slate',
    'Widgets/Input/SButton.h': 'Slate', 'Widgets/Input/SEditableTextBox.h': 'Slate', 'Widgets/Input/SMultiLineEditableTextBox.h': 'Slate',
    'Widgets/Input/SCheckBox.h': 'Slate', 'Widgets/Input/SComboBox.h': 'Slate', 'Widgets/Input/SSpinBox.h': 'Slate', 'Widgets/Text/STextBlock.h': 'Slate',
    'Widgets/Colors/SColorBlock.h': 'Slate', 'Widgets/SWeakWidget.h': 'Slate',
    'Styling/CoreStyle.h': 'SlateCore', 'Styling/SlateBrush.h': 'SlateCore', 'Rendering/DrawElements.h': 'SlateCore', 'Fonts/SlateFontInfo.h': 'SlateCore',
    'OnlineSubsystem.h': 'OnlineSubsystem', 'OnlineSubsystemNames.h': 'OnlineSubsystem', 'Interfaces/OnlineIdentityInterface.h': 'OnlineSubsystem',
    'Interfaces/OnlineAchievementsInterface.h': 'OnlineSubsystem', 'Interfaces/OnlineExternalUIInterface.h': 'OnlineSubsystem',
}
MOTOR_ONEKLER = ('Cesium', 'glTFRuntime')   # koşullu (CAYSIM_WITH_*) eklenti başlıkları


def yorumsuz(t):
    """Yorumları ve dizge/karakter sabitlerini sırayla ayıklar (// dizge içinde olabilir)."""
    out, i, n = [], 0, len(t)
    while i < n:
        c = t[i]
        if t.startswith('//', i):
            j = t.find('\n', i)
            i = n if j < 0 else j
        elif t.startswith('/*', i):
            j = t.find('*/', i + 2)
            i = n if j < 0 else j + 2
        elif c == 'R' and t.startswith('R"(', i):
            j = t.find(')"', i + 3)
            out.append('""')
            i = n if j < 0 else j + 2
        elif c in '"\'':
            j = i + 1
            while j < n and t[j] != c:
                j += 2 if t[j] == '\\' else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return ''.join(out)


def build_moduller(modul_dizini):
    bc = [f for f in os.listdir(modul_dizini) if f.endswith('.Build.cs')]
    if not bc:
        return set()
    t = open(os.path.join(modul_dizini, bc[0]), encoding='utf-8').read()
    mods = set(re.findall(r'"([A-Za-z0-9_]+)"', t))
    return mods | {'Core'}


def main():
    hatalar, uyarilar = [], []
    proje_basliklar = {}
    for kok, _, dosyalar in os.walk(SRC):
        for f in dosyalar:
            if f.endswith('.h'):
                proje_basliklar.setdefault(f, []).append(os.path.join(kok, f))
    # ek denetimler için proje sınıfları (veri üyeleri + üst sınıflar)
    basliklar = [(p, yorumsuz(open(p, encoding='utf-8').read())) for ps in proje_basliklar.values() for p in ps]
    siniflar = EK.siniflari_topla(basliklar)
    hatalar.extend(EK.oz_sinama(yorumsuz))
    hatalar.extend(EK.eklenti_tutarlilik(KOK))
    for modul in [d for d in os.listdir(SRC) if os.path.isdir(os.path.join(SRC, d))]:
        mdir = os.path.join(SRC, modul)
        dosya_adlari = {}   # unity: ad → [dosyalar]
        dosya_tanimlari = {}
        api = modul.upper() + '_API'
        mods = build_moduller(mdir)
        for kok, _, dosyalar in os.walk(mdir):
            for f in dosyalar:
                if not f.endswith(('.h', '.cpp')):
                    continue
                yol = os.path.join(kok, f)
                goreli = os.path.relpath(yol, KOK)
                ham = open(yol, encoding='utf-8').read()
                t = yorumsuz(ham)
                saf = any(p in yol for p in (os.sep + 'Core' + os.sep, os.sep + 'Mavlink' + os.sep, os.sep + 'Crypto' + os.sep))
                incs = re.findall(r'^\s*#\s*include\s+"([^"]+)"', ham, flags=re.M)
                for inc in incs:
                    ad = os.path.basename(inc)
                    if saf:
                        if inc.endswith('.generated.h') or inc in MOTOR:
                            hatalar.append(f'{goreli}: saf çekirdekte Unreal başlığı: {inc}')
                        elif ad not in proje_basliklar:
                            hatalar.append(f'{goreli}: çözümlenemeyen #include "{inc}"')
                        continue
                    if inc.endswith('.generated.h') or ad in proje_basliklar:
                        continue
                    if inc.startswith(MOTOR_ONEKLER) or ad.startswith(MOTOR_ONEKLER):
                        continue
                    if inc not in MOTOR:
                        hatalar.append(f'{goreli}: bilinmeyen motor başlığı "{inc}" (MOTOR listesine modülüyle ekleyin)')
                    elif MOTOR[inc] not in mods and not (modul == 'Simumav' and MOTOR[inc] in build_moduller(os.path.join(SRC, 'CaySim')) and MOTOR[inc] in ('Engine',)):
                        hatalar.append(f'{goreli}: "{inc}" {MOTOR[inc]} modülünü ister; {modul}.Build.cs listesinde yok')
                if f.endswith('.h'):
                    yansitma = re.search(r'^\s*(UCLASS|USTRUCT|UENUM|UINTERFACE)\s*\(', t, flags=re.M)
                    if yansitma:
                        gen = f[:-2] + '.generated.h'
                        if not incs or incs[-1] != gen:
                            hatalar.append(f'{goreli}: son #include "{gen}" olmalı (bulunan: {incs[-1] if incs else "-"})')
                        n_tip = len(re.findall(r'^\s*(UCLASS|USTRUCT|UINTERFACE)\s*\(', t, flags=re.M))
                        n_gb = len(re.findall(r'\bGENERATED_(BODY|UCLASS_BODY|USTRUCT_BODY|UINTERFACE_BODY)\s*\(', t))
                        if n_gb < n_tip:
                            hatalar.append(f'{goreli}: {n_tip} yansıtılan tip, {n_gb} GENERATED_BODY')
                        if os.sep + 'Public' + os.sep in yol:
                            for m in re.finditer(r'^\s*(UCLASS|USTRUCT)\s*\([^)]*\)\s*\n\s*(class|struct)\s+(\w+)\s+(\w+)?', t, flags=re.M):
                                if m.group(3) != api and m.group(2) == 'class':
                                    uyarilar.append(f'{goreli}: {m.group(2)} {m.group(3)} {m.group(4) or ""} — {api} makrosu yok')
                # 7–11: gölgeleme, [=] lambda, kaldırılmış API, karışık FMath
                hatalar.extend(EK.golgeleme(goreli, t, siniflar))
                if not saf:
                    hatalar.extend(EK.lambda_esittir(goreli, t))
                    hatalar.extend(EK.kaldirilmis(goreli, t))
                    uyarilar.extend(EK.fmath_karisik(goreli, t))
                if f.endswith('.cpp'):
                    adlar, tanimlar = EK.dosya_kapsami_adlar(t if not saf else ham)
                    for a in adlar:
                        dosya_adlari.setdefault(a, []).append(goreli)
                    for a in tanimlar:
                        dosya_tanimlari.setdefault(a, []).append(goreli)
                # ayraç dengesi
                for a, b in (('{', '}'), ('(', ')'), ('[', ']')):
                    if t.count(a) != t.count(b):
                        hatalar.append(f'{goreli}: "{a}{b}" dengesiz ({t.count(a)} / {t.count(b)})')
                # AddDynamic → UFUNCTION
                for m in re.finditer(r'AddDynamic\(\s*this\s*,\s*&(\w+)::(\w+)\s*\)', t):
                    sinif, isl = m.group(1), m.group(2)
                    bas = [p for p in proje_basliklar.get(f[:-4] + '.h', []) + [p for ps in proje_basliklar.values() for p in ps]
                           if re.search(r'class\s+\w+_API\s+' + sinif + r'\b', open(p, encoding='utf-8').read())]
                    if bas:
                        ht = open(bas[0], encoding='utf-8').read()
                        if not re.search(r'UFUNCTION\([^)]*\)\s*(virtual\s+)?void\s+' + isl + r'\s*\(', ht):
                            hatalar.append(f'{goreli}: AddDynamic {sinif}::{isl} UFUNCTION değil')
        # 9: unity derlemesinde aynı çeviri birimine düşebilecek dosya-kapsamı adlar
        for a, ds in sorted(dosya_adlari.items()):
            if len(ds) > 1:
                hatalar.append(f'{modul}: dosya-kapsamı "{a}" birden çok .cpp\'de tanımlı (unity derlemede yeniden tanım): {", ".join(ds)}')
        for a, ds in sorted(dosya_tanimlari.items()):
            if len(ds) > 1:
                hatalar.append(f'{modul}: #define {a} birden çok .cpp\'de (#undef yok; unity derlemede yeniden tanım uyarısı): {", ".join(ds)}')
    for u in uyarilar:
        print('UYARI', u)
    for h in hatalar:
        print('HATA ', h)
    print(f'ue_denetim: {len(hatalar)} hata, {len(uyarilar)} uyarı')
    return 1 if hatalar else 0


if __name__ == '__main__':
    sys.exit(main())
