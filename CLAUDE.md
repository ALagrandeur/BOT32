# CLAUDE.md — BOT32

> Fichier lu automatiquement par Claude Code à chaque conversation.
> Il contient le contexte, les règles et les conventions du projet.
> **À lire en entier avant toute modification.**

---

## 1. Le projet en une phrase

**BOT32** : firmware ESP32 embarqué qui **détourne la jauge de température (coolant)**
du combiné d'instruments d'une **VW Golf MK7 Alltrack 2017** pour y **afficher la
pression de suralimentation (boost turbo)**, plus un ensemble croissant de fonctions
de diagnostic / monitoring (UDS, sniffers de boutons, lien Haldex, UI web).

Auteur / propriétaire : **Antoine Lagrandeur** — hobbyiste, communique **en français**.

---

## 2. Où est le code

| Élément | Chemin / référence |
|---|---|
| Repo git local | `C:\Users\AntoineLagrandeur\BOT32` |
| GitHub | `ALagrandeur/BOT32`, branche `master` |
| Sketch principal | `BOT32/BOT32.ino` (+ modules `.cpp/.h`) |
| Sketch jumeau Haldex | `BOT32-Haldex/BOT32-Haldex.ino` |
| UI PC | `webui/` — Flask + SocketIO (`server.py`, `static/index.html`, `app.js`, `style.css`) |
| UI mobile | servie en PROGMEM depuis `BOT32/wifi_ui.cpp` (`MOBILE_HTML`) |
| Docs | `docs/` |
| Captures véhicule (SavvyCAN) | `DATA input from CAR/*.csv` |
| Outils | `tools/verify_crc.py` |

> Note : Claude Code est généralement lancé depuis `C:\Users\AntoineLagrandeur\MK7-cluster`
> (c'est là que se charge la **mémoire auto**). Le code, lui, est dans `BOT32`.

---

## 3. Matériel

- **ESP32 DevKit** + **WaveShare 2-CH CAN HAT** (2× MCP2515 + 2× SIT65HVD230).
- Cristaux MCP2515 = **16 MHz** (constante `MCP2515_CLOCK_MHZ` — critique pour le timing CAN).
- Tout en **3.3 V** via le jumper VIO en position **3V3**, **pas** de level shifter.
- **Bus SPI partagé (VSPI)** : SCK=18, MISO=19, MOSI=23.
- **CAN0 = bus combiné (cluster)** : CS=5, INT=4.
- **CAN1 = bus OBD2** : CS=25, INT=26.
- LED statut : GPIO 2.
- Bench test : les **2 jumpers 120 Ω du HAT doivent être ON** (terminaison ; pas de
  bus véhicule pour faire l'ACK). En véhicule : jumpers **OFF** (terminateurs OEM présents).

---

## 4. Architecture firmware (machine à états)

```
BOOT        (5 s listen-only au démarrage, aucune TX)
SILENT      (levier P/R/N/D : pas de TX, le combiné affiche le vrai coolant)
BOOST       (levier S/M : poll MAP via OBD2, TX Motor_09 override boost)
SAFE_FAULT  (TX désactivée par l'utilisateur ou erreur fatale : aucune TX)
BENCH_TEST  (combiné sur établi sans véhicule : émet le bundle complet)
```

Fonction cœur : **boost-on-coolant** — on calcule un octet « coolant » à partir du MAP
(mapping linéaire `map_min_mbar`→50 °C, `map_max_mbar`→130 °C) puis on émet **Motor_09
(0x647)** sur le bus combiné. Le MAP est lu par **polling UDS** sur le bus OBD2.

---

## 5. ⚠️ RÈGLES ABSOLUES (sécurité — ne jamais violer)

1. **`block_airbag` est forcé ON en dur** au boot (`settings.cpp`, garde v2.3.3).
   Jamais de TX sur les IDs airbag **0x040 / 0x572**. Pas de case UI pour le désactiver.
2. **Aucun ID CAN deviné n'émet par défaut.** Toute émission doit reposer sur un ID
   **confirmé sur le véhicule** ou être **armée explicitement** par l'utilisateur.
3. **Le port série USB-C reste 100 % actif en parallèle du WiFi.** Le WiFi est purement
   additif ; il ne doit jamais casser le chemin série.
4. **5 s de listen-only au boot** avant toute première TX (protection du bus).
5. **Avertissement modal** (confirm) avant toute action destructrice/critique
   (désactiver TX, activer/désactiver bench mode, clear DTC, etc.).
6. **Préserver les réglages utilisateur entre flashs** : ne **pas** bumper
   `SETTINGS_VERSION` sauf nécessité réelle (un bump force un reset NVS one-shot).

---

## 6. Convention de versioning (DOIT rester uniforme partout)

À chaque release, mettre à jour **tous** ces points avec la même version `vX.Y.Z` :

1. En-tête `BOT32/BOT32.ino` (ligne 2).
2. En-tête `BOT32-Haldex/BOT32-Haldex.ino` (ligne 2).
3. `BUILD_VERSION` dans `BOT32/serial_proto.cpp`.
4. Chaîne `version` dans `BOT32/wifi_ui.cpp` (`handle_status`).
5. Tag git (`git tag vX.Y.Z`) + push + release GitHub.
6. L'UI affiche la version via l'évènement `boot` (pill « fw: vX.Y.Z ») — vérifier la cohérence.

---

## 7. Pattern de symétrie des settings (7 couches)

Tout réglage persistant doit exister, **cohérent**, dans ces 7 endroits :

`struct Settings` (settings.h) ↔ `make_defaults()` ↔ chargement NVS (`settings_init`) ↔
`settings_set_*()` ↔ `settings_reset_to_defaults()` ↔ `emit_settings()` (serial_proto.cpp) ↔
`serial_proto_apply_setting()` (dispatcher partagé série + HTTP) ↔ UI (`app.js` `SETTING_KEYS` + `index.html`).

- NVS : namespace `"bot32"`, clés courtes (ex. `obd_hz`, `blk_ab`, `bch_en`).
- `SETTINGS_VERSION` actuel = **18**.
- Le dispatcher `serial_proto_apply_setting(key, value)` est la **source unique** utilisée à
  la fois par la commande série JSON `{"cmd":"set"}` et par l'endpoint WiFi `/api/set`.

---

## 8. IDs CAN & DIDs confirmés (véhicule)

**Bus combiné (CAN0) :**
| ID | Nom | Usage |
|---|---|---|
| `0x647` | Motor_09 | TX coolant override (boost) / sniff réel |
| `0x394` | WBA_03 | RX levier de vitesse |
| `0x30B` | KOMBI_01 | RX frein à main — `byte[2] bit 7` (0x80=engagé) |
| `0x5BF` | MFSW | RX bouton OK volant — `byte[0]` (0x00=pressé) |
| `0x366` | Blinkmodi_01 | RX bouton Hazard — `byte[2] bit 4` (0x10=ON) |
| `0x0FD` | ESP_21 | RX bouton Traction Control — `byte[6]` (0x03=tenu) |
| `0x040`, `0x572` | Airbag | ❌ INTERDIT en TX |

**Bus OBD2 (CAN1) — UDS ReadDataByIdentifier (SID 0x22 → réponse 0x62) :**
| Requête→Réponse | ECU | DID | Valeur |
|---|---|---|---|
| `0x7E0`→`0x7E8` | Moteur | `0x39C0` | MAP (mbar, 16-bit) |
| `0x7E0`→`0x7E8` | Moteur | `0xF452` | Éthanol (raw·100/255 = %) |
| `0x7E0`→`0x7E8` | Moteur | `0x40D5` | EGT °C = `((d4<<8)|d5) − 250` |
| `0x7E1`→`0x7E9` | DSG (TCM) | `0x2104` | Huile DSG °C = `d4` |
| `0x70F`→`0x779` | Haldex | `0x2BF3` | Degré de blocage (16-bit) |
| `0x700` (broadcast) | tous OBD-II | Mode 04 | Clear DTC émissions |

- Polling **round-robin 5 slots** (`obd2.cpp`), défaut `obd2_poll_hz = 30` (~6 Hz/slot).
- Sentinelle des températures = **-1000.0f** (les vraies temps peuvent être négatives).
- Sniffer OK (0x5BF) : repos = `byte[0] == 0x00`, **pressé = `byte[0] != 0x00`** (fix v2.10.0).

---

## 9. Données live exposées (PC + mobile) — 12 cellules

Levier · MAP · Coolant override (TX) · Coolant réel (sniff) · Éthanol % · Haldex blocage % ·
Huile DSG °C · EGT °C · Frein à main · Bouton OK · Hazard · Traction Control.
(+ statut WiFi.) Les sniffers sont **passifs** (lecture seule, aucune action firmware).
Le Traction Control est affiché **ON** (relâché = TC actif) / **OFF** (bouton tenu = TC désactivé).
(Huile moteur retirée en v2.10.0.)

---

## 10. Build / flash + ⚠️ piège du cache Arduino

L'Arduino IDE 2.x **copie** le sketch dans un cache avant de compiler :
- Build cache : `C:\Users\<user>\AppData\Local\arduino\sketches\<HASH>\sketch\`
- Language server : `C:\Users\<user>\AppData\Local\Temp\arduino-language-server*\`

**Piège connu (mai 2026)** : si un fichier source est **supprimé** (ex. `cluster_override.cpp`
retiré en v2.9.0), l'IDE peut **garder l'ancienne copie en cache** et continuer à la compiler,
provoquant des erreurs sur un fichier qui « n'existe plus ». **Solution** :
1. Fermer **complètement** l'Arduino IDE (libère les locks + onglets fantômes).
2. Supprimer le dossier de build cache du sketch + les dossiers `arduino-language-server*`.
3. Rouvrir le sketch → compilation propre (full rebuild).

---

## 11. Workflow de livraison d'une release

1. Implémenter + maintenir la **symétrie settings** (§7) et la **sécurité** (§5).
2. Mettre à jour **tous** les marqueurs de version (§6).
3. Audit final (compile mental, includes, références mortes, symétrie, marqueurs).
4. `git add -A` → commit (message `vX.Y.Z — résumé`) → `git tag vX.Y.Z` → `git push` + `git push --tags`.
5. Créer la release GitHub (`gh release create vX.Y.Z`).
6. Identité commit : `Antoine Lagrandeur <antoine@enerserv.ca>`.

---

## 12. Langue & style

- **Répondre en français.**
- Avant une modif large : relire les fichiers concernés (ne pas présumer l'état).
- Tester la cohérence bout-en-bout : `config.h` → module → `serial_proto` status → `app.js` (PC) → `wifi_ui` (mobile).
