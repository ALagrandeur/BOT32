# État du projet BOT32

> Photo lisible de l'état du projet. Mise à jour : **2026-06-05**.
> Pour les règles et conventions techniques, voir **`CLAUDE.md`**.

---

## Version actuelle

**v3.9.0** (principal) + **BOT32-HALDEX v1.5.0** (module MITM privé, ESP32-CAN-X2).
Repo public `ALagrandeur/BOT32` (`master`) ; module MITM = dépôt **privé** `BOT32-HALDEX`.

🏁 **Jalon majeur : le MITM Haldex fonctionne en voiture.** Les 3 modes sont
confirmés sur le véhicule : **STOCK** (normal), **FWD** (roues avant seulement —
le burnout fonctionne), **50-50** (lock target 100 % ; le `pump %` varie à
l'arrêt = normal, l'engagement réel dépend du couple/glissement en roulant).
Le **lien ESP-NOW principal ↔ X2 est solide** et le contrôle se fait depuis l'UI.

### Ce qui a rendu le lien ESP-NOW fiable (v3.8.0 → v3.9.0 / v1.5.0)
1. **Canal radio verrouillé** explicitement (`esp_wifi_set_channel`) sur le **canal 1**
   des **deux** côtés — avant, le principal dépendait de son AP pour fixer le canal.
2. **Power-save WiFi DÉSACTIVÉ** des deux côtés (`WiFi.setSleep(false)` +
   `esp_wifi_set_ps(WIFI_PS_NONE)`) — c'était **LA** cause racine : en STA, la radio
   dormait entre les beacons et jetait les paquets ESP-NOW reçus. Réaffirmé après
   le passage en AP+STA (qui réactive le power-save).
3. **Diagnostics** : compteur `espnow_rx` côté principal (exposé dans le JSON +
   bandeau coloré dans l'UI), et côté X2 la ligne `[5s]` montre `espnow ch= tx= txf= rxcmd=`.

---

## Module MITM Haldex (BOT32-HALDEX, ESP32-CAN-X2) — résumé technique

- **Topologie en série** : coupe le stub CAN du calculateur Haldex. CAN1 = TWAI natif
  (côté CAR/PCM, GPIO7/6) ; CAN2 = MCP2515 sur HSPI (côté Haldex).
- **Pont CAR ↔ Haldex** dans une **tâche FreeRTOS dédiée** démarrée AVANT le WiFi →
  écart de boot minimal (évite les défauts TC/TPMS au démarrage en série).
- **CRC E2E AUTOSAR** (poly 0x2F, init 0xFF, xorout 0xFF) sur `[D2..D8, DataID]` —
  **DataID APPENDÉ** (confirmé 100 % sur 5 logs réels). Compteur = `D2 & 0x0F`.
- **Jeu de trames complet** (parité OpenHaldex, chacune derrière son flag) :
  0x08A / 0x0A7 / 0x0A8 (couple), 0x116 / 0x106 (ESP), 0x0B2 (roues).
  **FWD** → demande 0x00 + roues à 0 ; **50-50** → demande **0xFA** (0xFE/0xFF =
  SNA/réservé, écartés) + roues = vitesse véhicule réelle.
- **État live lu** : engagement pompe (0x118 D3), pédale (0x121 D3), vitesse (0x0FD).
- **12 V permanent + light-sleep** (15 min d'inactivité CAN) + **wake-on-CAN** ;
  **ne dort jamais** tant qu'un hôte USB est branché (`if (Serial)`).
- **Robustesse CAN** : récupération bus-off TWAI **et** MCP2515 (re-init sur EFLG TXBO),
  exposition du **TEC**. Terminaison **TERM2 (120 Ω)** activée côté Haldex.
- **Persistance** : le passthrough est **persisté en NVS** ; le **mode boot TOUJOURS STOCK**
  (sécurité — rien ne s'active tout seul au démarrage).

---

## Haldex — logique côté principal

- **Lien ESP-NOW uniquement.** AP téléphone + ESP-NOW **coexistent** sur le **canal 1**
  (mode AP+STA quand le lien est activé). Le PC en **USB-C** peut aussi parler
  directement au X2 (dev/diagnostic) — ESP-NOW court-circuité dans ce mode.
- **3 modes** : STOCK / FWD / 50-50.
- **FWD** = combo **Hazards ON + TC OFF** (ou app) ; **v3.10.0 : sort quand le TC repasse à ON** (plus quand les warnings s'éteignent → on peut couper les Hazards et garder FWD).
  **50-50** = app/USB ; sort via STOCK. **Pas d'auto-revert. Pas de seuil de pédale.**
- **Passthrough ON/OFF** (commande live) : le X2 démarre toujours **ON** (transparent/sûr) ;
  OFF = MITM **armé**. Affiché en live + confirmation modale avant d'armer + **ack** du X2 dans l'UI.
- **LIVE** : Vitesse · Pédale % · Lock target % · Pump % · Mode · Connexion (bandeau) · Passthrough
  · santé CAN (rx/tx✗/TEC/EFLG). Boutons mode + passthrough aussi sur l'**UI mobile**.

---

## Ce qui fonctionne (livré)

- ✅ **Boost-on-coolant** : override de Motor_09 (0x647), mapping linéaire MAP→température.
- ✅ **Polling UDS multi-DID** (round-robin 5 slots) : MAP, éthanol, blocage Haldex, huile DSG, EGT.
- ✅ **4 sniffers passifs** : frein à main, bouton OK volant, Hazard, Traction Control (affichage seul).
- ✅ **Clear Engine Fault** : OBD-II Mode 04 broadcast (0x700) — manuel depuis l'UI.
- ✅ **Clear DTC tous modules** : machine à états UDS non-bloquante (14+ ECU).
- ✅ **Bench test mode** + **lamp killer** (cartographie des voyants du combiné).
- ✅ **UI web PC** (Flask + SocketIO) via USB série + **UI mobile** via WiFi AP (PROGMEM).
- ✅ **MITM Haldex confirmé EN VOITURE** : STOCK / FWD (avant seulement) / 50-50, lien
  ESP-NOW solide, contrôle depuis l'UI. Firmware dans le dépôt **privé** `BOT32-HALDEX`
  (carte Autosport Labs **ESP32-CAN-X2**).
- ✅ **Sécurité** : `block_airbag` forcé ON, 5 s listen-only au boot, confirmations modales,
  aucun ID deviné émis par défaut, mode boot TOUJOURS STOCK, série USB toujours active.

---

## Historique des versions (résumé)

### Principal (BOT32)
| Version | Apport principal |
|---|---|
| v2.x | Voir historique antérieur (UDS multi-DID, WiFi AP, sniffers, bench mode) |
| v3.0.0 | Version stable — toutes les fonctions confirmées sur banc |
| v3.1.0 | **Haldex** : lien ESP-NOW, 3 modes, combo FWD, UI PC+mobile, coexistence canal 1 |
| v3.2.0 | Transport CAN mort retiré (ESP-NOW only) |
| v3.3.0 | Mode/passthrough persistés + UI affiche le mode réel du X2 |
| v3.4.0 | Lamp killer (test voyants au banc, trames live depuis l'UI) |
| v3.7.0 | Source de lien Haldex sélectionnable (ESP-NOW / USB-C direct vers le X2) |
| v3.8.0 | **Verrou de canal ESP-NOW explicite** côté principal + compteur RX + bandeau de lien dans l'UI |
| **v3.9.0** | **Power-save WiFi OFF** (lien ESP-NOW fiable) + lecture du canal réel |

### Module MITM (BOT32-HALDEX)
| Version | Apport principal |
|---|---|
| v0.3–0.4 | Persistance état + spoof vitesses de roue (FWD) |
| v0.7.x | **CRC E2E corrigé** (DataID appendé) + 0x116=0xAC / 0x106=0x07 + 50-50 demande 0xFA |
| v1.1.0 | Jeu de trames complet (0x116/0x106/0x0B2) sur FWD ET 50-50, sans seuil pédale |
| v1.3.0 | Pont en **tâche FreeRTOS avant WiFi** (fix défauts TC/TPMS au boot) |
| v1.4.x | **12 V permanent** + light-sleep 15 min + wake-on-CAN + récup bus-off MCP/TWAI + TEC |
| **v1.5.0** | **Power-save WiFi OFF** + diagnostics ESP-NOW (canal/tx/rxcmd) |

---

## Pistes / roadmap (non démarrées)

- ⏳ Affiner les valeurs **50-50** en roulant si l'engagement ne tient pas (log pump/pédale/vitesse).
- ⏳ Implémenter la **détection firmware** du déclenchement auto du Clear Engine Fault (Hazard ×3 / 4 s).
- ⏳ **Valider Clear Engine Fault** avec une capture fraîche.
- ⏳ Identifier les **témoins individuels** du combiné (lamp killer).
- 💡 Web UI pour les boutons du volant (objectif initial du projet).

---

## Notes de reprise

- Lancer Claude Code depuis `C:\Users\AntoineLagrandeur\MK7-cluster` pour recharger la
  **mémoire auto** ; le code principal est dans `C:\Users\AntoineLagrandeur\BOT32`,
  le module MITM dans `C:\Users\AntoineLagrandeur\BOT32-HALDEX`.
- **Réglages Arduino X2** : carte « ESP32S3 Dev Module », **USB CDC On Boot = Enabled**
  (le port USB-C est l'USB natif de l'ESP32-S3 ; sinon le moniteur série reste muet).
- **ESP-NOW** : les deux ESP32 doivent être sur le **canal 1** (verrouillé) et avec le
  **power-save OFF** (fait en v3.9.0 / v1.5.0). Vérif : bandeau vert dans l'UI, ou
  ligne `[5s]` du X2 `espnow ch=1 tx=… rxcmd=…`.
- **Sur banc** : garder le **X2 sur USB-C** (sinon sommeil après 15 min → ESP-NOW coupé).
- Dernier jalon (2026-06-05) : MITM **confirmé en voiture** (FWD/STOCK/50-50) + lien
  ESP-NOW rendu fiable (canal verrouillé + power-save OFF). Point de sauvegarde complet.
