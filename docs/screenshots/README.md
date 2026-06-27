# BOT32 — Mobile UI screenshots

Captures de l'interface web mobile (servie par l'ESP32 Main sur son AP WiFi).

> ℹ️ Ces captures datent de **v4.4.0**. En **v4.5.0** (version finale), la temp
> embrayage est en **affichage seul** : le champ « Limite temp embrayage » et le
> « raw 0x2BF1 » visibles ici ont été **retirés** (la protection thermique native du
> Haldex suffit). Le reste de l'UI est identique.

| Capture | Description |
|---|---|
| ![Réglages — haut](mobile-settings-top.jpg) | **Réglages (haut)** — MAP → Gauge mapping, OBD2 (Poll Hz), Diffusion TX |
| ![Live — Haldex MITM](mobile-live-haldex.jpg) | **Live — Haldex (MITM)** — badge armé 🟢/🔴, mode, vitesse/pédale, lock target, pump %, **temp embrayage + raw 0x2BF1**, boutons STOCK/FWD/50-50 |
| ![Live — grille principale](mobile-live-main.jpg) | **Live — grille principale** — lever, MAP, coolant TX/réel, éthanol, Haldex %, DSG/EGT, sniffers (frein à main, OK, hazard, TC) |
| ![Réglages — Haldex](mobile-settings-haldex.jpg) | **Réglages (bas)** — bouton Clear Engine Fault, WiFi AP (mot de passe masqué), Haldex link (MAC, **limite temp embrayage**, Armer/Désarmer) |
