# kobo_probe

Sonde locale pour le spike API Kobo.

**Fait** : login ActivateOnWeb, sync bibliothèque, inventaire `DRMType` / `UrlFormat`.  
**Ne fait pas** : retrait DRM, déchiffrement KDRM, téléchargement des fichiers livres.

## Lancer

```bash
cd /home/gilles/pocketbook-kobo-shop
python3 -m venv .venv
source .venv/bin/activate
pip install requests
python3 tools/kobo_probe/kobo_probe.py
```

Ouvre ensuite `https://www.kobo.com/activate` et entre le code affiché.

## Sorties

| Fichier | Contenu |
|---|---|
| `docs/spikes/kobo-probe-state.json` | DeviceId / tokens (ne pas committer) |
| `docs/spikes/kobo-drm-matrix.json` | Matrice DRM par livre + résumé |

Ajoute ces fichiers JSON à `.gitignore` s’ils contiennent des secrets.
