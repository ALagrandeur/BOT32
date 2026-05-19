# BOT32 — Câblage avec HAT WaveShare 2-CH CAN

**Hardware utilisé** : HAT WaveShare 2-CH CAN (2× MCP2515 + 2× SIT65HVD230) connecté à un ESP32 via fils Dupont, **sans utiliser le connecteur GPIO 40 pin du Pi** (juste les broches individuelles).

## ✅ Pourquoi cette approche

| Avantage | Détail |
|---|---|
| Hardware déjà en main | Tu as le HAT, rien à acheter en plus |
| Pas de level shifter | SIT65HVD230 = 3.3V natif, ESP32 = 3.3V, compatible direct |
| Transceivers déjà soudés | + termination jumpers 120Ω + borniers à vis CAN H/L/G |
| 2 chips identiques | Code symétrique, plus simple à debugger |
| 10 fils Dupont seulement | Câblage rapide |

## 🛒 BOM additionnel

| Item | ~Prix CAD |
|---|---|
| ESP32-DevKitC-V4 (ou clone) | $10 |
| LM2596 buck 12V → 5V (alim depuis OBD2) | $3 |
| Connecteur OBD-II J1962 mâle + câble | $8 |
| Fils Dupont F-F 20 cm (paquet 40) | $3 |
| Fusible 2A + porte-fusible inline | $3 |
| **Total** | **~$27** |

(En plus du HAT WaveShare que tu as déjà.)

## 📌 Pinout du HAT — broches à utiliser

Sur le HAT, ces broches sont accessibles via le connecteur 40-pin (côté GPIO du Pi). Tu peux soit :
- **Option 1 (recommandée)** : connecter des fils Dupont **F-F (femelle-femelle)** entre les broches du HAT et les broches de ton ESP32 dev board
- **Option 2** : souder un câble nappe (plus propre mais plus complexe)

Les labels visibles sur la photo de ton HAT (côté GPIO header) :
```
INT1 | INT0 | CS1 | CS0 | SCK | MOSI | MISO | GND | 5V
```

Plus accessible sur les broches alignées du connecteur Pi (les 40 pin). Voici les broches à tapper (les autres sont libres / non utilisées) :

| Pin Pi (header) | Signal HAT | Connecter à ESP32 |
|---|---|---|
| **1** (3V3) | **3V3** | ESP32 **3V3** |
| **6** (GND) | **GND** | ESP32 **GND** |
| **19** (GPIO 10 = MOSI) | **MOSI** | ESP32 **GPIO 23** |
| **21** (GPIO 9 = MISO) | **MISO** | ESP32 **GPIO 19** |
| **23** (GPIO 11 = SCK) | **SCK** | ESP32 **GPIO 18** |
| **24** (GPIO 8 = CE0) | **CS0** | ESP32 **GPIO 5** |
| **26** (GPIO 7 = CE1) | **CS1** | ESP32 **GPIO 25** |
| **22** (GPIO 25) | **INT0** | ESP32 **GPIO 4** |
| **18** (GPIO 24) | **INT1** | ESP32 **GPIO 26** |

⚠ **Important — jumper VIO sur ton HAT** : sur ta photo, je vois le jumper VIO en bas à gauche. Il sélectionne l'alimentation logique des MCP2515 entre **5V** ou **3V3**. Pour utiliser avec ESP32 (3.3V), **mets ce jumper en position 3V3**.

## 🔌 Diagramme de câblage

```
   ┌─────────────────────────────────────────────────────┐
   │            HAT WaveShare 2-CH CAN                    │
   │                                                      │
   │  GPIO header (40 pin Pi-style)                       │
   │  ┌─────────────────────────────────────┐             │
   │  │ pin 1 (3V3)  ───── 3V3 ─────┐       │             │
   │  │ pin 6 (GND)  ───── GND ─────┼─┐     │             │
   │  │ pin 19 (P10) ───── MOSI ────┼─┼─┐   │             │
   │  │ pin 21 (P9)  ───── MISO ────┼─┼─┼─┐ │             │
   │  │ pin 23 (P11) ───── SCK  ────┼─┼─┼─┼─┐             │
   │  │ pin 24 (P8)  ───── CS0  ────┼─┼─┼─┼─┼─┐           │
   │  │ pin 26 (P7)  ───── CS1  ────┼─┼─┼─┼─┼─┼─┐         │
   │  │ pin 22 (P25) ───── INT0 ────┼─┼─┼─┼─┼─┼─┼─┐       │
   │  │ pin 18 (P24) ───── INT1 ────┼─┼─┼─┼─┼─┼─┼─┼─┐     │
   │  └─────────────────────────────┼─┼─┼─┼─┼─┼─┼─┼─┼─┘   │
   │                                │ │ │ │ │ │ │ │ │     │
   │  Jumper VIO en position 3V3 ✓  │ │ │ │ │ │ │ │ │     │
   │  Jumper 120R CAN0: ON (bench)  │ │ │ │ │ │ │ │ │     │
   │  ou OFF (voiture)              │ │ │ │ │ │ │ │ │     │
   │  Jumper 120R CAN1: même règle  │ │ │ │ │ │ │ │ │     │
   │                                │ │ │ │ │ │ │ │ │     │
   │  Bornier CAN0: H/L → cluster   │ │ │ │ │ │ │ │ │     │
   │  Bornier CAN1: H/L → OBD2      │ │ │ │ │ │ │ │ │     │
   └────────────────────────────────│─│─│─│─│─│─│─│─│─────┘
                                    │ │ │ │ │ │ │ │ │
   ┌────────────────────────────────│─│─│─│─│─│─│─│─│─────┐
   │            ESP32-DevKitC-V4    │ │ │ │ │ │ │ │ │     │
   │                                │ │ │ │ │ │ │ │ │     │
   │            ┌────────────────┐  │ │ │ │ │ │ │ │ │     │
   │  3V3  pin1 │1            38 │ ◄┘ │ │ │ │ │ │ │ │     │
   │  GND  pin38│2               │ ◄──┘ │ │ │ │ │ │ │     │
   │  GPIO23    │37              │ ◄────┘ │ │ │ │ │ │     │
   │  GPIO19    │31              │ ◄──────┘ │ │ │ │ │     │
   │  GPIO18    │30              │ ◄────────┘ │ │ │ │     │
   │  GPIO 5    │29              │ ◄──────────┘ │ │ │     │
   │  GPIO25    │9               │ ◄────────────┘ │ │     │
   │  GPIO 4    │26              │ ◄──────────────┘ │     │
   │  GPIO26    │10              │ ◄────────────────┘     │
   │            └────────────────┘                         │
   │            USB-C / micro-USB (flash + debug + power)  │
   └─────────────────────────────────────────────────────────┘
```

## ⚡ Alimentation

Plusieurs options :

### Option A (bench / dev) : USB depuis PC

- ESP32 alimenté via USB-C (5V depuis PC)
- ESP32 LDO interne génère 3.3V
- HAT alimenté par ESP32 3V3 pin (suffisant pour les 2 MCP2515 + transceivers)

→ Idéal pour développer + flasher. **Pas d'alim externe nécessaire**.

### Option B (voiture, installation finale) : 12V via OBD2 pin 16

```
   OBD2 pin 16 (+12V)
        │
   [Fusible 2A inline]
        │
   [Diode 1N5408 anti-inversion]
        │
   [LM2596 ajusté à 5.0V]
        │
        ▼
   ESP32 VIN (pin 19)  →  régulateur interne génère 3.3V
        │
        ▼
   HAT 3V3 pin
```

→ Quand cléf OFF, OBD2 pin 16 coupe l'alim, ESP32 s'éteint, pas de drain batterie.

## 🚗 Câblage côté voiture (terminaux à vis du HAT)

### Bornier **CAN0** (cluster)
| Vis HAT | → | Cluster MK7 (connecteur 18 pin) |
|---|---|---|
| **H** | → | pin **17** (CAN-H) — T-tap, ne PAS couper |
| **L** | → | pin **18** (CAN-L) — T-tap, ne PAS couper |
| **G** | → | pin **10** (GND) ou GND chassis |

### Bornier **CAN1** (OBD2)
| Vis HAT | → | OBD-II J1962 mâle |
|---|---|---|
| **H** | → | pin **6** (CAN-H) |
| **L** | → | pin **14** (CAN-L) |
| **G** | → | pin **4 ou 5** (GND) |

### Termination jumpers

| Configuration | Jumper 120R CAN0 | Jumper 120R CAN1 |
|---|---|---|
| Test bench seul (HAT + 1 cluster sur table) | **ON** (cluster ne termine pas seul) | **ON** |
| Installation voiture (bus a déjà 2 terminateurs) | **OFF** | **OFF** |

## 🛡️ Sécurité électrique

| Composant | Rôle |
|---|---|
| Fusible 2A | Coupe si court-circuit, protège la batterie voiture |
| Diode 1N5408 | Si tu inverses + et -, la diode bloque (chute 0.7V OK) |
| Jumper VIO=3V3 | Force le HAT en 3.3V logique, évite tout level shifter |
| Termination OFF en voiture | Évite mismatch impédance avec terminateurs OEM |
| Pas de TX en BOOT (5s) | Listen-only au démarrage, attend signaux stables |

## 📋 Checklist montage rapide

- [ ] **Jumper VIO du HAT en position 3V3** ✓
- [ ] 10 fils Dupont F-F connectés selon table ci-dessus
- [ ] Termination jumpers selon usage (bench=ON, voiture=OFF)
- [ ] Bornier CAN0 vissé sur fils vers cluster pins 17/18
- [ ] Bornier CAN1 vissé sur fils vers OBD2 pins 6/14
- [ ] Alim 5V vers ESP32 VIN (USB ou LM2596 depuis OBD2 pin 16)
- [ ] LED `PWR` du HAT allumée (rouge orange)
- [ ] ESP32 boot OK via Serial Monitor @ 115200

## 🧪 Test rapide (sans rien câbler au véhicule)

1. Wire les 10 fils Dupont HAT ↔ ESP32
2. Alimente l'ESP32 via USB depuis ton PC
3. Vérifie LED `PWR` HAT allumée
4. Flash le sketch BOT32
5. Ouvre Serial Monitor @ 115200
6. Tu dois voir :
```
================================
  BOT32 - boost gauge override
================================
[NVS] Settings loaded from flash
[CAN] cluster MCP2515 started at 500 kbps (12 MHz xtal)
[CAN] obd2    MCP2515 started at 500 kbps (12 MHz xtal)
[main] Entering BOOT
```

→ Si tu vois "MCP2515 started" pour les 2 chips : ✅ tu peux déjà voir les frames RX dans la web UI dès que tu connectes le HAT à un bus CAN vivant (ou loopback).

## ⚠ Différence vs Option A originale

| | Option A (initial) | **HAT WaveShare (actuel)** |
|---|---|---|
| CAN0 controller | TWAI interne ESP32 | MCP2515 #0 sur SPI |
| CAN1 controller | MCP2515 sur SPI | MCP2515 #1 sur SPI |
| Transceiver cluster | SN65HVD230 séparé | SIT65HVD230 sur HAT |
| Transceiver OBD2 | TJA1050 (5V) | SIT65HVD230 sur HAT (3.3V) |
| Level shifter | Requis (TXS0108E) | **PAS requis** ✓ |
| Coût additionnel | ~$64 | ~$27 |
| Composants à acheter | ESP32 + transceivers + shifter + module + alim | ESP32 + alim + connecteur OBD2 |

## 🔧 Configuration logicielle correspondante

Le `BOT32/config.h` est déjà configuré pour cette wiring :

```cpp
// SPI partagé
#define PIN_SPI_SCK    18
#define PIN_SPI_MISO   19
#define PIN_SPI_MOSI   23

// CAN0 cluster (MCP2515 #0)
#define PIN_CAN0_CS     5
#define PIN_CAN0_INT    4

// CAN1 OBD2 (MCP2515 #1)
#define PIN_CAN1_CS    25
#define PIN_CAN1_INT   26

// Cristal MCP2515 sur HAT WaveShare = 16 MHz (officiel WaveShare wiki)
#define MCP2515_CLOCK_MHZ 16
```

Si tu changes de HAT (par exemple un module MCP2515 chinois avec cristal 8 MHz), change juste `MCP2515_CLOCK_MHZ`. Le reste du code n'a aucune notion du hardware spécifique.
