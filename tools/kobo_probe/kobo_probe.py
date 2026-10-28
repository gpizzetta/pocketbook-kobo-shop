#!/usr/bin/env python3
"""
Sonde API Kobo (usage personnel / spike).

Fait:
  - ActivateOnWeb + auth/device + initialization
  - library_sync
  - content access metadata (DRMType / UrlFormat) par livre

Ne fait PAS:
  - retrait / contournement de DRM
  - déchiffrement KDRM
  - téléchargement des fichiers livres (métadonnées seulement)

Voir docs/spikes/kobo-api.md
"""

from __future__ import annotations

import base64
import html
import json
import re
import secrets
import string
import sys
import time
import urllib.parse
from collections import Counter
from pathlib import Path

import requests

AFFILIATE = "Kobo"
APP_VERSION = "4.38.23171"
PLATFORM_ID = "00000000-0000-0000-0000-000000000373"
DISPLAY_PROFILE = "Android"
DEVICE_OS = "3.0.35+"
DEVICE_OS_VERSION = "NA"
USER_AGENT = (
    "Mozilla/5.0 (Linux; U; Android 2.0; en-us;) AppleWebKit/538.1 "
    "(KHTML, like Gecko) Version/4.0 Mobile Safari/538.1 "
    f"(Kobo Touch 0373/{APP_VERSION})"
)

ROOT = Path(__file__).resolve().parents[2]
OUT_DIR = ROOT / "docs" / "spikes"
STATE_PATH = OUT_DIR / "kobo-probe-state.json"
MATRIX_PATH = OUT_DIR / "kobo-drm-matrix.json"


def random_hex(length: int) -> str:
    return "".join(secrets.choice(string.hexdigits) for _ in range(length)).lower()


def load_state() -> dict:
    if STATE_PATH.is_file():
        return json.loads(STATE_PATH.read_text(encoding="utf-8"))
    return {}


def save_state(state: dict) -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    STATE_PATH.write_text(json.dumps(state, indent=2) + "\n", encoding="utf-8")
    print(f"État sauvé: {STATE_PATH}")


class KoboProbe:
    def __init__(self) -> None:
        self.session = requests.Session()
        self.session.headers.update({"User-Agent": USER_AGENT})
        self.session.timeout = 30
        self.state = load_state()
        self.resources: dict = {}

    def ensure_device_ids(self) -> None:
        if not self.state.get("device_id"):
            self.state["device_id"] = random_hex(64)
        if not self.state.get("serial_number"):
            self.state["serial_number"] = random_hex(32)

    def auth_headers(self) -> dict:
        token = self.state.get("access_token")
        if not token:
            raise RuntimeError("Pas d'access_token — authentifie d'abord.")
        return {"Authorization": f"Bearer {token}"}

    def authenticate_device(self, user_key: str = "") -> None:
        self.ensure_device_ids()
        post = {
            "AffiliateName": AFFILIATE,
            "AppVersion": APP_VERSION,
            "ClientKey": base64.b64encode(PLATFORM_ID.encode()).decode(),
            "DeviceId": self.state["device_id"],
            "PlatformId": PLATFORM_ID,
            "SerialNumber": self.state["serial_number"],
        }
        if user_key:
            post["UserKey"] = user_key

        r = self.session.post("https://storeapi.kobo.com/v1/auth/device", json=post)
        r.raise_for_status()
        data = r.json()
        if data.get("TokenType") != "Bearer":
            raise RuntimeError(f"TokenType inattendu: {data.get('TokenType')}")

        self.state["access_token"] = data["AccessToken"]
        self.state["refresh_token"] = data["RefreshToken"]
        if user_key:
            self.state["user_key"] = data.get("UserKey", user_key)
        save_state(self.state)

    def refresh(self) -> None:
        post = {
            "AppVersion": APP_VERSION,
            "ClientKey": base64.b64encode(PLATFORM_ID.encode()).decode(),
            "PlatformId": PLATFORM_ID,
            "RefreshToken": self.state["refresh_token"],
        }
        r = self.session.post(
            "https://storeapi.kobo.com/v1/auth/refresh",
            json=post,
            headers=self.auth_headers(),
        )
        r.raise_for_status()
        data = r.json()
        self.state["access_token"] = data["AccessToken"]
        self.state["refresh_token"] = data["RefreshToken"]
        save_state(self.state)

    def get_json(self, url: str, **kwargs) -> requests.Response:
        headers = kwargs.pop("headers", {})
        headers.update(self.auth_headers())
        r = self.session.get(url, headers=headers, **kwargs)
        if r.status_code == 401:
            print("401 → refresh token…")
            self.refresh()
            headers = self.auth_headers()
            r = self.session.get(url, headers=headers, **kwargs)
        r.raise_for_status()
        return r

    def activate_on_web(self) -> None:
        self.ensure_device_ids()
        # Device auth anonyme d'abord (comme les clients communautaires)
        self.authenticate_device("")

        params = {
            "pwspid": PLATFORM_ID,
            "wsa": AFFILIATE,
            "pwsdid": self.state["device_id"],
            "pwsav": APP_VERSION,
            "pwsdm": PLATFORM_ID,
            "pwspos": DEVICE_OS,
            "pwspov": DEVICE_OS_VERSION,
        }
        r = self.session.get("https://auth.kobobooks.com/ActivateOnWeb", params=params)
        r.raise_for_status()
        text = r.text

        m = re.search(r'data-poll-endpoint="([^"]+)"', text)
        if not m:
            raise RuntimeError("Endpoint de poll activation introuvable (page changée ?).")
        poll_url = "https://auth.kobobooks.com" + html.unescape(m.group(1))

        m = re.search(r"qrcodegenerator/generate.+?%26code%3D(\d+)", text)
        if not m:
            raise RuntimeError("Code d'activation introuvable (page changée ?).")
        code = m.group(1)

        print()
        print("=== Activation Kobo ===")
        print("1. Ouvre https://www.kobo.com/activate dans un navigateur")
        print(f"2. Entre le code : {code}")
        print("3. Connecte-toi à ton compte si demandé")
        print("En attente de confirmation…")
        print()

        while True:
            time.sleep(5)
            pr = self.session.post(poll_url)
            pr.raise_for_status()
            try:
                data = pr.json()
            except Exception as exc:
                raise RuntimeError(f"Réponse poll non JSON: {pr.text[:200]}") from exc

            if data.get("Status") == "Complete":
                redirect = data["RedirectUrl"]
                q = urllib.parse.parse_qs(urllib.parse.urlparse(redirect).query)
                self.state["user_id"] = q["userId"][0]
                user_key = q["userKey"][0]
                print("Activation OK.")
                self.authenticate_device(user_key)
                return

    def initialization(self) -> None:
        r = self.get_json("https://storeapi.kobo.com/v1/initialization")
        self.resources = r.json()["Resources"]
        print(f"Resources: {len(self.resources)} clés")

    def library_sync(self) -> list:
        url = self.resources["library_sync"]
        full: list = []
        sync_token = ""
        page = 0
        while True:
            page += 1
            headers = {}
            if sync_token:
                headers["x-kobo-synctoken"] = sync_token
            r = self.get_json(url, headers=headers)
            batch = r.json()
            full.extend(batch)
            print(f"  sync page {page}: +{len(batch)} (total {len(full)})")
            if r.headers.get("x-kobo-sync") == "continue":
                sync_token = r.headers.get("x-kobo-synctoken", "")
                if not sync_token:
                    break
            else:
                break
        return full

    @staticmethod
    def extract_product_ids(entries: list) -> list[dict]:
        """Parse library_sync entries (NewEntitlement / ChangedEntitlement…)."""
        books = []
        for entry in entries:
            ent = None
            for key in ("NewEntitlement", "ChangedEntitlement", "Entitlement"):
                if isinstance(entry.get(key), dict):
                    ent = entry[key]
                    break
            if ent is None:
                continue

            meta = ent.get("BookMetadata")
            if not isinstance(meta, dict):
                continue

            product_id = meta.get("RevisionId") or meta.get("CrossRevisionId")
            if not product_id:
                continue

            title = meta.get("Title") or "?"
            authors = meta.get("ContributorRoles") or []
            author = "?"
            if isinstance(authors, list) and authors:
                first = authors[0]
                if isinstance(first, dict):
                    author = first.get("Name") or "?"
                else:
                    author = str(first)

            book_ent = ent.get("BookEntitlement") or {}
            archived = bool(book_ent.get("IsRemoved"))

            books.append(
                {
                    "product_id": product_id,
                    "title": title,
                    "author": author,
                    "archived": archived,
                    "raw_keys": sorted(entry.keys()),
                }
            )

        seen = set()
        unique = []
        for b in books:
            if b["product_id"] in seen:
                continue
            seen.add(b["product_id"])
            unique.append(b)
        return unique

    def content_access(self, product_id: str) -> dict:
        template = self.resources["content_access_book"]
        url = template.replace("{ProductId}", product_id)
        r = self.get_json(url, params={"DisplayProfile": DISPLAY_PROFILE})
        return r.json()

    def probe_drm(self, books: list[dict], limit: int | None = None) -> list[dict]:
        results = []
        sample = books if limit is None else books[:limit]
        for i, book in enumerate(sample, 1):
            pid = book["product_id"]
            print(f"[{i}/{len(sample)}] {book['title'][:60]}")
            row = {
                "product_id": pid,
                "title": book["title"],
                "author": book["author"],
                "content_urls": [],
                "error": None,
            }
            try:
                access = self.content_access(pid)
                urls = access.get("ContentUrls") or access.get("DownloadUrls") or []
                for u in urls:
                    row["content_urls"].append(
                        {
                            "DRMType": u.get("DRMType") or u.get("DrmType"),
                            "UrlFormat": u.get("UrlFormat") or u.get("Format"),
                            "has_download_url": bool(u.get("DownloadUrl") or u.get("Url")),
                        }
                    )
                row["content_keys_count"] = len(access.get("ContentKeys") or [])
            except Exception as exc:  # noqa: BLE001 — sonde: on journalise tout
                row["error"] = str(exc)
            results.append(row)
            time.sleep(0.3)
        return results


def summarize(rows: list[dict]) -> dict:
    drm_counter: Counter[str] = Counter()
    format_counter: Counter[str] = Counter()
    empty = 0
    errors = 0
    for row in rows:
        if row.get("error"):
            errors += 1
            continue
        urls = row.get("content_urls") or []
        if not urls:
            empty += 1
            drm_counter["(aucune URL)"] += 1
            continue
        # Un livre peut avoir plusieurs URLs : on compte chaque DRMType distinct
        types = {u.get("DRMType") or "?" for u in urls}
        for t in types:
            drm_counter[str(t)] += 1
        for u in urls:
            fmt = u.get("UrlFormat") or "?"
            format_counter[str(fmt)] += 1

    total = len(rows)
    readable_keys = {"SignedNoDrm", "AdobeDrm"}
    readable = sum(drm_counter[k] for k in readable_keys)
    kdrm = drm_counter.get("KDRM", 0)

    return {
        "books_probed": total,
        "errors": errors,
        "empty_content_urls": empty,
        "drm_type_counts": dict(drm_counter),
        "url_format_counts": dict(format_counter),
        "approx_readable_without_circumvention": readable,
        "approx_kdrm_only_signal": kdrm,
        "note": (
            "Les compteurs DRMType comptent les livres ayant AU MOINS une URL de ce type. "
            "Un même livre peut apparaître sous plusieurs types."
        ),
    }


def main() -> int:
    try:
        import requests  # noqa: F401
    except ImportError:
        print("Installe requests: pip install requests", file=sys.stderr)
        return 1

    probe = KoboProbe()

    if not probe.state.get("access_token") or not probe.state.get("user_key"):
        probe.activate_on_web()
    else:
        print("Réutilisation des jetons sauvés (kobo-probe-state.json).")
        print("Pour forcer une nouvelle activation: supprime ce fichier.")

    probe.initialization()
    raw = probe.library_sync()
    books = KoboProbe.extract_product_ids(raw)
    print(f"Livres / entitlements extraits: {len(books)}")

    if not books:
        # Dump d'aide pour ajuster le parseur
        debug_path = OUT_DIR / "kobo-sync-sample.json"
        OUT_DIR.mkdir(parents=True, exist_ok=True)
        debug_path.write_text(
            json.dumps(raw[:5], indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        print(f"Aucun livre extrait. Échantillon sync → {debug_path}")
        print("Il faudra ajuster extract_product_ids selon le JSON réel.")
        return 2

    active = [b for b in books if not b.get("archived")]
    archived = len(books) - len(active)
    print(f"Dont archivés (skip access): {archived} — à sonder: {len(active)}")

    rows = probe.probe_drm(active)
    summary = summarize(rows)
    summary["entitlements_total"] = len(books)
    summary["archived_skipped"] = archived
    payload = {"summary": summary, "books": rows}
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    MATRIX_PATH.write_text(
        json.dumps(payload, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )

    print()
    print("=== Résumé DRM ===")
    print(json.dumps(summary, indent=2, ensure_ascii=False))
    print(f"\nMatrice complète: {MATRIX_PATH}")

    readable = summary["approx_readable_without_circumvention"]
    kdrm = summary["approx_kdrm_only_signal"]
    if readable == 0 and kdrm > 0:
        print("\nIndicateur: beaucoup de KDRM, peu/pas de SignedNoDrm/AdobeDrm.")
        print("→ MVP lecture via store-API seul probablement insuffisant (voir spike).")
    elif readable > 0:
        print("\nIndicateur: des titres potentiellement lisibles sans contournement.")
        print("→ Go MVP possible pour ces titres; KDRM reste incompatible.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
