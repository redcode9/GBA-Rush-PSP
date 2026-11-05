# wip/perf-method — Metodo A/B, strumentazione, benchmark e suite di misura

- **Branch:** `features/state-of-the-art-development/wip/perf-method`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** PRF-1, PRF-2, PRF-3, PRF-8.6, PRF-10.1 (19 storie, circa 30 gg).
- **File principali:** `scripts/compare-benchmarks.py`, `source/src/perf_*.c`, `tools/`.
- **Dipende da:** `wip/release-and-ci` (ENG-1.6).
- **Nota:** PRF-8.6 e PRF-10.1 sono misure che servono a entrambi i core.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **PRF-2.1** — Tempo di lavoro per frame, frame in ritardo e oltre budget (report v7) · S, 2 gg · dipende da: ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-8.6** — Suite SMC e di riferimento misurata sulla 1.4.3 · M, 3 gg · dipende da: — · PSP: sì · **subito**

## 1.6

- [x] **PRF-1.1** — compare-benchmarks: verdetto esatto su N≥3 run per braccio · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **PRF-1.2** — Gate sugli estremi: p99, massimo robusto, work p95, frame oltre 1,5× budget al minuto · S, 1 gg · dipende da: PRF-1.1, PRF-2.1, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-1.3** — Ordine alternato verificato, ora di cattura e affermazione dichiarata prima · S, 1 gg · dipende da: PRF-1.1, PRF-2.1, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [x] **PRF-1.4** — Archivio delle baseline e tabella per modello generata · M, 2 gg · dipende da: PRF-1.1 · PSP: no
- [ ] **PRF-1.5** — Sessione hardware: baseline 1.6 per PSP-1000 e uno slim · M, 2,5 gg · dipende da: PRF-1.3, PRF-1.4, PRF-2.2, PRF-2.3, PRF-3.1, PRF-4.1, CMP-1.4, ENG-1.6 · PSP: sì · aspetta: PRF-4.1 (`wip/platform`), CMP-1.4 (`wip/open-bios`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-1.6** — Controllo delle prestazioni nel gate di release · S, 1 gg · dipende da: PRF-1.4, F-139, F-118 · PSP: no
- [ ] **PRF-2.2** — Timer di fase su frame campionati: update_gba, update_scanline, PSG audio · M, 2,5 gg · dipende da: PRF-2.1, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-2.3** — Contatori di dispatch per regione e di SMC · S, 1 gg · dipende da: PRF-2.1, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-2.4** — Prelazioni del thread di emulazione, divise tra lavoro e attesa · S, 1 gg · dipende da: PRF-2.1 · PSP: no
- [ ] **PRF-2.5** — Sonda del costo del lookup (solo build diagnostica) · S, 1 gg · dipende da: PRF-2.3, F-137 · PSP: sì
- [ ] **PRF-2.6** — Validazione su hardware della strumentazione · S, 1,5 gg · dipende da: PRF-2.2, PRF-2.4, PRF-3.2 · PSP: sì
- [ ] **PRF-3.1** — Benchmark senza limite da uno slot di stato · M, 2 gg · dipende da: PRF-2.1, ENG-1.6 · PSP: sì · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **PRF-3.2** — Bracci render-off e audio-off (build diagnostica) · S, 1 gg · dipende da: PRF-3.1, F-137 · PSP: sì
- [ ] **PRF-3.3** — Replay di traccia nel bench (build diagnostica) · S, 1 gg · dipende da: PRF-3.1, F-137 · PSP: sì
- [ ] **PRF-3.4** — compare-benchmarks per i bench e rumore misurato su hardware · S, 1 gg · dipende da: PRF-3.1, PRF-1.1 · PSP: sì
- [ ] **PRF-10.1** — Esperimento di padding (il gate) · S, 1,5 gg · dipende da: PRF-3.1, PRF-1.1, F-137 · PSP: sì

## 2.0

- [ ] **PRF-1.7** — Suite pubblica di almeno 40 scene per modello · M, 3 gg · dipende da: PRF-1.4, PRF-1.5, PRF-3.1, ENG-3.2 · PSP: sì · aspetta: ENG-3.2 (`wip/replay-oracle`)
