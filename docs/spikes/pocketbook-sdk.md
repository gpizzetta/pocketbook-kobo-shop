# Spike — PocketBook / SDK

| | |
|---|---|
| **Date** | 2026-10-28 |
| **Statut** | OK — hello world validé sur appareil (2026-10-28) |
| **Montage USB** | `/media/gilles/6940-95FF` (VFAT) |

## Identification appareil

| Champ | Valeur | Source |
|---|---|---|
| Modèle commercial | **PocketBook Era** | Manuels `system/manuals/User_Manual_Era_*.pdf` |
| Code USB / ADE | **PB700** | `lsusb` (`Obreey PB700`), `device.xml` |
| Série | `JZ4F1100036421W000U4` | USB / ADE `deviceSerial` |
| Architecture | `arm-obreey-linux-gnueabi` | Cache fontconfig |
| Famille SoC (communauté) | Allwinner **B288** | Discussions SDK / apps communautaires |
| Firmware logiciel | **`u700.6.10.3209`** | Utilisateur (Réglages → À propos), 2026-10-28 |
| Adobe DRM activé | **Non** (`pbactivation.cfg` vide) | `.adobe-digital-editions/` |
| ADE hobbes (stack Adobe embarquée) | `12.5.4.184545` | `device.xml` |
| Dossier apps | `applications/` **présent et vide** | Prêt pour sideload `.app` |

## Points vérifiés

- [x] Connexion USB stockage (mode disque)
- [x] Modèle = Era / PB700
- [x] Dossier `/applications` accessible en écriture depuis le PC
- [x] Présence stack Adobe (dossiers + `device.xml`) ; activation utilisateur absente
- [x] Version firmware exacte : `u700.6.10.3209`
- [x] Choix SDK cible : **6.10 / B288**
- [x] SDK téléchargé et extrait : [Sean-on-Git/PocketBook-SDK 6.10](https://github.com/Sean-on-Git/PocketBook-SDK/releases/tag/6.10) → `third_party/SDK-B288-6.10`
- [x] Hello world **compilé** (`apps/hello_kobo`, binaire ARM EABI5, `ld-linux.so.3`)
- [x] Hello world **copié** vers `applications/hello_kobo.app` sur la liseuse
- [x] Hello world **lancé et validé** sur l’écran (confirmation utilisateur)
- [ ] Test HTTPS / réseau depuis une app
- [ ] Ouverture d’un EPUB depuis une app vers le lecteur système
- [ ] Test EPUB Adobe DRM après autorisation Adobe ID

## Notes SDK

- Identifiant firmware : préfixe **`u700`** = Era (PB700), version **`6.10.3209`**.
- Famille SoC : **B288** (`arm-obreey-linux-gnueabi`).
- SDK utilisé : `SDK-B288-6.10.7z` (Sean-on-Git), toolchain Clang 15 / GCC 13.3, `share/cmake/arm_conf.cmake`.
- Build local : `apps/hello_kobo/build.sh` → `dist/hello_kobo.app`.
- Déploiement : copier le `.app` dans `applications/` sur le stockage USB de la liseuse.

## Validation hello world

Confirmé par l’utilisateur le 2026-10-28 : l’app `hello_kobo` démarre et s’affiche correctement sur l’Era `u700.6.10.3209`.

## Suite du spike PocketBook

1. Mini-app test **HTTPS** (GET simple) pour valider certificats / réseau depuis InkView.
2. Tester l’**ouverture d’un EPUB** local via le lecteur système depuis une app.
3. (Optionnel) Autoriser Adobe ID / ByteBooks, puis tester un EPUB Adobe DRM.

## MVP app (2026-10-28)

App native [`apps/kobo_shop/`](../../apps/kobo_shop/) déployée : `applications/kobo_shop.app`.

- Auth Kobo (ActivateOnWeb) + ByteBooks UI + liste + DL `SignedNoDrm` + `OpenBook`
- HTTPS via **libcurl** (SDK)
- Fulfil ADEPT : hooks prêts ; binaires ARM libgourou encore WIP (`tools/acsm_fulfill/build_arm.sh`)
