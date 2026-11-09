# wip/rewind — Riavvolgimento

- **Branch:** `features/state-of-the-art-development/wip/rewind`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** GIO-1, GIO-2 (13 storie, circa 41,5 gg).
- **File principali:** passo dell'ospite e hook di frame in `source/src/main.c`, rewind (nuovo).
- **Dipende da:** `wip/savestate-v2`, `wip/perf-method`, `wip/replay-oracle`, `wip/platform`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 2.0

- [ ] **GIO-1.1** — Modulo puro rewind_ring: delta XOR all'indietro, RLE degli zeri, anello, regola di atterraggio · M, 3,5 gg · dipende da: — · PSP: no · **subito** · _in parte: codice e test pronti (5bb8b56); throughput del codec sull'host, solo come informazione e senza alcuna affermazione sul PSP: tra 0,04 e 0,08 ms per cattura delle sei aree (387 KiB) più 4 KiB di stato piccolo con 0-1000 parole cambiate, circa 0,2 ms con 10.000, sola cattura senza le scritture della sonda (x86-64, clang -O2, migliore di 5 esecuzioni); manca il verde di make check e make test-sanitize nella CI_
- [ ] **GIO-1.2** — Snapshot e ripristino in memoria, salvataggio protetto · M, 3 gg · dipende da: GIO-1.1, F-007, ENG-3.1, ENG-3.3 · PSP: sì · aspetta: F-007 (`wip/savestate-v2`), ENG-3.1 (`wip/replay-oracle`), ENG-3.3 (`wip/replay-oracle`)
- [ ] **GIO-1.3** — Cattura a fine frame nel margine misurato · M, 3 gg · dipende da: GIO-1.2, F-025, F-027 · PSP: sì · aspetta: F-025 (`wip/perf-method`), F-027 (`wip/platform`)
- [ ] **GIO-1.4** — Comando Riavvolgi, indicatore e opzioni in cinque lingue · L, 5 gg · dipende da: GIO-1.3, F-045, LNK-3.2 · PSP: sì · aspetta: F-045 (`wip/ui-foundation`), LNK-3.2 (`wip/link-diagnostics`)
- [ ] **GIO-1.5** — Misure sul campo e soglia d'uscita · M, 2 gg · dipende da: GIO-1.4, F-024, ENG-3.1, ENG-3.3, ENG-1.6 · PSP: sì · aspetta: F-024 (`wip/perf-method`), ENG-3.1 (`wip/replay-oracle`), ENG-3.3 (`wip/replay-oracle`)
- [ ] **GIO-1.7** — Eventi di sessione: stato caricato, reset, link, sleep, uscita · M, 2,5 gg · dipende da: GIO-1.4, SAV-7.5, ENG-1.6, F-029 solo se l'anello sta nella partizione volatile · PSP: sì · aspetta: SAV-7.5 (`wip/suspend-resume`), F-029 (`wip/platform`) solo con l'anello nella partizione volatile

## dopo gate

- [ ] **GIO-1.6** — Mappa delle pagine scritte di EWRAM e VRAM (condizionata: solo se GIO-1.5 fallisce il gate) · L, 6 gg · dipende da: GIO-1.5, F-120, F-124 · PSP: sì · aspetta: F-120 (`wip/replay-oracle`), F-124 (`wip/classic-core`)
- [ ] **GIO-2.1** — Log per frame di input, contesto e letture del salvataggio · M, 2,5 gg · dipende da: GIO-1.4, ENG-3.2 · PSP: no · aspetta: ENG-3.2 (`wip/replay-oracle`)
- [ ] **GIO-2.2** — update_gba diviso in passo dell'ospite e hook di frame · L, 4 gg · dipende da: F-120, F-024 · PSP: sì · aspetta: F-120 (`wip/replay-oracle`), F-024 (`wip/perf-method`)
- [ ] **GIO-2.3** — Riesecuzione dei tratti e presentazione all'indietro · L, 5 gg · dipende da: GIO-2.1, GIO-2.2, CMP-7.3 · PSP: sì · aspetta: CMP-7.3 (`wip/game-compat`)
- [ ] **GIO-2.4** — Audio invertito · S, 1,5 gg · dipende da: GIO-2.3 · PSP: sì
- [ ] **GIO-2.5** — Verifica delle riesecuzioni con gli hash per frame · M, 2,5 gg · dipende da: GIO-2.3, F-120, ENG-1.6 · PSP: sì · aspetta: F-120 (`wip/replay-oracle`)
- [ ] **GIO-2.6** — Politica di memoria per modello · S, 1 gg · dipende da: GIO-2.3, F-027 · PSP: sì · aspetta: F-027 (`wip/platform`)
