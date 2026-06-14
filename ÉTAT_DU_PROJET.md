# État du projet BOT32

> Photo lisible de l'état du projet. Mise à jour : **2026-06-12**.
> Pour les règles et conventions techniques, voir **`CLAUDE.md`**.

---

## Version actuelle

🏁 **v4.4.0** (principal) + **BOT32-HALDEX v3.0.0** (module MITM privé, ESP32-CAN-X2) — **jalon stable.**
Repo public `ALagrandeur/BOT32` (`master`) ; module MITM = dépôt **privé** `BOT32-HALDEX`.

**Les 3 modes fonctionnent en voiture, confirmés par le pilote.** **STOCK** (normal),
**FWD** (roues avant seulement — le burnout fonctionne), et surtout **50-50 qui TIENT
~95 % d'engagement à TOUTES les vitesses** (le gros morceau enfin résolu). Lien ESP-NOW
solide, contrôle depuis l'UI (modes + passthrough + ack).

### Nouveautés de ce jalon (BOT32-HALDEX v2.0.0 → v3.0.0)
- **50-50 RÉSOLU (le gros morceau).** Découverte clé : le Haldex Gen5 est *feed-forward*
  (piloté par le **couple**, pas le glissement) ET il **réduit l'engagement quand la
  VITESSE monte** — et on lui transmettait la vraie vitesse, donc le lock mourait vers
  60 km/h. Fix : faire croire au Haldex qu'il est **immobile + au ralenti** (vitesse/
  roues/RPM neutralisés côté Haldex) → il ne réduit plus → **95 % tenu à toute vitesse**.
  *(Recette détaillée dans le dépôt privé.)*
- **Angle volant** : seul facteur qui baisse encore le 50-50 — **gardé volontairement**
  (un accouplement 100 % verrouillé bloque la transmission en virage).

### Ajouts principal v4.0.0 → v4.4.0
- **v4.2.0 — Temp embrayage Haldex** : DID UDS **0x2BF1** (poll via le Haldex, à travers
  le pont X2) affichée dans l'UI mobile à la place de « Dernier paquet ». Statut **armé**
  déplacé dans un badge d'en-tête 🟢/🔴 (carte Passthrough retirée ; bouton d'armement
  dans « Tous les réglages »).
- **v4.3.0 — Coupure thermique** : limite ajustable (défaut **150 °C**) dans les réglages ;
  si la temp embrayage atteint la limite → **retour STOCK auto + verrou** (bloque le ré-armement
  jusqu'à refroidir de 10 °C). Nécessaire car le 50-50 speed-spoof contourne la protection
  vitesse du Haldex. *(Échelle temp `×0.75−48` encore à calibrer vs température réelle.)*
- **Voyant shift-lock (0x394) abandonné** : émis par le Gateway + porte le rapport P/R/N/D
  → l'injection parallèle le ferait clignoter / glitcherait l'affichage du rapport. Mode dans l'app.
- **v4.4.0 — Ménage menu réglages (mobile)** : retiré Cluster bus IDs + Bench test ;
  OBD2 réduit à « Poll Hz » ; « TX » → « Diffusion TX » ; Clear Engine Fault = bouton seul.
  + **affichage du raw `0x2BF1`** sous la temp (page Live) pour **calibrer la formule** vs VCDS
  (la formule `×0.75−48` reste provisoire en attendant 2 points raw+VCDS).

---

## Module MITM Haldex (BOT32-HALDEX, ESP32-CAN-X2) — résumé technique

- **Topologie en série** : coupe le stub CAN du calculateur Haldex. CAN1 = TWAI natif
  (côté CAR/PCM, GPIO7/6) ; CAN2 = MCP2515 sur HSPI (côté Haldex).
- **Pont CAR ↔ Haldex** dans une **tâche FreeRTOS dédiée** démarrée AVANT le WiFi →
  écart de boot minimal (évite les défauts TC/TPMS au démarrage en série).
- **CRC E2E AUTOSAR** (poly 0x2F, init 0xFF, xorout 0xFF) sur `[D2..D8, DataID]` —
  **DataID APPENDÉ** (confirmé 100 % sur 5 logs réels). Compteur = `D2 & 0x0F`.
- **Réécriture des trames** (FWD inchangé, ça marche) : **FWD** → demande 0x00 + roues
  à 0. **50-50** → épingle le couple + plafond ET fait croire au Haldex qu'il est
  **immobile + au ralenti** (vitesse/roues/RPM neutralisés côté Haldex) pour qu'il ne
  réduise pas l'engagement à vitesse. Le **frein n'est PAS simulé** (freiner-en-roulant
  = 0 % d'engagement, le simuler tuerait le lock). *(Valeurs exactes + table CRC : privé.)*
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
| v3.9.0 | **Power-save WiFi OFF** (lien ESP-NOW fiable) + lecture du canal réel |
| v3.10.0 | **FWD sort quand le TC repasse à ON** (plus quand Hazards OFF) — état TC latché |
| **v4.0.0** | 🏁 **Jalon stable** — tout confirmé en voiture (STOCK/FWD/50-50, lien solide, UI complète) |

### Module MITM (BOT32-HALDEX)
| Version | Apport principal |
|---|---|
| v0.3–0.4 | Persistance état + spoof vitesses de roue (FWD) |
| v0.7.x | **CRC E2E corrigé** (DataID appendé) + 0x116=0xAC / 0x106=0x07 + 50-50 demande 0xFA |
| v1.1.0 | Jeu de trames complet (0x116/0x106/0x0B2) sur FWD ET 50-50, sans seuil pédale |
| v1.3.0 | Pont en **tâche FreeRTOS avant WiFi** (fix défauts TC/TPMS au boot) |
| v1.4.x | **12 V permanent** + light-sleep 15 min + wake-on-CAN + récup bus-off MCP/TWAI + TEC |
| v1.5.0 | **Power-save WiFi OFF** + diagnostics ESP-NOW (canal/tx/rxcmd) |
| v1.6.0 | **Fix 50-50** : 0x0A8 en passthrough (D8 = RPM, pas la demande de lock) |
| **v2.0.0** | 🏁 **Jalon stable** — MITM confirmé en voiture (STOCK/FWD/50-50), lien ESP-NOW solide |

---

## Pistes / roadmap (à faire — captures en attente)

- 📌 **Log Haldex 50-50 en roulant** (côté Haldex) : confirmer que l'engagement (0x118)
  tient sous charge, et tester **0x116** haut/bas/off (seul octet ESP incertain). Comparer
  à un run STOCK. → décide s'il faut affiner le 50-50.
- 📌 **Log cluster voyant** : capturer les **2 payloads** (allumé + éteint) d'un témoin pour
  pouvoir le faire clignoter (indicateur FWD au combiné — la trame « éteinte » manquait).
- ⏳ Implémenter la **détection firmware** du déclenchement auto du Clear Engine Fault (Hazard ×3 / 4 s).
- ⏳ **Valider Clear Engine Fault** avec une capture fraîche.
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
- **Jalon v4.0.0 / v2.0.0 (2026-06-06)** : version **stable**, tout confirmé en voiture par le
  pilote. Inclut le fix FWD-sort-sur-TC (v3.10.0) et le fix 50-50 0x0A8 (v1.6.0). Prochaines
  étapes = les 2 captures en attente (log Haldex 50-50 + log cluster voyant).
