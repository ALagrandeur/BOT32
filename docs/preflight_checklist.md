# BOT32 — Pre-flight checklist (à faire avant le 1er test)

Suis cette checklist **dans l'ordre** avant de connecter quoi que ce soit au véhicule.

---

## ☐ Phase 1 — Setup PC (5 min)

### ☐ 1.1 Arduino IDE installé
- Version 2.x recommandée
- [Téléchargement](https://www.arduino.cc/en/software)

### ☐ 1.2 ESP32 board support installé
- File → Preferences → Additional Boards URLs :
  `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
- Tools → Board → Boards Manager → recherche "esp32" → install "esp32 by Espressif Systems" (>= 3.x)

### ☐ 1.3 Bibliothèques Arduino installées
- Tools → Manage Libraries → install :
  - [x] **ACAN2515** par Pierre Molinaro (version 2.1.x ou plus)
  - [x] **ArduinoJson** par Benoit Blanchon (version **7.x** — pas 6.x !)

### ☐ 1.4 Python installé (pour la web UI)
- Version 3.10+ recommandée
- `python --version` doit répondre

### ☐ 1.5 Cloner le repo
```powershell
cd C:\Users\AntoineLagrandeur
git clone https://github.com/ALagrandeur/BOT32.git
```

(ou pull si déjà cloné)

### ☐ 1.6 Web UI Python deps installés
```powershell
cd C:\Users\AntoineLagrandeur\BOT32\webui
pip install -r requirements.txt
```

### ☐ 1.7 Raccourci bureau créé
- Le raccourci `BOT32` sur ton bureau OneDrive doit pointer vers `webui\run.bat`
- (Déjà fait par l'assistant via le shortcut script)

---

## ☐ Phase 2 — Compilation à blanc (5 min, **AVANT** de câbler quoi que ce soit)

### ☐ 2.1 Ouvre le sketch
- Arduino IDE → File → Open → `C:\Users\AntoineLagrandeur\BOT32\BOT32\BOT32.ino`
- Tu dois voir 9 onglets : `BOT32.ino`, `config.h`, `can_handler.*`, `coolant.*`, `lever_decoder.*`, `obd2.*`, `serial_proto.*`, `settings.*`, `vw_mqb.*`

### ☐ 2.2 Configure la board
- Tools → Board → **ESP32 Dev Module** (peu importe la marque)
- Tools → Upload Speed → 921600 (ou 460800 si problème)
- Tools → Flash Size → 4 MB
- Tools → Partition Scheme → Default 4 MB with spiffs
- Tools → Port → (ne sélectionne rien pour l'instant)

### ☐ 2.3 Compile (verify)
- Click sur ✓ (Verify) ou Ctrl+R
- **Attendu** : "Compilation terminée" (Done compiling), sans erreur
- Si erreur :
  - "ACAN2515.h: No such file" → lib pas installée, retour étape 1.3
  - "ArduinoJson.h: No such file" → idem
  - "JsonDocument is not a member" → mauvaise version d'ArduinoJson (besoin v7, pas v6)
  - Autre erreur → copie-colle le message, on debug

→ **Si compile OK : ✅ ton code est sain. Passe à la phase 3.**

---

## ☐ Phase 3 — Hardware physique (15 min)

### ☐ 3.1 Vérif HAT WaveShare
- Repère sur le HAT les jumpers suivants :
  - [ ] **Jumper VIO** (en bas à gauche sur ta photo) → position **3V3**
  - [ ] **Jumper 120R CAN0** (côté droit) → position **ON** (pour bench seul ; OFF en voiture)
  - [ ] **Jumper 120R CAN1** → position **ON** (pour bench seul ; OFF en voiture)

### ☐ 3.2 Câblage 10 fils Dupont F-F (HAT GPIO header → ESP32)

| Pin HAT (header Pi) | Signal | → ESP32 GPIO |
|---|---|---|
| **1** | 3V3 | 3V3 |
| **6** | GND | GND |
| **19** | MOSI | **GPIO 23** |
| **21** | MISO | **GPIO 19** |
| **23** | SCK | **GPIO 18** |
| **24** | CS0 | **GPIO 5** |
| **26** | CS1 | **GPIO 25** |
| **22** | INT0 | **GPIO 4** |
| **18** | INT1 | **GPIO 26** |

→ Tous les 10 fils sont raccordés (compte-les visuellement) : [ ] **9 signaux + GND vérifiés**

### ☐ 3.3 Alimentation ESP32
- Pour le bench : USB depuis PC (5V) — suffit pour ESP32 + HAT
- Pas besoin d'alim externe pour les tests initiaux

### ☐ 3.4 Vérif visuelle
- [ ] LED rouge `PWR` du HAT allumée quand l'ESP32 est sous tension
- [ ] Aucun fil croisé (MOSI/MISO/SCK ne se touchent pas)
- [ ] Pas de court-circuit visible (3V3 et GND séparés)

---

## ☐ Phase 4 — Premier flash et vérification (5 min)

### ☐ 4.1 Branche l'ESP32 en USB
- Windows doit détecter un nouveau port COM (CP210x ou CH340)
- Si pas reconnu : installer le driver USB-Serial du fabricant

### ☐ 4.2 Sélectionne le port
- Arduino IDE → Tools → Port → **COM**X (le nouveau)

### ☐ 4.3 Upload
- Click sur → (Upload) ou Ctrl+U
- Attendre "Done uploading"

### ☐ 4.4 Ouvre Serial Monitor
- Tools → Serial Monitor (ou Ctrl+Shift+M)
- **Baud rate** : **115200** (en bas à droite)
- Appuie sur EN/RST sur l'ESP32 pour redémarrer

### ☐ 4.5 Vérifie l'output attendu
Tu DOIS voir dans Serial Monitor :

```
================================
  BOT32 - boost gauge override
================================
[NVS] Settings loaded from flash
[CAN] cluster MCP2515 started at 500 kbps (16 MHz xtal)
[CAN] obd2 MCP2515 started at 500 kbps (16 MHz xtal)
[main] Entering BOOT
{"evt":"boot","version":"0.1","build":"..."}
{"evt":"settings", ...}
{"evt":"status", "mode":"BOOT", ...}
```

| Résultat | Diagnostic |
|---|---|
| ✅ Les 2 lignes "MCP2515 started" apparaissent | **Tout bon, hardware OK** |
| ❌ "MCP2515 begin FAILED, error 0x..." | Câblage SPI faux, ou VIO pas en 3V3 |
| ❌ Boot loop / reset constant | Court-circuit alim, mauvais pin |
| ❌ Rien dans Serial | Port COM faux, baud rate faux |

---

## ☐ Phase 5 — Web UI (5 min)

### ☐ 5.1 Lance le serveur
- Double-clic sur le raccourci **BOT32** sur le bureau
- OU : `cd C:\Users\AntoineLagrandeur\BOT32\webui` puis `python server.py`

### ☐ 5.2 Ouvre le navigateur
- Va sur [http://127.0.0.1:5000](http://127.0.0.1:5000)

### ☐ 5.3 Vérifie l'auto-connexion
- La pill en haut à droite doit passer de "disconnected" (rouge) à "connected: COM..." (vert)
- Si non : Refresh ports → sélectionne ton COM → Connect

### ☐ 5.4 Live status
- Tu dois voir :
  - **Mode** : "BOOT" en orange (pendant 5 sec) puis "SILENT" en vert
  - **Lever** : "—" (normal, pas de bus connecté)
  - **MAP** : "—" (normal, pas de bus connecté)
  - **Coolant byte** : "0x80 (50.0°C)"
- Bus stats : tu vois 0 partout (normal sans bus actif)

### ☐ 5.5 Settings éditables
- Modifie un slider (ex: MAP min) → tu dois voir un message ack dans le log
- Refresh la page → la valeur est persistée (NVS)

### ☐ 5.6 Frame monitor (optionnel pour ce test)
- Coche "Subscribe to live frames"
- Tu ne verras rien (pas de bus actif) — normal

---

## ☐ Phase 6 — Test avec un vrai bus CAN (optionnel — bench seul, sans véhicule)

Si tu as un autre device CAN capable de générer du traffic à 500 kbps :

### ☐ 6.1 Câble vis CAN0
- HAT CAN0 H ↔ autre device CAN-H
- HAT CAN0 L ↔ autre device CAN-L
- HAT CAN0 G ↔ autre device GND

### ☐ 6.2 Sniff
- Dans la web UI : coche "Subscribe to live frames"
- Tu dois voir les trames de l'autre device apparaître

### ☐ 6.3 Test TX
- Pour forcer un TX BOOST simulé : tu peux temporairement éditer `lever_decoder.cpp` pour retourner `'S'` directement, ou injecter une trame WBA_03 0x394 avec byte1=0x50

(C'est optionnel — pas nécessaire si tu vas direct à la voiture.)

---

## ☐ Phase 7 — Installation véhicule (15 min, **après bench validé**)

### ☐ 7.1 Voiture cléf OFF
### ☐ 7.2 **CHANGER les jumpers 120R du HAT : ON → OFF** (le bus voiture a déjà ses terminateurs)
### ☐ 7.3 Câble OBD-II
- Connecteur OBD2 mâle (J1962) :
  - pin 4 ou 5 (GND) → BOT32 GND (et alim si tu utilises pin 16)
  - pin 6 (CAN-H) → HAT bornier **CAN1** vis **H**
  - pin 14 (CAN-L) → HAT bornier **CAN1** vis **L**
  - pin 16 (+12V) → optionnel, via LM2596 5V → ESP32 VIN (sinon USB depuis ordi portable)

### ☐ 7.4 Câble cluster
- Localise le connecteur 18 pins derrière le tableau de bord
- Avec un T-tap (sans couper le fil) :
  - pin **17** (CAN-H cluster) → HAT bornier **CAN0** vis **H**
  - pin **18** (CAN-L cluster) → HAT bornier **CAN0** vis **L**

### ☐ 7.5 Cléf ON moteur OFF
- ESP32 boot
- Web UI (si laptop branché) : tu dois voir bus stats RX qui montent (cluster broadcaste WBA_03, gateway broadcaste Wake + Engine_Code + ESP/TSK + Airbag etc.)
- Mode passe à SILENT (lever en P) ou BOOST si déjà en S/M/N

### ☐ 7.6 Test BOOST sans démarrer
- Met le levier en S
- Mode doit passer à BOOST
- OBD2 polling commence (tu vois `obd2.tx_ok` qui monte)
- **Aiguille coolant ne bouge probablement PAS** car moteur OFF → MAP = 1000 mbar pression atmosphérique constante

### ☐ 7.7 Test BOOST moteur tournant
- Démarre le moteur
- Met en S
- L'aiguille devrait remonter selon le MAP (idle ~300-400 mbar, accel ~1500-2500 mbar)
- Repasse en D → aiguille redescend à la vraie temp coolant

---

## 🚨 Si quelque chose foire à la Phase 4 ou 5 (avant d'aller en voiture)

| Symptôme | Cause probable | Action |
|---|---|---|
| Compile error "ACAN2515.h: No such file" | Lib pas installée | Tools → Manage Libraries → ACAN2515 |
| Compile error "JsonDocument" | ArduinoJson v6 au lieu de v7 | Désinstalle v6, installe v7 |
| Upload error "Failed to connect" | Maintiens BOOT button enfoncé pendant l'upload | |
| LED PWR du HAT pas allumée | Alim 3V3 pas connectée du tout | Vérif câbles 3V3 et GND du HAT |
| "MCP2515 begin FAILED" | SPI cassé, VIO en 5V (grave !), ou crystal faux | Vérif jumper VIO=3V3, câblage SPI, MCP2515_CLOCK_MHZ |
| MCP2515 OK mais tx_fail monte vite | Pas de bus actif (normal en bench seul) | OK, attendu si rien de connecté |
| Web UI ne se connecte pas | Mauvais COM, ou serveur pas démarré | Sélectionne port manuellement dans UI |
| Web UI connectée mais rien ne s'affiche | Boot loop ESP32 ou Serial.begin pas appelé | Reflash + Serial Monitor |

---

## 📋 Récapitulatif rapide pour demain

1. **Arduino IDE** : ouvre `BOT32.ino`, install libs (ACAN2515 + ArduinoJson v7), board ESP32 Dev Module, **compile**
2. **HAT** : jumper VIO=3V3, jumpers 120R=ON (bench seul) ou OFF (voiture), 10 fils Dupont selon table
3. **Flash** : Upload, Serial Monitor @ 115200
4. **Vérif** : 2x "MCP2515 started at 500 kbps (16 MHz xtal)" doivent apparaître
5. **Web UI** : raccourci BOT32 sur bureau → navigateur sur localhost:5000
6. **Test** : modifie un setting, vérifie qu'il est persisté

Si tout OK jusqu'ici → tu peux brancher au véhicule.

Si problème → envoie-moi le **Serial Monitor output** et le **comportement de l'UI**, on debug.

Bonne chance !
