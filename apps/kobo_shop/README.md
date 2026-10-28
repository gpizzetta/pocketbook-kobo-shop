# kobo_shop — MVP PocketBook × Kobo

Application native InkView (Era / B288 / firmware 6.10) :

1. **Compte Kobo** — ActivateOnWeb (code à entrer sur kobo.com/activate)
2. **Compte ByteBooks** — e-mail / mot de passe (identité ADEPT)
3. **Bibliothèque** — liste des titres achetés
4. **Téléchargement** — `SignedNoDrm` → EPUB ; sinon **ACSM** via `DisplayProfile=Desktop` (export Adobe)
5. **Fulfil ADEPT** — ACSM → EPUB Adobe DRM (`acsmdownloader` ARM + compte ByteBooks)

Pas de retrait de DRM (`adept_remove` / DeDRM).

## Build

```bash
./apps/kobo_shop/build.sh
# → dist/kobo_shop.app
# → dist/kobo_shop/{adept_activate,acsmdownloader,adept_libs,ca-bundle.crt}
```

Le `.app` PocketBook est un **exécutable**, pas une archive : on ne peut pas y embarquer des fichiers.
Les helpers ADEPT vont donc dans le dossier `applications/kobo_shop/` (invisible au lanceur).

## Déploiement

```bash
./apps/kobo_shop/deploy.sh
# ou à la main :
# cp dist/kobo_shop.app /media/$USER/*/applications/
# cp -a dist/kobo_shop /media/$USER/*/applications/
```

Sur la liseuse :

```
applications/
  kobo_shop.app           ← entrée Applications
  kobo_shop/              ← install (binaires ADEPT, libs, icons/)
    icons/kobo_shop.bmp   ← icône lanceur
kobo_shop_data/           ← créé par l'app (racine stockage)
  config.json, library.json, covers/, adept/
  acsm/                   ← dépôt USB des .acsm
  kdrm/                   ← archives EPUB KDRM
Books/Kobo/               ← EPUB Adobe / libres
```

Éjecter, lancer **kobo_shop**.

### Icônes

```bash
.venv/bin/python apps/kobo_shop/scripts/generate_icons.py
```

`deploy.sh` place tout sous `applications/kobo_shop/icons/` et met à jour `view.json`.

## Première utilisation

1. Wi‑Fi ON
2. Menu → **1 Connecter Kobo** → ouvrir https://www.kobo.com/activate sur un autre appareil, entrer le code
3. **2 Compte ByteBooks** → e-mail / mot de passe ByteBooks (activation ADEPT si outils présents)
4. **3 Bibliothèque** → sync → choisir un titre → **Télécharger** → **Ouvrir**

Données : `/mnt/ext1/kobo_shop_data/`  
Livres : `/mnt/ext1/Books/Kobo/`  
ACSM : `/mnt/ext1/kobo_shop_data/acsm/`  
KDRM : `/mnt/ext1/kobo_shop_data/kdrm/`

## Titres sans ACSM via l’API (souvent label « sans ACSM »)

L’API appareil ne propose parfois que du KDRM, alors que **kobo.com** donne quand même un `.acsm` (Adobe). Flux prévu — **sans retrait de DRM** :

1. PC : [kobo.com](https://www.kobo.com) → Mes livres → **Télécharger** (Adobe) → `.acsm`
2. USB → copier dans **`kobo_shop_data/acsm/`**
3. Liseuse : **kobo_shop** → ☰ → **Importer ACSM**  
   (ByteBooks activé une fois)
4. Fulfil → EPUB dans `Books/Kobo/`, retire le `.acsm` → **Ouvrir**

Plusieurs `.acsm` d’un coup, puis un seul import.

## Périmètre MVP

| Chemin | Statut |
|---|---|
| Auth Kobo + liste | Inclus |
| DL `SignedNoDrm` | Inclus |
| DL ACSM (`DisplayProfile=Desktop`) + fulfil ADEPT | Inclus (binaires ARM dans `dist/`) |
| Titre sans ACSM côté API | Message + repli kobo.com |
| Ouverture lecteur système | Inclus (`OpenBook`) |

## Build outils ADEPT ARM

```bash
./tools/acsm_fulfill/build_arm.sh
# → tools/acsm_fulfill/dist_arm/{adept_activate,acsmdownloader}
```

Inclus automatiquement dans `dist/kobo_shop/` par `build.sh`.
