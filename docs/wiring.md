# BOT32 — Schéma de câblage détaillé

**Architecture retenue** : Option A
- **CAN0 (cluster)** = TWAI interne ESP32 + transceiver SN65HVD230 (3.3V)
- **CAN1 (OBD2)** = MCP2515 sur SPI + transceiver TJA1050 (5V) + level shifter

---

## 1. Liste des composants

| # | Composant | Rôle | ~Prix CAD |
|---|---|---|---|
| 1 | ESP32-DevKitC-V4 (38 pins) | Microcontrôleur principal | $12 |
| 2 | SN65HVD230 breakout (3.3V) | Transceiver CAN cluster | $5 |
| 3 | MCP2515 + TJA1050 module (5V) | Contrôleur+transceiver OBD2 | $8 |
| 4 | **TXS0108E ou BSS138 4-ch level shifter** | 3.3V ↔ 5V sur SPI MISO/INT | $4 |
| 5 | LM2596 buck converter | 12V auto → 5V | $3 |
| 6 | Diode 1N5408 (3A) | Protection inversion polarité | $1 |
| 7 | Fusible 2A + porte-fusible | Protection court-circuit | $3 |
| 8 | Connecteur OBD2 J1962 mâle | Branchement port OBD2 voiture | $8 |
| 9 | Connecteur T-tap ou Posi-Tap ×2 paires | Tap CAN cluster pins 17/18 | $5 |
| 10 | Boîtier ABS étanche | Protection physique | $10 |
| 11 | Câbles + JST connectors | Câblage | $5 |
| | **TOTAL** | | **~$64** |

---

## 2. Pinout ESP32-DevKitC (référence)

```
                       ESP32-DevKitC-V4 (38 pins)
                       ──────────────────────────
                                                                 
                       ┌─────────────────────────┐
                  3V3 ─┤ 1                    38 ├─ GND
                  EN  ─┤ 2                    37 ├─ GPIO 23  ← MCP2515 MOSI (via level shifter)
              GPIO 36 ─┤ 3 (ADC1_0/VP)        36 ├─ GPIO 22  ← TWAI RX (SN65HVD230 CRX)
              GPIO 39 ─┤ 4 (ADC1_3/VN)        35 ├─ GPIO  1  (TX0 - USB serial)
              GPIO 34 ─┤ 5                    34 ├─ GPIO  3  (RX0 - USB serial)
              GPIO 35 ─┤ 6                    33 ├─ GPIO 21  → TWAI TX (SN65HVD230 CTX)
              GPIO 32 ─┤ 7                    32 ├─ GND
              GPIO 33 ─┤ 8                    31 ├─ GPIO 19  ← MCP2515 MISO (via level shifter)
              GPIO 25 ─┤ 9                    30 ├─ GPIO 18  ← MCP2515 SCK (via level shifter)
              GPIO 26 ─┤ 10                   29 ├─ GPIO  5  → MCP2515 CS  (via level shifter)
              GPIO 27 ─┤ 11                   28 ├─ GPIO 17  
              GPIO 14 ─┤ 12                   27 ├─ GPIO 16  
              GPIO 12 ─┤ 13                   26 ├─ GPIO  4  ← MCP2515 INT (via level shifter)
                  GND ─┤ 14                   25 ├─ GPIO  0  (boot)
              GPIO 13 ─┤ 15                   24 ├─ GPIO  2  → LED Status (built-in)
              GPIO  9 ─┤ 16                   23 ├─ GPIO 15  
              GPIO 10 ─┤ 17                   22 ├─ GPIO  8  
              GPIO 11 ─┤ 18                   21 ├─ GPIO  7  
                   5V ─┤ 19 (VIN)             20 ├─ GPIO  6  
                       └─────────────────────────┘
                              USB-C / micro-USB
                              (TX0/RX0 + GPIO0)
```

---

## 3. Diagramme global

```
                         ┌──────────────────────────────────────┐
                         │           Voiture VW MK7              │
                         │                                       │
        ┌───────────────────────────────┐          ┌────────────────────┐
        │  Cluster (18-pin connecteur)  │          │ Port OBD-II (J1962)│
        │                               │          │                    │
        │  pin 17 CAN-H ──────┐         │          │  pin 6  CAN-H ──┐  │
        │  pin 18 CAN-L ────┐ │         │          │  pin 14 CAN-L ┐ │  │
        │                   │ │         │          │  pin 4/5 GND ┐ │ │  │
        │                   │ │         │          │  pin 16 +12V ┐│ │ │  │
        └───────────────────│─│─────────┘          └──────────────││─│─│──┘
                            │ │                                    ││ │ │
                            │ │ T-TAP                              ││ │ │ Connecteur OBD2 mâle
                            │ │                                    ││ │ │
              ┌─────────────┴─┴───┐                        ┌───────┴┴─┴─┴───┐
              │   SN65HVD230      │                        │    Câble vers    │
              │   (CAN cluster)   │                        │    BOT32 box     │
              │                   │                        └────────────┬─────┘
              │ CANH    CANL VCC GND CTX CRX │                            │
              └──┬───────┬───┬───┬───┬───┬──┘                            │
                 │       │   │   │   │   │                                │
              (pin17) (pin18) │  │   │   │                                │
                              │  │   │   │                                │
                              │  │   │   └─────────────────────┐         │
                              │  │   └────────────────┐        │         │
                              │  │                    │        │         │
   ┌──────────────────────────│──│────────────────────│────────│─────────│─────────────┐
   │                          │  │                    │        │         │             │
   │       BOT32 (boîtier)   3.3V GND               GPIO22  GPIO21       │             │
   │                          │  │                  RX_TWAI TX_TWAI      │             │
   │                          │  │                    │        │         │             │
   │                          │  │             ┌──────│────────│─────┐   │             │
   │                          │  │             │      │        │     │   │             │
   │                          │  │             │     ESP32-DevKitC   │   │             │
   │                          │  │             │   (UART USB pour    │   │             │
   │                          │  │             │    flash/debug)     │   │             │
   │                          │  │             │                     │   │             │
   │                          │  │             │  3V3 5V  GND   GPIO 5,18,19,23,4    │
   │                          │  │             │   │   │    │       │ │ │  │  │  │     │
   │                          │  │             └───│───│────│───────│─│─│──│──│──│─────┘
   │                          │  │                 │   │    │       │ │ │  │  │  │
   │                          │  │                 │   │    │       │ │ │  │  │  │
   │                          │  │                 │   │    │  CS  SCK MISO MOSI INT
   │                          │  │                 │   │    │   │   │   │   │    │
   │                          └──│─────────────────│───│────│   │   │   │   │    │
   │                             │                 │   │    │   │   │   │   │    │
   │                             └─────────────────│───│────│   │   │   │   │    │
   │                                          (3.3V) │ (GND) │   │   │   │   │    │
   │                                                 │       │   │   │   │   │    │
   │                                ┌────────────────│───────│───│───│───│───│────│──┐
   │                                │  Level shifter 4-ch (TXS0108E)             │  │
   │                                │  LV side (3.3V)        HV side (5V)        │  │
   │                                │  ┌─────────┐           ┌─────────┐         │  │
   │                                │  │ LV=3.3V │  bidir    │ HV=5V   │         │  │
   │                                │  │ A1 ─────┼───────────┼──── B1  │         │  │
   │                                │  │ A2 ─────┼───────────┼──── B2  │ (4 ch:  │  │
   │                                │  │ A3 ─────┼───────────┼──── B3  │  CS, SCK,│ │
   │                                │  │ A4 ─────┼───────────┼──── B4  │  MOSI, et│ │
   │                                │  │ OE      │           │         │  MISO+   │ │
   │                                │  │ GND     │           │ GND     │  INT     │ │
   │                                │  └─────────┘           └─────────┘  pair sep)│ │
   │                                └──────────────────────────────────────────────┘  │
   │                                       │                       │                  │
   │                                       │                       │                  │
   │                              ┌────────│───────────────────────│───────────────┐  │
   │                              │       MCP2515 module + TJA1050 (5V)            │  │
   │                              │                                                 │  │
   │                              │  VCC  GND  CS  SCK  MISO  MOSI  INT  CANH  CANL│  │
   │                              │   │    │    │   │    │     │    │    │     │  │  │
   │                              └───│────│────│───│────│─────│────│────│─────│──┘  │
   │                                  │    │    │   │    │     │    │    │     │     │
   │                                  5V  GND  (from level shifter HV side)   │  │     │
   │                                                                          │  │     │
   │                                                            (to OBD2 CAN H/L)│     │
   │                                                                              │     │
   │                                                                              │     │
   │      ┌──────────────────────────────────────────────────────────────────────┘     │
   │      │                                                                              │
   │      │   ┌──────────────────────────────────┐                                       │
   │      │   │  LM2596 buck converter           │                                       │
   │      │   │  IN: 7-30V    OUT: ajusté 5.0V   │                                       │
   │      │   │                                   │                                       │
   │      │   │  IN+  IN-    OUT+  OUT-          │                                       │
   │      │   │   │    │      │     │             │                                       │
   │      │   └───│────│──────│─────│─────────────┘                                       │
   │      │       │    │      │     │                                                     │
   │      │       │    │      │     └─────────── GND (commune partout)                    │
   │      │       │    │      └─────────────────── 5V → ESP32 VIN, MCP2515 VCC, HV shifter│
   │      │       │    │                                                                  │
   │      │   [Fusible 2A]  [Diode 1N5408 anti-inversion]                                │
   │      │       │    │                                                                  │
   │      │  +12V auto GND (depuis OBD2 pin 16 et pin 4/5)                                │
   │      └──────────────────────────────────────────────────────────────────────────────┘
   │                                                                                       │
   └───────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Tableau de câblage exhaustif

### 4.1 — Alimentation

| De | Vers | Câble | Notes |
|---|---|---|---|
| OBD2 pin 16 (+12V) | Fusible 2A → Diode anode | Rouge 18AWG | Protection court-circuit + inversion |
| Diode cathode | LM2596 IN+ | Rouge 18AWG | |
| OBD2 pin 4 (GND chassis) | LM2596 IN- | Noir 18AWG | OBD2 pin 5 = GND signal aussi OK |
| LM2596 OUT+ (réglé à 5.0V) | ESP32 VIN (pin 19) | Rouge 22AWG | Vérifier 5.0V au multimètre AVANT branchement ESP32 ! |
| LM2596 OUT+ | MCP2515 VCC | Rouge 22AWG | |
| LM2596 OUT+ | Level shifter HV (5V side) | Rouge 22AWG | |
| LM2596 OUT- | ESP32 GND, MCP2515 GND, shifter GND | Noir 22AWG | GND commun obligatoire |
| ESP32 3V3 (pin 1) | SN65HVD230 VCC | Rouge 26AWG | 3.3V généré par LDO interne ESP32 |
| ESP32 3V3 (pin 1) | Level shifter LV (3.3V side) | Rouge 26AWG | |
| ESP32 GND (pin 38) | SN65HVD230 GND, shifter LV GND | Noir 26AWG | |

### 4.2 — CAN0 cluster (TWAI interne)

| De (ESP32) | Vers (SN65HVD230) | Notes |
|---|---|---|
| **GPIO 21** | **CTX (TX in)** | TX du contrôleur CAN |
| **GPIO 22** | **CRX (RX out)** | RX vers contrôleur CAN |
| 3V3 | VCC | 3.3V power |
| GND | GND | |

| De (SN65HVD230) | Vers (Voiture) | Notes |
|---|---|---|
| **CANH** | Cluster pin **17** (T-tap, ne PAS couper) | Différentiel + |
| **CANL** | Cluster pin **18** (T-tap, ne PAS couper) | Différentiel - |

⚠ **Pas de terminaison 120Ω sur le HAT** : le bus voiture a déjà 2 terminateurs (cluster + gateway). Si ton breakout SN65HVD230 a un jumper de termination → **OFF**.

### 4.3 — CAN1 OBD2 (MCP2515 sur SPI)

#### Lignes ESP32 (3.3V) → MCP2515 (5V) via level shifter

| ESP32 | Level shifter LV side | Level shifter HV side | MCP2515 |
|---|---|---|---|
| GPIO 5 (CS) | A1 | B1 | CS |
| GPIO 18 (SCK) | A2 | B2 | SCK |
| GPIO 23 (MOSI) | A3 | B3 | SI (MOSI) |
| GPIO 19 (MISO) | A4 ← | B4 ← | SO (MISO) |
| GPIO 4 (INT) | A5 ← | B5 ← | INT |
| 3V3 | LV (Vcc 3.3V) | HV (Vcc 5V) | (5V via cable séparé) |
| GND | GND | GND | GND |

⚠ Note : le TXS0108E a 8 channels. Si tu utilises 5 lignes (CS+SCK+MOSI+MISO+INT), tu en occupes 5. Tu peux aussi prendre un BSS138 4-ch et faire MISO+INT séparément (les 2 lignes "input vers ESP32") sur un autre shifter ou résistances pull-down — voir notes.

| MCP2515 | Vers OBD2 (port voiture) | Notes |
|---|---|---|
| CANH | Connecteur OBD2 mâle pin **6** | Différentiel + |
| CANL | Connecteur OBD2 mâle pin **14** | Différentiel - |

⚠ Termination OBD2 : même règle que cluster, **PAS de 120Ω sur le module MCP2515** quand branché en voiture (gateway termine). Beaucoup de modules MCP2515 chinois ont un jumper "J1" pour la termination → **OFF**.

---

## 5. Pourquoi le level shifter ?

**Sans level shifter** :
- ESP32 → MCP2515 (TTL 3.3V → CMOS 5V) : **OK** car le MCP2515 voit 3.3V comme HIGH (seuil ~2V).
- MCP2515 → ESP32 (MISO + INT, 5V → 3.3V) : **DANGEREUX**. Les GPIO ESP32 ne sont **PAS 5V tolerant**. Ça grille la pin (et peut tuer le chip).

**Avec level shifter** :
- Les 5 lignes sont propres et bidir (utile pour SPI).
- Aucun risque pour l'ESP32.
- Coût ~$2 pour la sécurité.

**Alternative pour économiser** : utiliser un module MCP2515 qui supporte 3.3V natif (rare mais ça existe). Sinon level shifter obligatoire.

---

## 6. Connecteur OBD-II J1962 (mâle, vue côté contacts)

```
   ┌─────────────────────────────────────┐
   │   1   2   3   4   5   6   7   8   │
   │   ○   ○   ○   ○   ○   ●   ○   ○   │
   │                                     │
   │   ○   ○   ○   ○   ○   ●   ○   ●   │
   │   9   10  11  12  13  14  15  16   │
   └─────────────────────────────────────┘
       ↑                ↑           ↑
       BOT32 uses:      CAN-L (14)  +12V batt (16)
       CAN-H (6) ───────┘           
       GND (4 ou 5) ────────────────────  (ground commune)
```

| Pin OBD2 | Signal | Connecté à |
|---|---|---|
| 4 ou 5 | GND chassis/signal | LM2596 IN- (GND commune BOT32) |
| **6** | **CAN-H (HS-CAN)** | MCP2515 CANH |
| **14** | **CAN-L (HS-CAN)** | MCP2515 CANL |
| **16** | **+12V batterie** | Fusible 2A → diode → LM2596 IN+ |

Les autres pins (1, 2, 3, 7, 8, 9, 10, 11, 12, 13, 15) ne sont PAS utilisées par BOT32 — ne les câble PAS.

---

## 7. Cluster MK7 — connecteur 18 pins

⚠ **NE PAS débrancher le connecteur cluster** — utilise des **T-tap** ou **Posi-Tap** pour piquer les fils sans les couper.

| Pin cluster | Signal | Action BOT32 |
|---|---|---|
| 1 | Kl.30 (+12V perm) | Ne pas tapper (déjà alimenté par voiture) |
| 10 | Kl.31 (GND) | Ne pas tapper (GND déjà via OBD2) |
| 16 | Kl.15 (+12V ignition) | Ne pas tapper |
| **17** | **CAN-H cluster** | **T-tap → SN65HVD230 CANH** |
| **18** | **CAN-L cluster** | **T-tap → SN65HVD230 CANL** |

Pinout exact selon modèle cluster — pour 5G1 920 740B (Alltrack 2017) :

```
   Vue côté connecteur cluster (côté arrière du dash) :
   
   ┌──────────────────────────────────────────────┐
   │  1   2   3   4   5   6   7   8   9          │
   │  ○   ○   ○   ○   ○   ○   ○   ○   ○          │
   │                                              │
   │  10  11  12  13  14  15  16  17  18         │
   │  ○   ○   ○   ○   ○   ○   ○   ●   ●          │
   └──────────────────────────────────────────────┘
                                    ↑   ↑
                                    │   └── CAN-L (BOT32 tap)
                                    └────── CAN-H (BOT32 tap)
```

---

## 8. Procédure de montage (recommandée)

### Étape 1 — Test sur bench AVANT installation voiture

1. Monte tout sur breadboard sans le câbler à la voiture
2. Alimente avec 12V depuis une alim de labo (limit current 500 mA)
3. Vérifie LED ESP32 allume + boot OK via Serial USB
4. Test loopback CAN0 (TWAI) : flash le sketch test
5. Test loopback CAN1 (MCP2515) : flash le sketch test
6. Test envoi/réception entre les 2 (boucler CAN0-H/L ↔ CAN1-H/L avec 120Ω résistance)

### Étape 2 — Test bench avec cluster réel

1. Branche le cluster sur ton bench (alim 12V + GND + Kl.15)
2. Branche BOT32 CAN0 H/L sur cluster pins 17/18 (avec 1 terminateur 120Ω TEMPORAIRE car bench seul, pas de gateway)
3. Flash le sketch de test bench
4. Vérifie que l'aiguille bouge en fonction du MAP simulé

### Étape 3 — Installation voiture

1. **Voiture cléf OFF**, retire le fusible OBD2 (si tu peux l'identifier) ou déconnecte la batterie
2. Connecte BOT32 sur OBD2 (vérifie polarité avant de mettre cléf)
3. Tap les fils cluster pins 17/18 avec T-tap
4. **Retire le terminateur 120Ω temporaire** sur BOT32 CAN0 (la voiture termine déjà)
5. Cléf ON moteur OFF : vérifie LED BOT32 + boot via Serial (peut utiliser USB long si tu veux debug en live)
6. Démarre moteur : passe en mode S, observe aiguille

---

## 9. Sécurité physique

- **Fusible 2A inline** sur l'alim 12V (côté OBD2 pin 16). Si court-circuit, ça grille le fusible, pas BOT32 ni la voiture.
- **Diode 1N5408** en série après le fusible : si tu inverses + et - par erreur, la diode bloque (chute ~0.7V mais OK pour 12V).
- **Boîtier étanche** ABS pour protéger contre les vibrations + humidité.
- **Strain relief** sur les câbles : pas de tension directe sur les pins ESP32.

---

## 10. BOM final (Bill of Materials)

Liste de courses précise :

```
Quantité  Article                                          Source typique
─────────────────────────────────────────────────────────────────────────
1x       ESP32-DevKitC-V4 (38 pins)                       Amazon, AliExpress
1x       SN65HVD230 CAN Transceiver Module                Amazon, AliExpress  
1x       MCP2515 CAN BUS Module avec TJA1050              Amazon, AliExpress
1x       TXS0108E 8-channel Logic Level Converter         Amazon, AliExpress
1x       LM2596 Step-Down Buck Converter (ajustable)     Amazon, AliExpress
1x       Diode 1N5408 (3A, 1000V) ou 1N4007 (1A)          DigiKey, Amazon
1x       Porte-fusible inline + fusibles 2A (x3 spare)   Amazon
2x       T-tap connector 18 AWG (rouge) pour CAN cluster Amazon
1x       Connecteur OBD2 mâle J1962 16-pin avec câble    Amazon
1x       Boîtier ABS IP65 ~100x60x30mm                    Amazon
1m       Câble 18AWG rouge + 1m noir (alim)              Amazon
2m       Câble 26AWG multi-conducteurs (signal CAN)       Amazon
1x       Header pins femelles 2.54mm assortis             Amazon
1x       Breadboard 400 points (pour prototype)           Amazon
```

Budget total estimé : **~$60-80 CAD** selon source.

---

## 11. Configuration `config.h` finale

Avec ces choix, mettre à jour `BOT32/config.h` :

```cpp
// CAN0 (TWAI interne - cluster)
#define PIN_CAN0_TX    21
#define PIN_CAN0_RX    22

// CAN1 (MCP2515 SPI - OBD2)
#define PIN_CAN1_CS    5
#define PIN_CAN1_INT   4
#define PIN_CAN1_SCK   18
#define PIN_CAN1_MISO  19
#define PIN_CAN1_MOSI  23

// LED status
#define PIN_LED_STATUS 2
```

Voilà, je vais update `config.h` avec ces valeurs maintenant.
