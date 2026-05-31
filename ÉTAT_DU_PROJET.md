# État du projet BOT32

> Photo lisible de l'état du projet. Mise à jour : **2026-05-30**.
> Pour les règles et conventions techniques, voir **`CLAUDE.md`**.

---

## Version actuelle

**v3.3.0** (principal) + **BOT32-HALDEX v0.3.0** (module MITM privé, ESP32-CAN-X2).
Repo public `ALagrandeur/BOT32` (`master`) ; module MITM = dépôt **privé** `BOT32-HALDEX`.

v3.3.0 / v0.3.0 : le X2 **persiste son état** (mode + passthrough) en NVS et le
restaure au boot (1er boot = STOCK + passthrough ON). ⚠ s'il est coupé ARMÉ il
revient ARMÉ. L'UI principale affiche le **mode réel rapporté par le X2**.

`SETTINGS_VERSION = 18` (inchangé — réglages préservés) · `obd2_poll_hz` défaut = 30 Hz (5 slots round-robin).

### Haldex (v3.1.0 → v3.2.0)
- **Lien ESP-NOW uniquement.** AP téléphone + ESP-NOW **coexistent** sur le **canal 1**.
  v3.2.0 a **retiré les 4 réglages morts** du transport CAN (haldex_bus/state_id/cmd_id/transport).
- **3 modes** : STOCK / FWD / 50-50 (60/40, 75/25, Expert retirés).
- **FWD** = combo **Hazards ON + bouton TC** (ou app) ; sort quand les warnings s'éteignent.
  **50-50** = app/USB ; sort via STOCK. **Pas d'auto-revert.**
- **Passthrough ON/OFF** (commande live) : le X2 démarre toujours **ON** (transparent/sûr) ;
  OFF = MITM **armé**. Affiché en live + confirmation modale avant d'armer.
- **MITM réel (Phase 2)** sur le X2 : en FWD/50-50 armé, réécrit les trames MQB
  d'allocation de couple (0x08A ESP_14, 0x0A7 Motor_11, 0x0A8 Motor_12) avec
  recalcul du **CRC E2E AUTOSAR** (faits du protocole, inspiré d'OpenHaldex).
- **LIVE** : Vitesse · Pédale % · Lock target % · Pump % · Mode · Connexion · Passthrough.
- Boutons de mode + bouton passthrough sur l'**UI mobile** aussi.
- Témoin frein à main pour le mode : **abandonné** (frein mécanique → pas de trame CAN
  d'entrée à injecter). Le mode actuel se lit dans l'UI web.

---

## Ce qui fonctionne (livré)

- ✅ **Boost-on-coolant** : override de Motor_09 (0x647), mapping linéaire MAP→température.
- ✅ **Polling UDS multi-DID** (round-robin 5 slots) : MAP, éthanol, blocage Haldex,
  huile DSG, EGT.
- ✅ **4 sniffers passifs** de boutons/états : frein à main, bouton OK volant, Hazard,
  Traction Control (affichage seul, aucune action firmware). TC affiché ON/OFF.
- ✅ **Clear Engine Fault** : OBD-II Mode 04 broadcast (0x700) — manuel depuis l'UI.
  Détecteur de déclenchement **automatique** (Hazard ×3 en 4 s) : config persistée,
  détection firmware = **roadmap (pas encore implémentée)**.
- ✅ **Clear DTC tous modules** : machine à états UDS non-bloquante (14+ ECU).
- ✅ **Bench test mode** : émet le bundle complet pour tester un combiné sur établi
  (auto-toggle TX + avertissements jumpers 120 Ω).
- ✅ **UI web PC** (Flask + SocketIO) via USB série + **UI mobile** via WiFi AP (PROGMEM).
- ✅ **Lien Haldex** (client) : lit l'état d'un module MITM externe, envoie des commandes
  de mode via **ESP-NOW**. Le module MITM (firmware) vit désormais dans un **dépôt privé
  séparé** `BOT32-HALDEX` (porté sur la carte Autosport Labs **ESP32-CAN-X2**).
- ✅ **Sécurité** : `block_airbag` forcé ON, 5 s listen-only au boot, confirmations modales,
  aucun ID deviné émis par défaut, série USB toujours actif.

---

## Données live affichées (PC + mobile) — 12 cellules

Levier · MAP (mbar) · Coolant override (TX) · Coolant réel (sniff) · Éthanol % ·
Haldex blocage % · Huile DSG °C · EGT °C · Frein à main ·
Bouton OK · Hazard · Traction Control (ON/OFF). (+ statut WiFi.)

---

## Historique des versions (résumé)

| Version | Apport principal |
|---|---|
| v2.1.0 | Multi-DID UDS + éthanol/Haldex + actions clear DTC + section diag UI |
| v2.2.0 | (Cluster display override — **retiré depuis** en v2.9.0) |
| v2.3.x | Valeur éthanol brute, nettoyage UI, restructure, garde airbag hardcodée |
| v2.4.x | Restructure UI, polling ON par défaut, config trigger clear-engine-fault |
| v2.5.x | Audit complet, toggle auto-trigger CEF, défauts Hazard |
| v2.6.x | **Mode WiFi AP** pour accès mobile + page settings mobile |
| v2.7.0 | Éthanol + Haldex pollés en permanence (pas seulement en BOOST) |
| v2.7.1 | Bench mode : auto-toggle TX + avertissements jumpers |
| v2.8.0 | **Huile DSG / EGT / huile moteur** + sniffers frein à main & bouton OK |
| v2.9.0 | + sniffers Hazard & Traction Control, − cluster display override |
| v2.10.0 | − huile moteur (live data), TC affiché ON/OFF, fix décodage bouton OK (0x5BF) |
| v3.0.0 | Version stable — toutes les fonctions confirmées sur banc (bouton OK validé) |
| **v3.1.0** | **Haldex** : lien ESP-NOW only, 3 modes (STOCK/FWD/50-50), combo FWD (Hazards+TC), UI PC+mobile, coexistence AP/ESP-NOW canal 1. Module MITM porté **ESP32-CAN-X2** (repo privé). |

---

## Pistes / roadmap (non démarrées)

- ⏳ Implémenter la **détection firmware** du déclenchement automatique du Clear Engine Fault
  (séquence Hazard ×3 en 4 s) — la config existe déjà dans les settings.
- ⏳ **Valider Clear Engine Fault** avec une capture fraîche (confirmer l'effacement réel).
- ⏳ Identifier les **témoins individuels** du combiné (warning lights).
- 💡 Web UI pour les boutons du volant (objectif initial du projet, à étoffer).

---

## Notes de reprise

- Lancer Claude Code depuis `C:\Users\AntoineLagrandeur\MK7-cluster` pour recharger la
  **mémoire auto** ; le code est dans `C:\Users\AntoineLagrandeur\BOT32`.
- Avant de flasher après une suppression de fichier : **vider le cache Arduino**
  (voir `CLAUDE.md` §10).
- Dernier incident résolu (2026-05-30) : erreur de compilation « fantôme » sur
  `cluster_override.cpp` → c'était un **cache Arduino périmé**, pas le code source.
- v2.10.0 → v3.0.0 : décodage du bouton OK (0x5BF) corrigé (repos = 0x00, pressé = non-zéro)
  et **confirmé fonctionnel sur banc** (v3.0.0). Tous les sniffers et live data validés.
