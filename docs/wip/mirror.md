# wip/mirror — Mirror: due GBA su ogni PSP

- **Branch:** `features/state-of-the-art-development/wip/mirror`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** LNK-12, SOA-7 (12 storie, circa 50 gg).
- **File principali:** `source/src/link_cable.c`, sessione in `link_net.c`, istanza B in `main.c` e `memory.c`.
- **Dipende da:** `wip/new-core-speed` (SOA-4: in main, branch ancora aperto), `wip/media-engine`, `wip/link-rooms` (LNK-7.1), `wip/link-techniques` (LNK-8.1, gate punto 1 con LNK-11), `wip/link-diagnostics` (LNK-1.3, LNK-2.2), `wip/replay-oracle` (F-120), `wip/savestate-v2` (F-007), `wip/perf-method` (F-025), `wip/platform` (F-027), `wip/rewind` (GIO-1.2).
- **Gate:** le storie LNK-12 partono solo con i tre punti del gate superati (LNK-11, poi LNK-12.1 e LNK-12.2 su hardware) e con il verdetto rivisto per iscritto (decisione (n), da confermare). SOA-7.1 e SOA-7.3 sono in 2.0 ma dipendono da LNK-12.1 e LNK-12.3, che sono dopo il gate.
- **Core Classic:** per la decisione (m) il lavoro su `cpu.c`, `mips_emit.h` e `mips_stub.S` è bloccato: le storie di questo branch toccano solo le parti comuni e il core New, e l'istanza B è del core New.
- **Risultati su hardware:** LNK-12.1, LNK-12.2 e LNK-12.7 scrivono righe in `docs/HARDWARE-RESULTS.md`, che devono usare un id di test di `docs/hardware-tests.md` (`scripts/hardware-session.py --check`, parte di make check); il catalogo non ha ancora un test per il budget a due core, per il replay di determinismo né per la sessione a due core: vanno aggiunti prima delle sessioni.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 2.0

- [ ] **SOA-7.1** — Budget a due core con New: macchina B senza rendering né audio · S, 1,5 gg · dipende da: SOA-4.1, LNK-12.1 · PSP: sì · aspetta: SOA-4.1 (`wip/new-core-speed`: codice in main, manca l'accettazione su PSP) · _gli obiettivi li conferma o corregge SOA-2.4 (`wip/hardware-validation`, decisione (o))_
- [ ] **SOA-7.3** — Macchina B senza PPU né mixer: solo lo stato che il gioco osserva · M, 2,5 gg · dipende da: LNK-12.3 · PSP: sì

## dopo gate

- [ ] **LNK-12.1** — Misura del budget a due core (gate 2) · S, 2 gg · dipende da: F-025, F-027, LNK-1.3 · PSP: sì · aspetta: F-025 (PRF-2, `wip/perf-method`), F-027 (PRF-4, `wip/platform`), LNK-1.3 (`wip/link-diagnostics`)
- [ ] **LNK-12.2** — Spike di determinismo tra modelli (gate 3) · M, 3 gg · dipende da: F-120, F-122 · PSP: sì · aspetta: F-120 (ENG-3, `wip/replay-oracle`) · _F-122: ENG-5.1 (in parte) ed ENG-5.4 sono in main con `wip/release-and-ci`; ENG-5.2-5.3 sono bloccate dalla decisione (m) (`wip/classic-core` fermo): va deciso se i test host del core New (`tests/core/`, `make check-core`) ne prendono il posto o se la dipendenza cade_
- [ ] **LNK-12.3** — Istanza B del core · XL, 14 gg · dipende da: LNK-12.8 · PSP: sì · _per la decisione (m) la seconda compilazione è solo del core New e delle parti comuni, senza `mips_stub.S`; per il test di F-122 vale la nota di LNK-12.2_
- [ ] **LNK-12.4** — Scambio degli input e ritardo calcolato dall'RTT · M, 4 gg · dipende da: LNK-12.1, LNK-12.2 · PSP: no
- [ ] **LNK-12.5** — Avvio della sessione a due core · M, 4 gg · dipende da: LNK-12.3, LNK-12.4, LNK-7.1, LNK-8.1, LNK-2.2, F-007 · PSP: no · aspetta: LNK-7.1 (`wip/link-rooms`), LNK-8.1 (`wip/link-techniques`), LNK-2.2 (`wip/link-diagnostics`), F-007 (SAV-4, `wip/savestate-v2`)
- [ ] **LNK-12.6** — Impronta di stato e fine pulita · M, 3 gg · dipende da: LNK-12.5, F-120 · PSP: no · aspetta: F-120 (ENG-3, `wip/replay-oracle`)
- [ ] **LNK-12.7** — Accettazione su hardware · S, 2 gg · dipende da: LNK-12.6 · PSP: sì
- [ ] **LNK-12.8** — Cache e stato del core dietro puntatori, senza seconda istanza · M, 4 gg · dipende da: LNK-12.1, LNK-12.2 · PSP: sì · _parte CPU in gran parte fatta dal core New; per la decisione (m) la parte Classic (cache di `cpu.c`, `mips_stub.S`) è bloccata: restano lo stato comune (`memory.c`, `update_gba`, suono, array delle cache usati anche dal core New) e gli statici di `arm_gba.c`_
- [ ] **SOA-7.2** — (condizionata) Spike: la CPU della macchina B sul Media Engine · L, 5 gg · dipende da: SOA-6.1, SOA-7.1 · PSP: sì · aspetta: SOA-6.1 (`wip/media-engine`) · _la scelta tra renderer e CPU di B sul ME la prepara SOA-2.4 (`wip/hardware-validation`)_
- [ ] **SOA-7.4** — (condizionata) Rollback di 1-2 frame con snapshot in memoria · L, 5 gg · dipende da: SOA-7.3, GIO-1.2 · PSP: sì · aspetta: GIO-1.2 (`wip/rewind`)
