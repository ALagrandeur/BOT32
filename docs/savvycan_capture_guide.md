# SavvyCAN — Guide de capture VW MQB powertrain (Haldex hunt)

Workflow pour identifier la présence du Haldex sur le bus moteur MK7 et
décoder ses paramètres en utilisant SavvyCAN + le DBC fourni dans
`docs/vw_mqb_powertrain.dbc`.

## 🔌 Setup matériel

| Item | Notes |
|---|---|
| Hardware CAN | CANable v2, M2RET, OBDLink MX+, ou ESP32 SavvyCAN firmware |
| Connexion | T-tap sur la paire CAN-H/CAN-L derrière le PCM (J623) |
| Baudrate | **500 kbps** (HS-CAN powertrain MQB) |
| Mode | **LISTEN-ONLY** (passive monitor) — aucun TX |
| Terminaison | NE PAS ajouter de 120Ω, le bus a déjà ses terminateurs OEM |

## 📥 Charger le DBC dans SavvyCAN

1. Lance SavvyCAN
2. `File → Load DBC File` → sélectionne `docs/vw_mqb_powertrain.dbc`
3. Une fois chargé, dans les fenêtres frame view, tu verras les **noms** au lieu des IDs hex (HALDEX_GEN5_STATUS au lieu de 0x118, MOTOR_11 au lieu de 0x0A7, etc.)
4. Le panneau "Signals" décodera automatiquement les bytes en valeurs interprétées (RPM, vitesse roues, pump engagement %, etc.)

## 🎬 4 scénarios de capture

### Scénario 1 — Baseline (10 secondes)

**But** : Identifier toutes les IDs présentes + leur fréquence.

1. Démarre la voiture, levier en P, frein de service appuyé, ralenti chaud
2. SavvyCAN : `Connection → Connect`, vérifie que les frames défilent
3. `Capture → Start Capture` (10 secondes)
4. `Capture → Stop Capture`
5. `Tools → ID Bucket Analyzer` → tu obtiens la liste de toutes les IDs avec leur fréquence Hz

**Ce que tu cherches** :
- `0x118 HALDEX_GEN5_STATUS` à ~50-100 Hz → ✅ Haldex confirmé sur ce bus
- `0x0A7 MOTOR_11`, `0x0A8 MOTOR_12`, `0x08A ESP_14`, `0x0B2 ESP_19` → frames PCM source
- `0x394 WBA_03` → levier (déjà connu)
- `0x107 MOTOR_04` → RPM

Sauvegarde : `File → Save Loaded File As → vw_mqb_baseline_idle.log`

### Scénario 2 — Lever scan (45 secondes)

**But** : Confirmer le décodage du levier + voir si le Haldex change avec la position.

1. Démarre nouvelle capture
2. Levier (5s entre chaque) : P → R → N → D → S → M → retour P
3. Stop capture
4. Filtre sur ID 0x394 (`Frame Filter → Show only 0x394`)
5. Tu dois voir `WBA_03_LeverPosition` qui change : 1 → 2 → 3 → 4 → 5 → 6 → 1
6. Filtre sur ID 0x118 (HALDEX) :
   - `HDX_PumpEngagement_pct` devrait rester proche de 0 (voiture stationnaire)
   - `HDX_State_CouplingOpen` devrait rester à 1 (coupling fully open au ralenti)

Sauvegarde : `vw_mqb_lever_scan.log`

### Scénario 3 — Throttle blips (60 secondes)

**But** : Identifier la frame pédale d'accélération + voir le Haldex pré-charger sur demande de couple.

1. Voiture en **P** ou **N**, frein de service **fermement appuyé** (anti-bond)
2. Démarre capture
3. Pédale : 0% (5s) → 25% (3s) → 0% → 50% (3s) → 0% → 75% (2s) → 0% → 100% bref (1s) → 0% (5s)
4. Stop
5. `Tools → Graph Frames` :
   - Ajoute signal `MOTOR_11_Mom_Soll_Roh` (devrait suivre ta pédale)
   - Ajoute signal `HDX_PumpEngagement_pct` (peut bouger légèrement, le PCM précharge l'AWD sur demande de couple)
   - Ajoute signal `MOTOR_04_EngineRPM` (devrait suivre la pédale aussi)

Si tu vois `MOTOR_11_Mom_Soll_Roh` qui colle parfaitement à ta pédale → confirmé "raw target torque = pedal indication".

Sauvegarde : `vw_mqb_throttle_blips.log`

### Scénario 4 — Driving (1-2 min, parking lot)

**But** : Voir le Haldex s'engager pour de vrai.

1. Démarre capture
2. Manœuvres (selon ce qui est sécuritaire) :
   - Démarrage franc depuis arrêt en S
   - Virage serré à basse vitesse (Haldex anticipe la perte de motricité)
   - Accélération en sortie de virage
   - Crawl sur sol meuble si possible
3. Stop
4. `Tools → Graph Frames` :
   - `HDX_PumpEngagement_pct` (axe Y principal) — devrait monter à 30-80% pendant accélération depuis arrêt
   - `HDX_State_CouplingOpen` — devrait passer à 0 (engagé)
   - `ESP_19_WheelSpeed_HL/HR/VL/VR` — pour voir les différences de vitesse essieu avant vs arrière
   - `MOTOR_11_Mom_Soll_Roh` — couple demandé

Sauvegarde : `vw_mqb_driving_haldex_engage.log`

## 🔍 Analyse post-capture dans SavvyCAN

### Vue Frames Live (RX raw)
- Tu vois toutes les frames en temps réel
- Le DBC affiche le nom à droite
- Click sur une frame → décodage complet en bas

### Vue Graph Frames
- Permet de plotter plusieurs signaux dans le temps
- Idéal pour corréler 2-3 signaux (ex: pedal → torque → haldex engagement)

### Vue Frame Comparator
- Compare 2 captures côte à côte
- Utile pour voir les diff entre lever P et lever S sur les mêmes IDs

### Vue ID Bucket Analyzer
- Liste toutes les IDs vues + Hz + taille payload
- Pour repérer les frames rares (event-based)

### Filter par ID
- `Frame Filter` en haut → entre `0x118,0x0A7,0x08A,0x0B2` pour focus
- Sauve le filter en preset

## 📊 Tableau d'observations à remplir pendant analyse

| ID | Nom DBC | Vu ? | Fréquence Hz | Note |
|---|---|---|---|---|
| 0x086 | LWI_01 (steering angle) | | | |
| 0x08A | ESP_14 | | | byte 7 = Allrad demand max |
| 0x0A7 | MOTOR_11 | | | bytes 6-7 = torque demand |
| 0x0A8 | MOTOR_12 | | | |
| 0x0B2 | ESP_19 (wheel speeds) | | | |
| **0x118** | **HALDEX_GEN5** | | | **CRUCIAL** |
| 0x107 | MOTOR_04 (RPM) | | | |
| 0x101 | ESP_02 | | | yaw rate ? |
| 0x106 | ESP_05 | | | long accel ? |
| 0x116 | ESP_10 | | | |
| 0x121 | MOTOR_20 | | | |
| 0x394 | WBA_03 (lever) | | | déjà connu BOT32 |
| 0x3C0 | Klemmen_Status_01 | | | wake |
| 0x641 | MOTOR_CODE_01 | | | engine alive |

## 🎯 Réponses-clés à extraire

Après analyse, tu sauras :

1. **Le Haldex est-il sur ce bus ?** → 0x118 visible OUI/NON
2. **Pump engagement live** → `HDX_PumpEngagement_pct` valeur min/max observée
3. **Frames source côté PCM** → 0x0A7/0x0A8/0x08A/0x0B2 visibles ?
4. **Données contextuelles** → throttle %, RPM, wheel speeds, yaw, steering angle

→ Tu auras alors **tout le contexte** pour ton MITM Haldex (savoir quoi modifier dans haldex_mitm.cpp et avec quelles valeurs).

## ⚠️ Sécurité

- **JAMAIS de TX** sur ce bus depuis SavvyCAN. Mode listen-only obligatoire.
- Ton CANable / OBDLink doit être en mode "monitor" / "silent"
- Si tu utilises ESP32 SavvyCAN firmware : mode `LISTEN_ONLY` dans ACAN2515Settings
- Une seule frame TX malencontreuse sur le powertrain CAN peut déclencher des codes défaut multi-ECU

## 📝 Après la capture

Pour partager / archiver tes findings :
1. Sauve tous les `.log` (format SavvyCAN natif, replay-able)
2. Export CSV (`File → Save Loaded File As → CSV`) pour analyse externe
3. Note tes observations dans le tableau ci-dessus
4. Si tu veux étendre le DBC avec des signaux que tu as découverts, édite directement `vw_mqb_powertrain.dbc` et commit la mise à jour
