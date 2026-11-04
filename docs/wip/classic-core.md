# wip/classic-core — Correttezza del core Classic

- **Branch:** `features/state-of-the-art-development/wip/classic-core`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-5 (tranne CMP-5.1), CMP-11 (tranne CMP-11.1), CMP-12 (tranne CMP-12.1, CMP-12.2), ENG-5.2, ENG-5.3 (13 storie, circa 21 gg).
- **File principali:** `source/src/cpu.c`, `mips_emit.h`, `mips_stub.S`.
- **Dipende da:** `wip/test-roms`, `wip/replay-oracle` (ENG-3.3), `wip/perf-method` (PRF-1.5).
- **Nota:** Attivo finché il Classic è il predefinito (decisione (m)).
- **Stato:** BLOCCATO per decisione dell'utente del 2026-11-04 (decisione (m) in `docs/BACKLOG.md`): nessun lavoro sul core Classic. Il branch resta aperto ma fermo; le correzioni che servono ai giochi si fanno nel core New.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **CMP-5.2** — Percorso veloce con test della regione di SP e trampolino EWRAM · M, 2,5 gg · dipende da: CMP-5.1, F-124, PRF-1.1, PRF-1.5, F-120 · PSP: sì · aspetta: CMP-5.1 (`wip/test-roms`), PRF-1.1 (`wip/perf-method`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-11.2** — NZCV vivi in MSR CPSR_c e PC+12 per Rn con shift per registro · S, 0,75 gg · dipende da: CMP-11.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-11.3** — BX verso ARM con il bit 1 a 1: si prosegue dalla parola sotto · S, 0,5 gg · dipende da: CMP-11.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-11.4** — Banco FIQ completo in set_cpu_mode e nei trasferimenti ^ · M, 1,5 gg · dipende da: CMP-11.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-11.5** — LDM/STM ^ senza PC: indirizzo dalla base del modo corrente · S, 1 gg · dipende da: CMP-11.1, CMP-11.4, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-11.6** — IRQ già pendente preso subito quando una scrittura di CPSR lo sblocca · S, 1 gg · dipende da: CMP-11.1, CMP-3.2, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), CMP-3.2 (`wip/open-bios`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-12.3** — Cicli dei salti e delle istruzioni saltate come sull'ARM7TDMI · M, 1,5 gg · dipende da: CMP-12.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-12.4** — Store multipli su codice tradotto e su I/O: ogni parola controllata · M, 3,5 gg · dipende da: CMP-12.1, PRF-8.6, ENG-2.2, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), PRF-8.6 (`wip/perf-method`), ENG-2.2 (`wip/release-and-ci`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-12.5** — Finestra di prefetch: uno store sulle due istruzioni già prelevate non le cambia · L, 4 gg · dipende da: CMP-12.1, CMP-12.4, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`)
- [ ] **ENG-5.2** — Encoder MIPS in un header puro e test delle codifiche · M, 1,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-5.3** — Harness host per lo stato del core · M, 1,5 gg · dipende da: F-004 · PSP: no

## dopo gate

- [ ] **CMP-5.3** — (condizionato) Controllo SMC sugli store dello stack in IWRAM · M, 1,5 gg · dipende da: CMP-5.2, PRF-1.1 · PSP: sì · aspetta: PRF-1.1 (`wip/perf-method`)
- [ ] **CMP-12.6** — (condizionata) Byte dell'indirizzo esatto nelle letture a 16 e 32 bit dal backup · S, 0,25 gg · dipende da: CMP-12.1, ENG-3.3 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`)
