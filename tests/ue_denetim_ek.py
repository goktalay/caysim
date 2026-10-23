"""ue_denetim.py ek denetimleri (Unreal'sız, sezgisel; derlemenin yerini TUTMAZ).

  7. Gölgeleme (MSVC C4456/C4457/C4458 — UE'de HATA): üye işlev gövdesindeki yerel değişken / parametre / lambda parametresi
     sınıf (ve projedeki üst sınıf + bilinen motor üst sınıfı) veri üyesiyle aynı adlı; ya da iç kapsamda dış kapsamdaki
     yerel/parametreyle aynı adlı bildirim.
  8. [=] lambda üye işlevde (C++20: örtük `this` yakalama kullanımdan kalktı) → [this, ...] açık yakalama.
  9. Unity derleme çakışması: aynı modülde birden çok .cpp'de aynı adlı dosya-kapsamı (static / anonim namespace) işlev/değişken
     ya da #undef edilmemiş #define.
 10. Kaldırılmış / kullanımdan kalkmış API: FTicker, FEditorStyle, TAtomic, TCHAR_TO_UTF8 sonucunu işaretçide saklama, GetWorld()->SpawnActor
     ham sınıf adı olmadan, ...
 11. Karışık float/double FMath::Clamp/Max/Min (UE5 LWC: FVector/FRotator bileşenleri double) — UYARI (tip bilinmiyor).
 12. Build.cs'teki eklenti modülleri .uproject Plugins listesinde olmalı.
"""
import json
import os
import re

# Bilinen motor üst sınıflarının (korumalı/açık) veri üyeleri — yerel ad bunlarla çakışırsa C4458.
MOTOR_UYELER = {
    'UObject': set(),
    'AActor': {'RootComponent', 'Tags', 'InputComponent', 'PrimaryActorTick', 'Owner', 'Instigator', 'Layers',
               'InitialLifeSpan', 'CustomTimeDilation', 'NetUpdateFrequency', 'bHidden', 'bReplicates', 'Children'},
    'APawn': {'Controller', 'AIControllerClass', 'AutoPossessPlayer', 'AutoPossessAI', 'BaseEyeHeight', 'LastControlInputVector',
              'ControlInputVector', 'PreviousController', 'bUseControllerRotationYaw', 'bUseControllerRotationPitch', 'bUseControllerRotationRoll'},
    'AGameModeBase': {'GameSession', 'GameState', 'DefaultPawnClass', 'HUDClass', 'PlayerControllerClass', 'PlayerStateClass',
                      'SpectatorClass', 'GameStateClass', 'OptionsString', 'ServerStatReplicator'},
    'AGameMode': set(),
    'APlayerController': {'Player', 'PlayerInput', 'PlayerCameraManager', 'MyHUD', 'AcknowledgedPawn', 'bShowMouseCursor'},
    'UActorComponent': {'PrimaryComponentTick', 'ComponentTags', 'bAutoActivate'},
    'USceneComponent': {'AttachParent', 'AttachChildren', 'RelativeLocation', 'RelativeRotation', 'RelativeScale3D', 'Mobility', 'ComponentVelocity'},
    'UAudioComponent': {'Sound', 'VolumeMultiplier', 'PitchMultiplier', 'bAllowSpatialization', 'AttenuationSettings'},
    'USynthComponent': {'NumChannels', 'SoundClass', 'SoundSubmix', 'bIsUISound', 'AudioComponent', 'Synth', 'bAllowSpatialization',
                        'AttenuationSettings', 'ConcurrencySettings', 'bEnableBusSends', 'bEnableBaseSubmix', 'bEnableSubmixSends',
                        'BusSends', 'PreEffectBusSends', 'SubmixSends', 'SourceEffectChain', 'Priority', 'bOutputToBusOnly',
                        'VolumeMultiplier', 'PitchMultiplier', 'bIsSynthPlaying', 'bIsInitialized', 'EnvelopeFollowerAttackTime',
                        'EnvelopeFollowerReleaseTime'},
    'UGameInstance': {'LocalPlayers', 'WorldContext', 'OnlineSession', 'ReferencedObjects', 'TimerManager', 'LatentActionManager'},
    'UGameInstanceSubsystem': set(),
    'USubsystem': set(),
    'USaveGame': set(),
    'UUserWidget': {'ColorAndOpacity', 'ForegroundColor', 'Padding', 'Priority', 'bIsFocusable', 'WidgetTree'},
    'UWidget': {'Slot', 'Visibility', 'RenderOpacity', 'ToolTipText', 'Cursor', 'Clipping'},
    'SWidget': {'Cursor', 'EnabledState', 'Visibility', 'RenderOpacity', 'RenderTransform', 'RenderTransformPivot', 'Clipping',
                'ToolTip', 'Tag', 'bIsHovered'},
    'SCompoundWidget': {'ChildSlot', 'ContentScale', 'ColorAndOpacity', 'ForegroundColor'},
    'SLeafWidget': set(),
    'FRunnable': set(),
    'FTickableGameObject': set(),
}
# üst sınıf zinciri (motor)
MOTOR_UST = {
    'APawn': 'AActor', 'ACharacter': 'APawn', 'AGameModeBase': 'AActor', 'AGameMode': 'AGameModeBase', 'APlayerController': 'AActor',
    'AStaticMeshActor': 'AActor', 'AInfo': 'AActor', 'AHUD': 'AActor',
    'USceneComponent': 'UActorComponent', 'UPrimitiveComponent': 'USceneComponent', 'UAudioComponent': 'USceneComponent',
    'USynthComponent': 'USceneComponent', 'UGameInstanceSubsystem': 'USubsystem', 'UUserWidget': 'UWidget',
    'SCompoundWidget': 'SWidget', 'SLeafWidget': 'SWidget', 'SPanel': 'SWidget',
}

ANAHTAR = {
    'return', 'delete', 'new', 'else', 'case', 'goto', 'throw', 'using', 'typedef', 'if', 'while', 'for', 'switch', 'do',
    'break', 'continue', 'default', 'sizeof', 'co_return', 'co_await', 'co_yield', 'namespace', 'template', 'typename',
    'public', 'private', 'protected', 'friend', 'operator', 'class', 'struct', 'enum', 'union', 'decltype', 'alignof',
    'static_assert', 'true', 'false', 'nullptr', 'this', 'not', 'and', 'or', 'catch', 'try', 'requires', 'concept',
    'check', 'ensure', 'verify', 'checkf', 'ensureMsgf', 'UE_LOG', 'TEXT', 'LOCTEXT', 'NSLOCTEXT', 'SLATE_BEGIN_ARGS',
    'SNew', 'SAssignNew', 'Super', 'GENERATED_BODY', 'UPROPERTY', 'UFUNCTION', 'UCLASS', 'USTRUCT', 'UENUM',
}
NITELEYICI = {'const', 'static', 'constexpr', 'volatile', 'mutable', 'inline', 'thread_local', 'register', 'unsigned', 'signed',
              'long', 'short', 'struct', 'class', 'enum', 'typename', 'extern', 'consteval', 'constinit'}

TOKEN_RE = re.compile(r'[A-Za-z_]\w*|::|->|\d[\w.]*|\S')


def tokenle(t):
    """(belirteç, satır) listesi; önişlemci satırları atlanır (yorumsuz metin beklenir)."""
    out = []
    for no, satir in enumerate(t.split('\n'), 1):
        if satir.lstrip().startswith('#'):
            continue
        for m in TOKEN_RE.finditer(satir):
            out.append((m.group(0), no))
    return out


def eslesen(tok, i, ac, kapa):
    """tok[i] == ac; eşleşen kapanışın indeksi."""
    d = 0
    for j in range(i, len(tok)):
        if tok[j][0] == ac:
            d += 1
        elif tok[j][0] == kapa:
            d -= 1
            if d == 0:
                return j
    return len(tok) - 1


def sablon_atla(tok, i):
    """tok[i] == '<' ise eşleşen '>' sonrasını döndürür (>> tek belirteç değil; TOKEN_RE '>' '>' ayırır)."""
    d = 0
    for j in range(i, min(len(tok), i + 200)):
        s = tok[j][0]
        if s == '<':
            d += 1
        elif s == '>':
            d -= 1
            if d == 0:
                return j + 1
        elif s in (';', '{', '}'):
            return None
    return None


def bildirim_ad(tok, i, son):
    """tok[i:son] bir bildirim başlangıcı mı? (tip ... ad (= ; ( { : , ) [ ) → (ad, ad_indeksi) ya da None."""
    j = i
    while j < son and tok[j][0] in NITELEYICI:
        j += 1
    if j >= son:
        return None
    s = tok[j][0]
    if not re.match(r'[A-Za-z_]\w*$', s) or s in ANAHTAR:
        return None
    # tip adı (A::B::C, şablon argümanları)
    j += 1
    while j < son:
        if tok[j][0] == '::' and j + 1 < son and re.match(r'[A-Za-z_]\w*$', tok[j + 1][0]):
            j += 2
        elif tok[j][0] == '<':
            k = sablon_atla(tok, j)
            if k is None:
                return None
            j = k
        else:
            break
    while j < son and tok[j][0] in ('*', '&', 'const', '&&'):
        j += 1
    if j >= son:
        return None
    ad = tok[j][0]
    if not re.match(r'[A-Za-z_]\w*$', ad) or ad in ANAHTAR or ad in NITELEYICI:
        return None
    if j + 1 < son and tok[j + 1][0] in ('=', ';', '(', '{', ':', ',', '[', ')'):
        if tok[j + 1][0] == ':' and j + 2 < son and tok[j + 2][0] == ':':
            return None
        return ad, j
    return None


def parametreler(tok, a, b):
    """tok[a] == '(' ... tok[b] == ')' → parametre adları."""
    adlar, parca, d = [], [], 0
    for k in range(a + 1, b + 1):
        s = tok[k][0]
        if s in '(<[{':
            d += 1
        elif s in ')>]}' and k != b:
            d -= 1
        if (s == ',' and d == 0) or k == b:
            p = [x for x in parca]
            if '=' in p:
                p = p[:p.index('=')]
            idler = [x for x in p if re.match(r'[A-Za-z_]\w*$', x) and x not in NITELEYICI]
            if len(idler) >= 2 and re.match(r'[A-Za-z_]\w*$', p[-1]):
                adlar.append(p[-1])
            parca = []
        else:
            parca.append(s)
    return adlar


def siniflari_topla(basliklar):
    """{sınıf: (üst sınıflar, veri üyeleri)} — proje başlıklarındaki class/struct gövdeleri (ilk düzey veri üyeleri)."""
    siniflar = {}
    for yol, t in basliklar:
        tok = tokenle(t)
        i = 0
        while i < len(tok):
            if tok[i][0] in ('class', 'struct') and i + 1 < len(tok):
                j = i + 1
                if re.match(r'[A-Z]+_API$', tok[j][0]):
                    j += 1
                if j >= len(tok) or not re.match(r'[A-Za-z_]\w*$', tok[j][0]):
                    i += 1
                    continue
                ad = tok[j][0]
                k = j + 1
                if k < len(tok) and tok[k][0] == 'final':
                    k += 1
                ustler = []
                if k < len(tok) and tok[k][0] == ':':
                    while k < len(tok) and tok[k][0] not in ('{', ';'):
                        if re.match(r'[A-Za-z_]\w*$', tok[k][0]) and tok[k][0] not in ('public', 'private', 'protected', 'virtual'):
                            ustler.append(tok[k][0])
                        k += 1
                if k < len(tok) and tok[k][0] == '{':
                    son = eslesen(tok, k, '{', '}')
                    uyeler = set()
                    d = 0
                    bas = k + 1
                    m = k + 1
                    while m < son:
                        s = tok[m][0]
                        if s == '{':
                            # satır içi işlev gövdesi / iç tip: atla
                            m = eslesen(tok, m, '{', '}') + 1
                            bas = m
                            continue
                        if s == '(':
                            m = eslesen(tok, m, '(', ')') + 1
                            continue
                        if s in (';',) or (s == ':' and m > 0 and tok[m - 1][0] in ('public', 'private', 'protected')):
                            parca = [x for x in tok[bas:m] if x[0] not in ('UPROPERTY',)]
                            # UPROPERTY(...) kısmını at
                            p = [x[0] for x in tok[bas:m]]
                            if 'UPROPERTY' in p:
                                q = p.index('UPROPERTY')
                                if q + 1 < len(p) and p[q + 1] == '(':
                                    kap = q + 1
                                    dd = 0
                                    for z in range(q + 1, len(p)):
                                        if p[z] == '(':
                                            dd += 1
                                        elif p[z] == ')':
                                            dd -= 1
                                            if dd == 0:
                                                kap = z
                                                break
                                    p = p[:q] + p[kap + 1:]
                            if p and p[0] not in ('using', 'typedef', 'friend', 'class', 'struct', 'enum', 'DECLARE_MULTICAST_DELEGATE_OneParam') \
                                    and '(' not in p and 'operator' not in p:
                                if '=' in p:
                                    p = p[:p.index('=')]
                                if '{' in p:
                                    p = p[:p.index('{')]
                                if '[' in p:
                                    p = p[:p.index('[')]
                                if ':' in p and len(p) > 2:   # bit alanı
                                    p = p[:p.index(':')]
                                if len(p) >= 2 and re.match(r'[A-Za-z_]\w*$', p[-1]) and p[-1] not in NITELEYICI:
                                    uyeler.add(p[-1])
                            bas = m + 1
                        m += 1
                    eski = siniflar.get(ad)
                    if eski is None or len(uyeler) > len(eski[1]):
                        siniflar[ad] = (ustler, uyeler)
                    i = k + 1
                    continue
            i += 1
    return siniflar


def tum_uyeler(sinif, siniflar, gorulen=None):
    gorulen = gorulen or set()
    if sinif in gorulen:
        return set()
    gorulen.add(sinif)
    out = set()
    if sinif in siniflar:
        ustler, uyeler = siniflar[sinif]
        out |= uyeler
        for u in ustler:
            out |= tum_uyeler(u, siniflar, gorulen)
    if sinif in MOTOR_UYELER:
        out |= MOTOR_UYELER[sinif]
    if sinif in MOTOR_UST:
        out |= tum_uyeler(MOTOR_UST[sinif], siniflar, gorulen)
    return out


def kapsam_denetimi(goreli, tok, z, son, ilk, uyeler=frozenset(), sinif_adi=''):
    """tok[z] == '{' ... tok[son] == '}' gövdesinde kapsam yığını; gölgeleme iletileri döndürür."""
    hatalar = []

    def kontrol(ad, satir, yigin, tur):
        for kap in yigin:
            if ad in kap:
                hatalar.append(f'{goreli}:{satir}: {tur} "{ad}" dış kapsamdaki aynı adlı bildirimi gölgeliyor (satır {kap[ad]}) — C4456/C4457')
                return
        if ad in uyeler:
            hatalar.append(f'{goreli}:{satir}: {tur} "{ad}" {sinif_adi} sınıf üyesini gölgeliyor — C4458')

    yigin = [dict(ilk), {}]
    k = z + 1
    bekleyen = None   # lambda / for parametreleri: sonraki '{' kapsamına
    onceki = '{'
    while k < son:
        s = tok[k][0]
        if s == '{':
            yigin.append(dict(bekleyen or {}))
            bekleyen = None
            onceki = '{'
            k += 1
            continue
        if s == '}':
            if len(yigin) > 2:
                yigin.pop()
            onceki = '}'
            k += 1
            continue
        # lambda: [yakalama] ( parametreler ) [mutable] [-> tip] {
        if s == '[' and not re.match(r'[A-Za-z_]\w*$|[)\]]$', tok[k - 1][0]):
            kap = eslesen(tok, k, '[', ']')
            if kap + 1 < son and tok[kap + 1][0] == '(':
                pk = eslesen(tok, kap + 1, '(', ')')
                w = pk + 1
                while w < son and tok[w][0] not in ('{', ';', ')', ','):
                    w += 1
                if w < son and tok[w][0] == '{':
                    bekleyen = {}
                    for ad in parametreler(tok, kap + 1, pk):
                        kontrol(ad, tok[kap + 1][1], yigin, 'lambda parametresi')
                        bekleyen[ad] = tok[kap + 1][1]
                    k = w
                    continue
        if s == 'for' and k + 1 < son and tok[k + 1][0] == '(':
            pk = eslesen(tok, k + 1, '(', ')')
            b = bildirim_ad(tok, k + 2, pk + 1)
            bekleyen = {}
            if b:
                kontrol(b[0], tok[b[1]][1], yigin, 'for değişkeni')
                bekleyen[b[0]] = tok[b[1]][1]
            k = pk + 1
            if k < son and tok[k][0] != '{':
                bekleyen = None
            continue
        if onceki in (';', '{', '}'):
            b = bildirim_ad(tok, k, son)
            if b:
                kontrol(b[0], tok[b[1]][1], yigin, 'yerel değişken')
                yigin[-1][b[0]] = tok[b[1]][1]
                k = b[1] + 1
                onceki = '_id'
                continue
        onceki = s if s in (';', '{', '}') else '_id'
        k += 1
    return hatalar


def golgeleme(goreli, t, siniflar):
    """Üye işlev gövdeleri: C4456/C4457/C4458; üye olmayan işlevler: C4456/C4457."""
    tok = tokenle(t)
    hatalar = []
    n = len(tok)
    i = 0
    while i < n:
        s = tok[i][0]
        if s != '(' or i == 0 or not re.match(r'[A-Za-z_]\w*$', tok[i - 1][0]) or tok[i - 1][0] in ANAHTAR:
            i += 1
            continue
        pk = eslesen(tok, i, '(', ')')
        z = pk + 1
        while z < n and tok[z][0] in ('const', 'override', 'noexcept', 'final', '&', '&&'):
            z += 1
        uye = i >= 3 and tok[i - 2][0] == '::'
        if uye and tok[i - 3][0] == '~':
            pass
        sinif_adi = ''
        if uye:
            sinif_adi = tok[i - 3][0] if tok[i - 3][0] != '~' else tok[i - 4][0]
            if z < n and tok[z][0] == ':':   # kurucu başlatıcı listesi
                while z < n and tok[z][0] != '{':
                    if tok[z][0] == '(':
                        z = eslesen(tok, z, '(', ')')
                    z += 1
                    # Ad{...} başlatıcısı: '{' öncesi ad, sonrası ',' ya da '{' → atla
                    if z < n and tok[z][0] == '{' and re.match(r'[A-Za-z_]\w*$', tok[z - 1][0]) and tok[z - 2][0] in (':', ','):
                        z = eslesen(tok, z, '{', '}') + 1
        tanim = z < n and tok[z][0] == '{'
        if tanim and not uye:
            # serbest işlev tanımı: önünde tip olmalı (çağrı değil)
            on = tok[i - 2][0] if i >= 2 else ''
            tanim = bool(re.match(r'[A-Za-z_]\w*$|[>*&]$', on)) and on not in ANAHTAR and on not in ('return',)
        if not tanim:
            i += 1
            continue
        son = eslesen(tok, z, '{', '}')
        uyeler = tum_uyeler(sinif_adi, siniflar) if uye else frozenset()
        ilk = {}
        for ad in parametreler(tok, i, pk):
            if ad in uyeler:
                hatalar.append(f'{goreli}:{tok[i][1]}: parametre "{ad}" {sinif_adi} sınıf üyesini gölgeliyor — C4458')
            ilk[ad] = tok[i][1]
        hatalar.extend(kapsam_denetimi(goreli, tok, z, son, ilk, uyeler, sinif_adi))
        i = son + 1
    return hatalar


def lambda_esittir(goreli, t):
    out = []
    for no, satir in enumerate(t.split('\n'), 1):
        if re.search(r'\[\s*=\s*[\],]', satir):
            out.append(f'{goreli}:{no}: [=] lambda — C++20\'de örtük this yakalama kullanımdan kalktı; [this, ...] ya da açık değer yakalaması kullanın')
    return out


def dosya_kapsami_adlar(t):
    """static / anonim namespace içindeki dosya-kapsamı adlar + #undef edilmeyen #define'lar."""
    adlar = set()
    for m in re.finditer(r'^static\s+(?:const\s+|constexpr\s+|inline\s+)*[\w:<>,\s*&]+?\b(\w+)\s*(?:\(|=|\[)', t, flags=re.M):
        adlar.add(m.group(1))
    for m in re.finditer(r'namespace\s*\{', t):
        bas = m.end()
        d, j = 1, bas
        while j < len(t) and d:
            if t[j] == '{':
                d += 1
            elif t[j] == '}':
                d -= 1
            j += 1
        govde = t[bas:j - 1]
        # yalnız ilk düzey
        duz, d = [], 0
        for c in govde:
            if c == '{':
                d += 1
            elif c == '}':
                d -= 1
                continue
            if d == 0:
                duz.append(c)
        for m2 in re.finditer(r'(?:^|[;}])\s*(?:static\s+|const\s+|constexpr\s+|inline\s+)*[\w:<>,*&]+\s+[*&]?(\w+)\s*(?:\(|=|\[|;)', ''.join(duz), flags=re.M):
            adlar.add(m2.group(1))
        for m2 in re.finditer(r'(?:struct|class|enum(?:\s+class)?)\s+(\w+)', govde):
            adlar.add(m2.group(1))
    tanim = set(re.findall(r'^\s*#\s*define\s+(\w+)', t, flags=re.M))
    tanimsiz = set(re.findall(r'^\s*#\s*undef\s+(\w+)', t, flags=re.M))
    return adlar, tanim - tanimsiz


KALDIRILMIS = [
    (r'\bFTicker\b', 'FTicker UE5.0\'da kaldırıldı → FTSTicker::GetCoreTicker()'),
    (r'\bFEditorStyle\b', 'FEditorStyle kaldırıldı → FAppStyle'),
    (r'\bTAtomic\s*<', 'TAtomic kullanımdan kalktı → std::atomic'),
    (r'(?:const\s+)?(?:ANSI)?(?:char|ANSICHAR)\s*\*\s*\w+\s*=\s*TCHAR_TO_(?:UTF8|ANSI)\s*\(', 'TCHAR_TO_UTF8 sonucu işaretçide saklanıyor (geçici ömrü deyim sonunda biter) → FTCHARToUTF8 nesnesi'),
    (r'\bauto\s*\*?\s*\w+\s*=\s*TCHAR_TO_(?:UTF8|ANSI)\s*\(', 'TCHAR_TO_UTF8 sonucu auto ile saklanıyor → FTCHARToUTF8 nesnesi'),
    (r'\bFPaths::GameDir\b|\bFPaths::GameSavedDir\b', 'FPaths::GameDir kaldırıldı → ProjectDir / ProjectSavedDir'),
    (r'\bGetWorld\(\)->GetTimerManager\(\)\.SetTimer\(\s*\w+\s*,\s*\[', 'SetTimer lambda doğrudan alınmaz → FTimerDelegate::CreateLambda'),
    (r'\bBindUObject\(\s*this\s*,\s*&\w+::\w+\s*\)\s*;\s*//?.*FTSTicker', ''),
    (r'\bFVector::(?:ZeroVector|OneVector)\s*\.\s*Size\(\)', ''),
    (r'\bUProperty\b|\bUArrayProperty\b|\bUStructProperty\b', 'UProperty → FProperty (UE4.25+)'),
    (r'\bFindField<\s*UProperty', 'FindField<UProperty> → FindFProperty'),
    (r'\bANY_PACKAGE\b', 'ANY_PACKAGE UE5.1\'de kaldırıldı → FindFirstObject'),
    (r'\bFMemory::Memzero\s*\(\s*this\b', ''),
    (r'\bEKeys::Gamepad_(?:LeftTrigger|RightTrigger)Axis\b', ''),
    (r'\bFSlateDrawElement::MakeLines\s*\([^;]*ESlateDrawEffect::None\s*,\s*FLinearColor[^;]*\)\s*;', ''),
    (r'\bToPaintGeometry\s*\(\s*FVector2D\s*\([^)]*\)\s*,\s*FVector2D\s*\(', 'ToPaintGeometry(Offset, Size) kaldırıldı (UE5.2+) → ToPaintGeometry(Size, FSlateLayoutTransform(Offset))'),
    (r'\bGetBrush\s*\(\s*"', 'GetBrush("...") TEXT() olmadan da derlenir (FName) — sorun değil'),
]


def kaldirilmis(goreli, t):
    out = []
    for no, satir in enumerate(t.split('\n'), 1):
        for desen, ileti in KALDIRILMIS:
            if ileti and 'sorun değil' not in ileti and re.search(desen, satir):
                out.append(f'{goreli}:{no}: {ileti}')
    return out


FMATH_RE = re.compile(r'FMath::(Clamp|Max|Min)\s*\(')


def fmath_karisik(goreli, t):
    """FMath::Clamp/Max/Min: float/double karışımı UE5'te SORUN DEĞİL (MIX_FLOATS aşırı yükleri); tamsayı sabiti + kayan nokta
    karışımı ise şablon çıkarımını bozar (C2672/C2782) — UYARI (diğer argümanın tipi kesin bilinmiyor)."""
    out = []
    for m in FMATH_RE.finditer(t):
        bas = m.end()
        d, j = 1, bas
        while j < len(t) and d:
            if t[j] == '(':
                d += 1
            elif t[j] == ')':
                d -= 1
            j += 1
        govde = t[bas:j - 1]
        # üst düzey argümanlar
        args, parca, dd = [], [], 0
        for c in govde:
            if c in '([{<':
                dd += 1
            elif c in ')]}>':
                dd -= 1
            if c == ',' and dd == 0:
                args.append(''.join(parca).strip())
                parca = []
            else:
                parca.append(c)
        args.append(''.join(parca).strip())
        if len(args) < 2:
            continue
        def tur(a):
            if re.match(r'^-?\d+\.\d*f$|^-?\d+\.?\d*e-?\d+f$|^-?\.\d+f$', a) or re.match(r'^\(float\)', a) or a.startswith('static_cast<float>'):
                return 'f'
            if re.match(r'^-?\d+\.\d*$|^-?\d+\.\d*e-?\d+$', a) or a.startswith('(double)') or a.startswith('static_cast<double>'):
                return 'd'
            if re.match(r'^-?\d+$', a):
                return 'i'
            if re.search(r'\.(X|Y|Z)\b\s*$|\.(Pitch|Yaw|Roll)\b\s*$|\b(Size|Length|SizeSquared|Size2D|Dot)\s*\(\s*\)\s*$|^FVector::Dist\w*\(', a):
                return 'd?'
            return '?'
        turler = [tur(a) for a in args]
        if 'i' in turler and any(x in turler for x in ('f', 'd', 'd?')):
            satir = t.count('\n', 0, m.start()) + 1
            out.append(f'{goreli}:{satir}: FMath::{m.group(1)} tamsayı sabiti + kayan nokta karışık ({", ".join(args)[:90]}) — şablon çıkarımı (C2672/C2782)')
    return out


# Build.cs modülü → onu sağlayan eklenti (motor modülü değil)
EKLENTI_MODUL = {
    'EnhancedInput': 'EnhancedInput', 'OnlineSubsystem': 'OnlineSubsystem', 'OnlineSubsystemUtils': 'OnlineSubsystemUtils',
    'ProceduralMeshComponent': 'ProceduralMeshComponent', 'CesiumRuntime': 'CesiumForUnreal', 'glTFRuntime': 'glTFRuntime',
    'OnlineSubsystemSteam': 'OnlineSubsystemSteam', 'SteamSockets': 'SteamSockets', 'RawInput': 'RawInput',
}


def eklenti_tutarlilik(kok):
    out = []
    up = [f for f in os.listdir(kok) if f.endswith('.uproject')]
    if not up:
        return out
    proje = json.load(open(os.path.join(kok, up[0]), encoding='utf-8'))
    eklentiler = {p['Name'] for p in proje.get('Plugins', []) if p.get('Enabled')}
    eklentiler |= {p['Name'] for p in proje.get('Plugins', []) if p.get('Optional')}
    moduller = {m['Name'] for m in proje.get('Modules', [])}
    src = os.path.join(kok, 'Source')
    for modul in os.listdir(src):
        bc = os.path.join(src, modul, modul + '.Build.cs')
        if not os.path.isfile(bc):
            continue
        if modul not in moduller:
            out.append(f'{up[0]}: Source/{modul} modülü Modules listesinde yok')
        t = open(bc, encoding='utf-8').read()
        for m in sorted(set(re.findall(r'"([A-Za-z0-9_]+)"', t))):
            if m in EKLENTI_MODUL and EKLENTI_MODUL[m] not in eklentiler:
                out.append(f'Source/{modul}/{modul}.Build.cs: "{m}" modülü {EKLENTI_MODUL[m]} eklentisinden gelir; {up[0]} Plugins listesinde etkin değil')
    for hedef in [f for f in os.listdir(src) if f.endswith('.Target.cs')]:
        t = open(os.path.join(src, hedef), encoding='utf-8').read()
        for m in re.findall(r'"([A-Za-z0-9_]+)"', t):
            if os.path.isdir(os.path.join(src, m)) and m not in moduller:
                out.append(f'Source/{hedef}: {m} Modules listesinde yok')
    return out


def oz_sinama(yorumsuz):
    """Denetleyicilerin kendisini sınar (sezgisel ayrıştırıcı bozulursa sessizce 0 bulgu vermesin)."""
    h = 'class AFoo : public AActor { int32 Speed; int32 Count = 0; UPROPERTY() TObjectPtr<UObject> M; void Baz(); };'
    c = ('void AFoo::Bar(int32 Speed, float X)\n{\n\tint32 Y = 0;\n\tfor (int32 i = 0; i < 3; ++i) { int32 Y = i; }\n'
         '\tauto L = [this](float X) { return X; };\n\tif (X > 0) { const TArray<int32>& Count = Arr; }\n\tTMap<FString, int32> M;\n'
         '\tauto Q = [=]() { return Speed; };\n}\nstatic int Helper(int A) { int B = 0; { int A = 2; } return B; }\n')
    sin = siniflari_topla([('x.h', h)])
    bul = golgeleme('t.cpp', yorumsuz(c), sin) + lambda_esittir('t.cpp', yorumsuz(c))
    beklenen = ['t.cpp:1: parametre "Speed"', 't.cpp:4: yerel değişken "Y"', 't.cpp:5: lambda parametresi "X"',
                't.cpp:6: yerel değişken "Count"', 't.cpp:7: yerel değişken "M"', 't.cpp:8: [=]', 't.cpp:10: yerel değişken "A"']
    eksik = [b for b in beklenen if not any(x.startswith(b) for x in bul)]
    fazla = len(bul) - len(beklenen)
    return [f'öz sınama: beklenen bulgu yok: {e}' for e in eksik] + ([f'öz sınama: {fazla} fazla bulgu: {bul}'] if fazla > 0 else [])
