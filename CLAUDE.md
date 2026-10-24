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
| Module Haldex MITM | **dépôt privé séparé** `BOT32-HALDEX` (carte ESP32-CAN-X2) — hors de ce repo |
| UI PC | `webui/` — Flask + SocketIO (`server.py`, `static/index.html`, `app.js`, `style.css`) — mode d'emploi : `webui/README.md` |
| UI mobile | servie en PROGMEM depuis `BOT32/wifi_ui.cpp` (`MOBILE_HTML`) |
| Docs | `docs/` — à jour : `haldex_integration.md`, `wiring_waveshare_hat.md` ; historiques (bandeau en tête) : `wiring.md`, `hardware.md`, `future_features.md`, `savvycan_capture_guide.md`, `preflight_checklist.md` |
| Captures véhicule (SavvyCAN) | `DATA input from CAR/*.csv` |
| Outils | `tools/generate_bench_frames_cpp.py` (génère `BOT32/bench_frames.cpp` ; chemins Windows codés en dur, capture source absente du repo) · `tools/verify_crc.py` (vérif CRC ponctuelle) · `tools/derive_bench_frames.py` (ancien, remplacé par `generate_bench_frames_cpp.py`) |

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
BOOT        (5 s au démarrage : aucune TX Motor_09 sur le bus combiné ; le polling UDS OBD2 tourne déjà avec les réglages par défaut)
SILENT      (levier P/R/N/D : pas de TX, le combiné affiche le vrai coolant)
BOOST       (levier S/M : poll MAP via OBD2, TX Motor_09 override boost)
SAFE_FAULT  (échec d'init MCP2515 au boot : aucune TX — `tx_enabled = false` coupe la TX sans changer de mode)
BENCH_TEST  (combiné sur établi sans véhicule : émet le bundle complet)
```

Fonction cœur : **boost-on-coolant** — on calcule un octet « coolant » à partir du MAP
(mapping linéaire `map_min_mbar`→50 °C, `map_max_mbar`→130 °C) puis on émet **Motor_09
(0x647)** sur le bus combiné. Le MAP est lu par **polling UDS** sur le bus OBD2.

---

## 5. ⚠️ RÈGLES ABSOLUES (sécurité — ne jamais violer)

1. **`block_airbag` est forcé ON en dur** au boot (`settings.cpp`, garde v2.3.3).
   Jamais de TX sur les IDs airbag **0x040 / 0x572**. Pas de case UI pour le désactiver.
   ⚠ La clé `block_airbag` reste modifiable **à chaud** par la commande série `set` et par
   `/api/set` (dispatcher `serial_proto_apply_setting`) jusqu'au prochain reboot.
2. **Aucun ID CAN deviné n'émet par défaut.** Toute émission doit reposer sur un ID
   **confirmé sur le véhicule** ou être **armée explicitement** par l'utilisateur.
3. **Le port série USB-C reste 100 % actif en parallèle du WiFi.** Le WiFi est purement
   additif ; il ne doit jamais casser le chemin série.
4. **5 s de listen-only au boot** avant toute première TX (protection du bus).
   ⚠ État réel du code (v4.6.0) : la phase `BOOT` bloque seulement l'override Motor_09 sur le
   bus combiné ; le polling UDS sur le bus OBD2 démarre dès le boot quand `poll_ethanol` ou
   `poll_haldex_blockage` est ON (réglages par défaut) — voir `loop()` dans `BOT32.ino`.
5. **Avertissement modal** (confirm) avant toute action destructrice/critique
   (désactiver TX, activer/désactiver bench mode, clear DTC, etc.).
6. **Préserver les réglages utilisateur entre flashs** : ne **pas** bumper
   `SETTINGS_VERSION` sauf nécessité réelle (un bump force un reset NVS one-shot).

---

## 6. Convention de versioning (DOIT rester uniforme partout)

À chaque release, mettre à jour **tous** ces points avec la même version `vX.Y.Z` :

1. En-tête `BOT32/BOT32.ino` (ligne 2).
2. *(BOT32-Haldex déplacé dans le dépôt privé `BOT32-HALDEX` — versionné indépendamment.)*
3. `BUILD_VERSION` dans `BOT32/serial_proto.cpp`.
4. Chaîne `version` dans `BOT32/wifi_ui.cpp` (`handle_status`).
5. Tag git (`git tag vX.Y.Z`) + push + release GitHub.
6. L'UI affiche la version via l'évènement `boot` (pill « fw: vX.Y.Z ») — vérifier la cohérence.

> État au 2026-10-12 : pour **v4.6.0**, le tag existe en local mais **n'a pas été poussé** et
> aucune release GitHub n'a été créée (dernière release : v4.1.0 ; tags distants jusqu'à v4.5.0).

---

## 7. Pattern de symétrie des settings (7 couches)

Tout réglage persistant doit exister, **cohérent**, dans ces 7 endroits :

`struct Settings` (settings.h) ↔ `make_defaults()` ↔ chargement NVS (`settings_init`) ↔
`settings_set_*()` ↔ `settings_reset_to_defaults()` ↔ `emit_settings()` (serial_proto.cpp) ↔
`serial_proto_apply_setting()` (dispatcher partagé série + HTTP) ↔ UI (`app.js` `SETTING_KEYS` + `index.html`).

- NVS : namespace `"bot32"`, clés courtes (ex. `obd_hz`, `blk_ab`, `bch_en`).
- `SETTINGS_VERSION` actuel = **18**.
- Exception voulue (v4.6.0) : les réglages `cef_*` (ancien déclencheur Hazard ×3, jamais codé) restent dans le firmware/NVS mais sont retirés de l'UI PC — pas de bump.
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
| `0x3C0` | Klemmen_Status_01 | RX contact — `byte[2] bit 1` = Kl15 (0x00 → 0x03 à la mise du contact, capture `1.csv`) — v4.8.0 |
| `0x040`, `0x572` | Airbag | ❌ INTERDIT en TX |

**Bus OBD2 (CAN1) — UDS ReadDataByIdentifier (SID 0x22 → réponse 0x62) :**
| Requête→Réponse | ECU | DID | Valeur |
|---|---|---|---|
| `0x7E0`→`0x7E8` | Moteur | `0x39C0` | MAP (mbar, 16-bit) |
| `0x7E0`→`0x7E8` | Moteur | `0xF452` | Éthanol (raw·100/255 = %) |
| `0x7E0`→`0x7E8` | Moteur | `0x40D5` | EGT °C = `((d4<<8)|d5) − 250` |
| `0x7E1`→`0x7E9` | DSG (TCM) | `0x2104` | Huile DSG °C = `d4` |
| `0x70F`→`0x779` | Haldex | `0x2BF3` | Degré de blocage (16-bit) |
| `0x7E0`→`0x7E8` | Moteur | Mode 04 `[01 04]` | **v4.6.0** : effacement codes MOTEUR seulement (procédure écoute seule) — confirmé en voiture (v4.6.0 stable) |
| `0x700` (broadcast) | moteur + DSG | Mode 04 | Plus utilisé depuis v4.6.0 (efface aussi la DSG) |

- **v4.6.0 — Effacement des codes moteur** (`obd2_clear_engine_fault`, bouton web PC + mobile) : `can_set_quiet(true)` bloque TOUTE émission BOT32 sur les deux bus (override Motor_09, polling OBD, bench) → Haldex en STOCK → 300 ms → Mode 04 `[01 04]` vers **0x7E0 seulement** (`can_send_quiet_exempt`) → attente 0x7E8 (`0x44` = OK, `7F 04 78` = en attente, autre `7F` = refus, 2 s sans réponse = échec) → reprise. Le Haldex reste en STOCK. Résultat dans le status : `clear_engine_state/msg/seq/src`.
- **v4.8.0 — Haldex en STOCK à chaque redémarrage** (`ignition_watch.cpp`, à valider en voiture) : `haldex_modes_force_stock()` au démarrage de BOT32, à chaque changement de Kl15 (Klemmen_Status_01 `0x3C0` octet 2 bit 1 — confirmé `00`→`03` à la mise du contact, capture `1.csv`) et au réveil du bus cluster après ≥ 3 s de silence ; STOCK renvoyé toutes les 500 ms (`haldex_link_send_forced_stock(false)`, sans log) **jusqu'à ce que le STATE du X2 rapporte STOCK** — y compris pendant que le X2 dort (ESP-NOW coupé) : abandon après 10 s de liaison sans STOCK ou 30 min sans aucun STATE (`FORCE_STOCK_WINDOW_MS` / `FORCE_STOCK_MAX_MS`). Tous les retours en STOCK passent par `force_stock_start()` : bouton STOCK app/USB et effacement des codes (`haldex_modes_set_manual(0)`), TC remis après un combo, désactivation de la liaison (dernier STOCK, 10 s max ; seul `haldex_link_send_forced_stock()` passe liaison désactivée — `haldex_link_set_mode()` refuse tout — et **seulement si ESP-NOW tourne déjà** — `haldex_espnow_ready()`, ne jamais lancer ESP-NOW liaison désactivée : `WiFi.mode(WIFI_STA)` couperait le point d'accès). Annulé si l'app ou un combo choisit un mode (une commande AMOLED n'est pas vue par BOT32). Une coupure radio du X2 n'envoie jamais STOCK. Liaison activée en cours de route : `wifi_ui_haldex_link_on()` ajoute le STA à l'AP en place (AP → AP+STA, sans redémarrer l'AP) puis `haldex_espnow_init()` ; `softAPdisconnect(true)` depuis AP seul coupe le pilote WiFi et efface ESP-NOW → `stop_ap()` appelle `haldex_espnow_invalidate()` ; `haldex_espnow_init()` garde un AP en marche (AP → AP+STA) ; `haldex_espnow_ready()` exige le STA. **Le X2 de la voiture reste en v3.0.0** (commit `f9e789a`, pas de reflash) : il ne revient jamais en STOCK de lui-même (mode en RAM, gardé en veille), donc ce retour repose **entièrement** sur BOT32 avec `haldex_enabled` ON. X2 v3.1.0 (HEAD du dépôt privé, non flashé) = filet optionnel : STOCK après 5 s sans trame CAN. La manette AMOLED n'est pas utilisée.
- **v4.8.0 — Fraîcheur du combo en « temps de lecture »** (`haldex_modes.cpp`, `track_combo_frames()`) : chaque `can_poll()` ajoute au plus `COMBO_POLL_STEP_MS` (100 ms) au temps sans trame de hazard / TC / frein à main ; une trame lue à la dernière lecture remet à 0. Une pause de `loop()` qui fait déborder le tampon ACAN2515 (32 trames, les nouvelles sont jetées) ne fait donc plus retomber un combo en STOCK ; une vraie perte du bus oui (`COMBO_FRESH_MS` = 2 s de lecture). Le renvoi STOCK d'`ignition_watch` exige `haldex_enabled` (désactivé par défaut).
- **v4.8.0 — Déclencheur au volant = OK MAINTENU 6 s** (`ok_clear.cpp`, remplace OK × 10 de v4.7.0) : maintien mesuré depuis le front d'appui, relâcher annule, un déclenchement par appui, 10 s d'inhibition.
- **v4.7.0 — Déclencheur au volant (remplacé en v4.8.0)** (`ok_clear.cpp`, **à valider en voiture** ; v4.6.0 = dernière stable) : **OK appuyé 10 fois en moins de 6 s** → `obd2_clear_engine_start("bouton OK")`. Fronts comptés trame par trame dans `button_sniffer` (`button_sniffer_ok_press_events()`), 10 s d'inhibition après un déclenchement. Bandeau sur la page principale du cellulaire seulement si elle est ouverte au moment de l'effacement.
- Polling **round-robin 6 slots** (`obd2.cpp`), défaut `obd2_poll_hz = 30` (~5 Hz/slot).
- Sentinelle des températures = **-1000.0f** (les vraies temps peuvent être négatives).
- Sniffer OK (0x5BF) : repos = `byte[0] == 0x00`, **pressé = `byte[0] != 0x00`** (fix v2.10.0).

**Haldex (MITM) — principal v4.6.0 STABLE (confirmée en voiture le 2026-10-12) / X2 v3.0.0 — 3 modes CONFIRMÉS EN VOITURE (jalons stables : v4.0.0 / X2 v2.0.0, puis X2 v3.0.0 = 50-50 tenu à toutes les vitesses) :**

> ⚠ Les puces « CRC E2E / trames modifiées », « v1.6.0 » et « Burnout FWD » ci-dessous
> datent du X2 v0.4–v2.0 et sont **conservées telles quelles (historique)**. Le détail de la
> recette du X2 **v3.0.0** est dans le dépôt privé `BOT32-HALDEX` : s'y référer. **Le X2 de la voiture
> reste en v3.0.0** (commit `f9e789a`) ; le HEAD v3.1.0 du dépôt privé n'est pas flashé (optionnel) —
> lire `git show f9e789a:<fichier>` pour le comportement réel.
- Module MITM = **dépôt privé `BOT32-HALDEX`** sur carte **ESP32-CAN-X2** (CAN1 TWAI natif = côté voiture, CAN2 MCP2515 = côté Haldex, **TERM2 120 Ω activée côté Haldex**, 500 kbps). Pont CAR↔Haldex dans une **tâche FreeRTOS avant WiFi** (évite l'écart de boot). **12 V permanent** + light-sleep 15 min + wake-on-CAN (jamais de sommeil tant qu'un hôte USB est branché).
- Lien **ESP-NOW uniquement**. **Fiabilité (v3.9.0/v1.5.0)** : les deux côtés **verrouillent le canal 1** (`esp_wifi_set_channel`) ET **désactivent le power-save WiFi** (`WiFi.setSleep(false)` + `esp_wifi_set_ps(WIFI_PS_NONE)`) — sans ça la radio STA dort et jette les paquets reçus (c'était LA cause d'un lien mort). Diagnostics : `espnow_rx` (JSON + bandeau UI), ligne X2 `[5s] espnow ch= tx= rxcmd=`.
- 3 modes : STOCK/FWD/50-50. FWD = combo **Hazards ON + TC OFF** (ou app) ; **v3.10.0 : sort quand le TC repasse à ON** (plus quand les Hazards s'éteignent → on peut couper les warnings et garder FWD). On lit l'état **latché** du TC (0x0FD byte6==0x03 stable). 50-50 = **v4.6.0 : même combo avec le frein à main TIRÉ au moment où il se complète** (KOMBI_01 0x30B byte[2] bit 7, échantillonné une seule fois ; frein baissé = FWD) ; un mode armé par le combo ne change plus tant que le TC reste OFF, et sort quand le TC repasse à ON. 50-50 aussi par l'app (sort via STOCK). **Pas d'auto-revert, pas de seuil de pédale.**
- **CRC E2E AUTOSAR** sur `[D2..D8, DataID]` — **DataID APPENDÉ** (confirmé 100 % sur 5 logs). Trames modifiées (FWD ET 50-50) : **0x08A D8 + 0x0A7 D6/D7** (couple — la demande de lock) + 0x116/0x106 (ESP) + 0x0B2 (roues). **FWD → demande 0x00 + roues 0 ; 50-50 → demande 0xFA** (0xFE/0xFF = SNA/réservé écartés ; **vérifié run3 : 0x08A D8 est constant 0xFA sur la voiture, jamais 0xFE** — contredit le 0xFE d'OpenHaldex) + roues = vitesse réelle.
- **v1.6.0 : 0x0A8 (MOTOR_12) en PASSTHROUGH** — son byte[7]/D8 est le signal **RPM moteur** (confirmé run3 : 81 valeurs distinctes), PAS la demande de lock (le compteur est en D2). On le forçait à tort. La demande vit uniquement dans 0x08A + 0x0A7.
- **Passthrough** = armement : OFF = MITM armé. **Le X2 PERSISTE `passthrough` en NVS** ; le **mode boot TOUJOURS STOCK** (sécurité). 1er boot = STOCK + passthrough ON. ⚠ s'il est coupé ARMÉ, il revient ARMÉ (mais en STOCK = inerte). L'UI affiche le **mode réel rapporté par le X2** + l'**ack** des commandes.
- **Burnout FWD (v0.4.0)** : en FWD armé, le X2 falsifie aussi **ESP_19 (0x0B2)** = les 4 vitesses de roue (présente les 4 à la vitesse réelle véhicule, lue sur ESP_21 0x0FD, `roue=veh*4/3`) → le Haldex (embrayage ouvert) voit une voiture cohérente, pas de défaut. ESP_19 est full-packed → pas de CRC à recalculer. ⚠ ne trompe que le Haldex ; l'ESP/ABS amont se neutralise par le bouton TC/ESP.
- Côté principal : module `haldex_modes` (logique modes + passthrough), `haldex_link`/`haldex_espnow` (transport ESP-NOW).
- **2e source de SET_MODE : la manette AMOLED** (dépôt `Remote32`, `Recepteur_AMOLED`, mode Golf) écoute le STATE et diffuse **SET_MODE seulement, jamais SET_PASSTHROUGH** (l'armement reste réservé à l'app du principal). Le principal affiche le mode rapporté par le X2 et renvoie toujours un STOCK explicite (`haldex_modes_set_manual`), même si son propre état est déjà STOCK.
- Témoin frein à main pour indiquer le mode = **abandonné** (frein mécanique → pas de trame CAN d'entrée). Le mode se lit dans l'UI web.

---

## 9. Données live exposées (PC + mobile) — 12 cellules

Levier · MAP · Coolant override (TX) · Coolant réel (sniff) · Éthanol % · Haldex blocage % ·
Huile DSG °C · EGT °C · Frein à main · Bouton OK · Hazard · Traction Control.
(+ statut WiFi.) Les sniffers sont **passifs** (lecture seule sur le bus) ; Hazard, TC et frein à main servent d'entrées aux combos Haldex (`haldex_modes`).
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
