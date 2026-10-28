# Test fulfilment ACSM (sans retrait DRM)

Objectif : vérifier qu’on peut, comme Calibre/DeACSM, **échanger un `.acsm` contre un EPUB Adobe DRM** via le protocole ADEPT — **sans** appeler `adept_remove` / DeDRM.

Code de référence open-source :
- [libgourou](https://blog.soutade.fr/post/2021/07/libgourou-a-free-adept-protocol-implementation.html) (protocole ADEPT)
- [DeACSM / acsm-calibre-plugin](https://github.com/Leseratte10/acsm-calibre-plugin) (réimplémentation Python)
- Image Docker : [bcliang/docker-libgourou](https://github.com/bcliang/docker-libgourou)

## Prérequis

- Compte **ByteBooks** (pas un vieux Adobe ID seul — Adobe a basculé le login ADE)
- Un `.acsm` **non expiré**
- Outils : AppImage **libgourou 0.8.10** dans `third_party/libgourou-0.8.10/` (déjà en place), ou Docker en secours

### Erreur `0x500b` SSL

L’image Docker `bcliang/docker-libgourou` n’embarque souvent **pas** les certificats CA → `Error SSL peer certificate…`.  
Le script utilise maintenant l’**AppImage 0.8.10** en priorité (corrige aussi HTTPS + ByteBooks).

## Étape 1 — Activer

```bash
cd /home/gilles/pocketbook-kobo-shop
# nettoyer une tentative Docker ratée éventuelle
rm -rf tools/acsm_fulfill/adept/*
./tools/acsm_fulfill/run_fulfill.sh activate
```

Saisir l’e-mail / mot de passe **ByteBooks**.

## Étape 2 — Fulfil ACSM → EPUB DRM

```bash
./tools/acsm_fulfill/run_fulfill.sh download ascm_exemple/test.acsm
```

Résultat attendu : `tools/acsm_fulfill/out/book.drm` (EPUB Adobe DRM, **pas** cracké).

### Statut spike (2026-10-28) — **GO**

Activation ByteBooks + fulfil `test.acsm` (Kobo) → `book.drm` EPUB ~6 Mo, DRM Adobe conservé.

## Ce que ce test ne fait PAS

- Pas de `adept_remove`
- Pas d’export de clé privée pour DeDRM
- Pas d’intégration PocketBook encore

## Interprétation

| Résultat | Conclusion |
|---|---|
| EPUB DRM obtenu (**observé**) | ADEPT OK côté PC ; le client ACSM PocketBook est le goulot, pas Kobo |
| Erreur fulfilment Kobo | Même blocage serveur / négociation que sur PocketBook → à documenter |
| ACSM expiré | Retélécharger depuis kobo.com puis réessayer |
