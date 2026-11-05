# wip/replay-oracle — Replay deterministico e reti di regressione

- **Branch:** `features/state-of-the-art-development/wip/replay-oracle`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-3, ENG-4, ENG-6, ENG-7, ENG-11, ENG-13 (25 storie, circa 42 gg).
- **File principali:** `state_hash.c` (nuovo), tracce di input, script PPSSPP.
- **Dipende da:** `wip/dev-environment` (SOA-1.4), `wip/test-roms`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [x] **ENG-3.1** — Modulo puro state_hash · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-3.2** — Traccia v2 con stato iniziale compagno · M, 1,5 gg · dipende da: ENG-3.1 · PSP: sì · _codice pronto (9484557), manca la prova su PSP_
- [ ] **ENG-3.3** — Cattura degli hash su PSP durante il replay · M, 1,5 gg · dipende da: ENG-3.1, ENG-3.2, ENG-2.2 · PSP: sì · aspetta: ENG-2.2 (`wip/release-and-ci`)
- [x] **ENG-3.4** — Strumento di confronto e procedure di autoverifica e perturbazione · S, 1 gg · dipende da: ENG-3.1 · PSP: no
- [ ] **ENG-3.5** — Corpus pubblico di replay e golden compatti · M, 2,25 gg · dipende da: ENG-3.3, ENG-3.4, ENG-10.3, SAV-2.6, CMP-1.1, CMP-3.1, CMP-11.1, CMP-12.1, UI-10.5 · PSP: sì · aspetta: ENG-10.3 (`wip/test-roms`), SAV-2.6 (`wip/test-roms`), CMP-1.1 (`wip/open-bios`), CMP-3.1 (`wip/test-roms`), CMP-11.1 (`wip/test-roms`), CMP-12.1 (`wip/test-roms`), UI-10.5 (`wip/ui-ge`)
- [ ] **ENG-3.6** — Registrazione e replay anche nelle build release · S, 1 gg · dipende da: ENG-3.2, F-027 · PSP: sì
- [ ] **ENG-3.7** — Replay deterministico sui titoli con RTC · S, 0,5 gg · dipende da: ENG-3.2, CMP-7.3 · PSP: sì · aspetta: CMP-7.3 (`wip/game-compat`)
- [ ] **ENG-6.1** — Spike: EBOOT in PPSSPP headless fissato · S, 1,5 gg · dipende da: ENG-2.2, CMP-1.1 · PSP: no · aspetta: ENG-2.2 (`wip/release-and-ci`), CMP-1.1 (`wip/open-bios`)
- [ ] **ENG-6.2** — Autorun diagnostico · M, 1,5 gg · dipende da: ENG-2.2, ENG-2.4, ENG-1.6 · PSP: sì · aspetta: ENG-2.2 (`wip/release-and-ci`), ENG-2.4 (`wip/release-and-ci`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **ENG-6.6** — Modalità testpat su PSP con autorun e foto · S, 0,5 gg · dipende da: ENG-6.2 · PSP: sì

## 2.0

- [ ] **ENG-7.1** — Modulo puro net_fault con i sei profili · S, 1,25 gg · dipende da: — · PSP: no · **subito** · _in parte: manca check-rom-link (psp-stalls con i semi 4, 9 e 21, mksc-psp ad almeno 57 fps, mksc-psp-lossy ad almeno 53 fps), che richiede mGBA 0.10, le ROM e il BIOS_
- [ ] **ENG-7.2** — Manopole di guasto nel worker ad-hoc (profilo diagnostico) · M, 1,5 gg · dipende da: ENG-7.1, ENG-6.2 · PSP: sì
- [ ] **ENG-7.3** — Gate a due istanze PPSSPP prima di ogni bump di LINK_NET_VERSION o di LINK_CABLE_PROTOCOL · M, 2,5 gg · dipende da: ENG-6.1, ENG-6.2, ENG-1.2, CMP-1.1 · PSP: no · aspetta: ENG-1.2 (`wip/release-and-ci`), CMP-1.1 (`wip/open-bios`)

## dopo gate

- [ ] **ENG-4.1** — Modulo puro input_script · M, 2 gg · dipende da: ENG-3.4 · PSP: no
- [ ] **ENG-4.2** — Integrazione su PSP nel profilo diagnostico · M, 1,5 gg · dipende da: ENG-4.1, ENG-3.3, ENG-6.2 · PSP: sì
- [ ] **ENG-4.3** — Tabelle di indirizzi verificate · M, 1,5 gg · dipende da: ENG-4.2 · PSP: sì
- [ ] **ENG-6.3** — Suite e2e: avvio, salvataggi, soak · M, 2,5 gg · dipende da: ENG-6.1, ENG-6.2, SAV-2.6, F-002 · PSP: no · aspetta: SAV-2.6 (`wip/test-roms`)
- [ ] **ENG-6.4** — Golden video del framebuffer del core · M, 2 gg · dipende da: ENG-6.3, ENG-3.5 · PSP: no
- [ ] **ENG-6.5** — Pattern di colori GE per ogni scala e filtro · S, 1 gg · dipende da: ENG-6.1, ENG-6.6 · PSP: no
- [ ] **ENG-11.1** — Passaggio via USB solo diagnostico · L, 3,25 gg · dipende da: ENG-2.2, ENG-6.2, ENG-3.3 · PSP: sì · aspetta: ENG-2.2 (`wip/release-and-ci`)
- [ ] **ENG-11.2** — Loop sul PC con oracolo esatto e A/B · M, 2,25 gg · dipende da: ENG-11.1, ENG-10.2 · PSP: sì · aspetta: ENG-10.2 (`wip/link-diagnostics`)
- [ ] **ENG-11.3** — Sleep e wake con un relè sull'interruttore POWER · M, 2 gg · dipende da: ENG-11.2 · PSP: sì
- [ ] **ENG-11.4** — Latenza dall'input alla foto e raster su hardware · M, 1,5 gg · dipende da: ENG-10.3, PRF-11.5 · PSP: sì · aspetta: ENG-10.3 (`wip/test-roms`), PRF-11.5 (`wip/media-engine`)
- [ ] **ENG-13.1** — Visualizzatore interattivo per la build qemu · L, 3 gg · dipende da: ENG-12.2 · PSP: no · aspetta: ENG-12.2 (`wip/classic-core-speed`)
- [ ] **ENG-13.2** — Due build desktop collegate via UDP con guasti iniettati · M, 2 gg · dipende da: ENG-13.1, ENG-7.1 · PSP: no
