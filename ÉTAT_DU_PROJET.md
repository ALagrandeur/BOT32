# État du projet BOT32

> Photo lisible de l'état du projet. Mise à jour : **2026-05-30**.
> Pour les règles et conventions techniques, voir **`CLAUDE.md`**.

---

## Version actuelle

**v3.0.0** — version stable, taggée `v3.0.0` et poussée sur GitHub (`ALagrandeur/BOT32`, `master`).
Release : https://github.com/ALagrandeur/BOT32/releases/tag/v3.0.0
Toutes les fonctions sont **confirmées fonctionnelles sur banc** (incl. bouton OK).

`SETTINGS_VERSION = 18` (inchangé — réglages préservés) · `obd2_poll_hz` défaut = 30 Hz (5 slots round-robin).

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
| **v3.0.0** | **Version stable** — toutes les fonctions confirmées sur banc (bouton OK validé) |

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
