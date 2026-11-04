# wip/classic-core-speed — Velocità del core Classic

- **Branch:** `features/state-of-the-art-development/wip/classic-core-speed`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-12, ENG-14 (tranne ENG-14.4), PRF-7, PRF-8 (tranne PRF-8.6), PRF-9 (tranne PRF-9.1), PRF-10 (tranne PRF-10.1) (35 storie, circa 76,5 gg).
- **File principali:** `source/src/cpu.c`, `mips_emit.h`, `mips_stub.S`.
- **Dipende da:** `wip/classic-core`, `wip/perf-method`.
- **Nota:** Congelato dalla decisione (m) fino a SOA-10.1, tranne ENG-14.1, che vale per entrambi i core.
- **Stato:** BLOCCATO per decisione dell'utente del 2026-11-04 (decisione (m) in `docs/BACKLOG.md`): nessun lavoro sul core Classic. Il branch resta aperto ma fermo; le correzioni che servono ai giochi si fanno nel core New.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **ENG-14.1** — Guardia sulle invalidazioni del JIT · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **PRF-9.2** — Contatori delle scritture a IE, IF e IME e dei cambi di modo (build diagnostica) · S, 0,75 gg · dipende da: PRF-2.3, F-137 · PSP: no · aspetta: PRF-2.3 (`wip/perf-method`)
- [ ] **PRF-9.5** — Cicli spesi nelle attese pure (build diagnostica, misura del gate) · S, 0,5 gg · dipende da: PRF-2.2, F-137 · PSP: sì · aspetta: PRF-2.2 (`wip/perf-method`)

## dopo gate

- [ ] **ENG-12.1** — Spike: build mipsel-linux · M, 2 gg · dipende da: ENG-3.3, ENG-5.2, CMP-1.1 · PSP: no · aspetta: ENG-3.3 (`wip/replay-oracle`), ENG-5.2 (`wip/classic-core`), CMP-1.1 (`wip/open-bios`)
- [ ] **ENG-12.2** — Oracolo su corpus con controllo e mutanti · L, 3 gg · dipende da: ENG-12.1, ENG-12.6, ENG-3.5, ENG-2.5 · PSP: no · aspetta: ENG-3.5 (`wip/replay-oracle`), ENG-2.5 (`wip/release-and-ci`)
- [ ] **ENG-12.3** — (facoltativa) La build qemu al posto di mGBA in check-rom-link · M, 3 gg · dipende da: ENG-12.2, ENG-7.1 · PSP: no · aspetta: ENG-7.1 (`wip/replay-oracle`)
- [ ] **ENG-12.4** — Controllo del codice stantio a ogni istruzione eseguita, sul corpus della build qemu · S, 1 gg · dipende da: ENG-12.2, CMP-12.4 · PSP: no · aspetta: CMP-12.4 (`wip/classic-core`)
- [ ] **ENG-12.5** — (condizionata) Interprete di riferimento in lockstep per blocco nella build qemu · L, 6 gg · dipende da: ENG-12.2, CMP-11.1-11.6, ENG-10.4 · PSP: no · aspetta: CMP-11.1 (`wip/test-roms`), CMP-11.2 (`wip/classic-core`), CMP-11.3 (`wip/classic-core`), CMP-11.4 (`wip/classic-core`), CMP-11.5 (`wip/classic-core`), CMP-11.6 (`wip/classic-core`), ENG-10.4 (`wip/dev-environment`) · _non serve al core New (interprete IR)_
- [ ] **ENG-12.6** — Corpus locale della build qemu registrato su PSP · M, 2 gg · dipende da: ENG-12.1, ENG-3.2, ENG-3.3, PRF-8.6 · PSP: sì · aspetta: ENG-3.2 (`wip/replay-oracle`), ENG-3.3 (`wip/replay-oracle`), PRF-8.6 (`wip/perf-method`)
- [ ] **ENG-14.2** — Scansione diagnostica del lato dati · M, 1,5 gg · dipende da: ENG-2.2, ENG-3.5 · PSP: sì · aspetta: ENG-2.2 (`wip/release-and-ci`), ENG-3.5 (`wip/replay-oracle`)
- [ ] **ENG-14.3** — Plugin qemu col modello delle cache Allegrex · L, 3,5 gg · dipende da: ENG-12.2 · PSP: no
- [ ] **PRF-7.1** — Modulo puro dispatch_cache · S, 1 gg · dipende da: PRF-2.3, PRF-2.5 · PSP: no · aspetta: PRF-2.3 (`wip/perf-method`), PRF-2.5 (`wip/perf-method`)
- [ ] **PRF-7.2** — Sonda asm negli stub indiretti e riempimento nel lookup · M, 2,5 gg · dipende da: PRF-7.1, F-120 · PSP: sì
- [ ] **PRF-7.3** — Contatori hit, miss e clear, e interruttore a run-time · S, 1 gg · dipende da: PRF-7.2, ENG-15.4, CMP-1.4 · PSP: no · aspetta: ENG-15.4 (`wip/crash-safety`), CMP-1.4 (`wip/open-bios`)
- [ ] **PRF-7.4** — Stress di invalidazione e mutante · M, 2 gg · dipende da: PRF-7.2, F-120, F-137 · PSP: sì
- [ ] **PRF-7.5** — A/B su hardware della dispatch cache · M, 2 gg · dipende da: PRF-7.3, PRF-7.4, PRF-3.1, PRF-1.1, PRF-1.3 · PSP: sì · aspetta: PRF-3.1 (`wip/perf-method`), PRF-1.1 (`wip/perf-method`), PRF-1.3 (`wip/perf-method`)
- [ ] **PRF-7.6** — Voci RAM a invalidazione precisa e stub _dual (con gate) · L, 4,5 gg · dipende da: PRF-7.5, PRF-2.3, PRF-8.6 · PSP: sì · aspetta: PRF-2.3 (`wip/perf-method`), PRF-8.6 (`wip/perf-method`)
- [ ] **PRF-8.1** — Punti caldi e classi delle trappole SMC (build diagnostica) · S, 2 gg · dipende da: PRF-2.3, F-137 · PSP: no · aspetta: PRF-2.3 (`wip/perf-method`)
- [ ] **PRF-8.2** — Gate SMC appresi una volta e spediti come dati · M, 2 gg · dipende da: PRF-8.1, CMP-2.2, F-120 · PSP: sì · aspetta: CMP-2.2 (`wip/game-compat`)
- [ ] **PRF-8.3** — Prologo stabile come thunk dei blocchi RAM · L, 4 gg · dipende da: PRF-7.5, F-120, F-124 · PSP: sì
- [ ] **PRF-8.4** — Collegamenti diretti RAM→RAM attraverso i thunk · M, 3 gg · dipende da: PRF-8.3 · PSP: sì
- [ ] **PRF-8.5** — Verifica su hardware e interruttore a run-time · M, 2 gg · dipende da: PRF-8.4, ENG-15.4, CMP-1.4, PRF-1.1, PRF-1.3 · PSP: sì · aspetta: ENG-15.4 (`wip/crash-safety`), CMP-1.4 (`wip/open-bios`), PRF-1.1 (`wip/perf-method`), PRF-1.3 (`wip/perf-method`)
- [ ] **PRF-8.7** — Store dello stesso valore su codice senza trappola (con gate) · M, 2 gg · dipende da: CMP-12.4, CMP-5.2, CMP-5.3, PRF-8.6, PRF-2.3, F-120 · PSP: sì · aspetta: CMP-12.4 (`wip/classic-core`), CMP-5.2 (`wip/classic-core`), CMP-5.3 (`wip/classic-core`), PRF-8.6 (`wip/perf-method`), PRF-2.3 (`wip/perf-method`)
- [ ] **PRF-8.8** — Patch in place degli immediati riscritti dal gioco (con gate) · L, 4 gg · dipende da: PRF-8.7, PRF-8.1, F-124, F-120 · PSP: sì
- [ ] **PRF-8.9** — Costo della traduzione RAM (con gate) · S, 1,5 gg · dipende da: PRF-8.6, PRF-2.2, F-120 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`), PRF-2.2 (`wip/perf-method`)
- [ ] **PRF-8.10** — add/sub pc, pc, #imm dentro il blocco come salto interno (con gate) · S, 1 gg · dipende da: PRF-8.6, PRF-8.1, PRF-2.3, F-120, F-124, ENG-15.4 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`), PRF-2.3 (`wip/perf-method`), ENG-15.4 (`wip/crash-safety`)
- [ ] **PRF-8.11** — Misura d'uscita di PRF-8 sull'ultima build · M, 3 gg · dipende da: PRF-8.6, PRF-3.1, PRF-4.1, CMP-12.4, F-120 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`), PRF-3.1 (`wip/perf-method`), PRF-4.1 (`wip/platform`), CMP-12.4 (`wip/classic-core`)
- [ ] **PRF-9.3** — Percorso rapido asm per gli store a IE, IF e IME (con gate) · M, 2,5 gg · dipende da: PRF-9.2, F-120, ENG-15.4, CMP-1.4, PRF-1.1, PRF-1.3 · PSP: sì · aspetta: ENG-15.4 (`wip/crash-safety`), CMP-1.4 (`wip/open-bios`), PRF-1.1 (`wip/perf-method`), PRF-1.3 (`wip/perf-method`)
- [ ] **PRF-9.4** — Cambi di modo della CPU senza chiamata C (con gate) · M, 2,5 gg · dipende da: PRF-9.2, CMP-11.2-11.6, F-120, F-124, ENG-15.4 · PSP: sì · aspetta: CMP-11.2 (`wip/classic-core`), CMP-11.3 (`wip/classic-core`), CMP-11.4 (`wip/classic-core`), CMP-11.5 (`wip/classic-core`), CMP-11.6 (`wip/classic-core`), ENG-15.4 (`wip/crash-safety`)
- [ ] **PRF-9.6** — Salto esatto dei cicli di attesa puri (con gate) · L, 3,5 gg · dipende da: PRF-9.5, PRF-8.6, F-120, F-124, ENG-15.4, CMP-3.1 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`), ENG-15.4 (`wip/crash-safety`), CMP-3.1 (`wip/test-roms`)
- [ ] **PRF-10.2** — Cache JIT allineate a 8 KiB con offset fisso · S, 0,5 gg · dipende da: PRF-10.1, F-120 · PSP: sì · aspetta: PRF-10.1 (`wip/perf-method`)
- [ ] **PRF-10.3** — Ordine di link del percorso caldo (spike con LTO) · M, 2 gg · dipende da: PRF-10.1 · PSP: sì · aspetta: PRF-10.1 (`wip/perf-method`)
- [ ] **PRF-10.4** — Report del budget caldo e controllo di build · S, 1,75 gg · dipende da: PRF-10.3, F-138 · PSP: no
- [ ] **PRF-10.5** — A/B su hardware del layout fissato · S, 1 gg · dipende da: PRF-10.2, PRF-10.3 · PSP: sì
- [ ] **PRF-10.6** — Dati caldi degli stub a un offset fisso di set della D-cache (con gate) · S, 1,5 gg · dipende da: PRF-10.1, PRF-10.4 · PSP: sì · aspetta: PRF-10.1 (`wip/perf-method`)
