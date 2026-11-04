# wip/event-scheduler — Scheduler a scadenza unica e timer dal vivo

- **Branch:** `features/state-of-the-art-development/wip/event-scheduler`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-12.2, PRF-9.1, SOA-5 (7 storie, circa 14,5 gg).
- **File principali:** `update_gba` in `source/src/main.c`, timer e DMA in `memory.c`, `event_queue.c` (nuovo).
- **Dipende da:** `wip/test-roms` (CMP-12.1), `wip/replay-oracle` (ENG-3.3).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **CMP-12.2** — Timer letti dal vivo, avviati e fermati al ciclo giusto · M, 3 gg · dipende da: CMP-12.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`) · _sostituita da SOA-5.3: si chiude con quella_
- [ ] **SOA-5.1** — Modulo puro event_queue con test host · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **SOA-5.2** — update_gba guidato dalla coda: righe, timer, DMA e SIO come eventi · L, 4 gg · dipende da: SOA-5.1, CMP-12.1 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`)
- [ ] **SOA-5.3** — Timer letti dal vivo, avviati e fermati al ciclo (sostituisce CMP-12.2) · M, 2 gg · dipende da: SOA-5.2 · PSP: sì
- [ ] **SOA-5.4** — Eventi di riga saltati quando nessuno li osserva · M, 2 gg · dipende da: SOA-5.2 · PSP: sì
- [ ] **SOA-5.5** — Verifica su due modelli: core-rom, link, replay e A/B · S, 1,5 gg · dipende da: SOA-5.3, SOA-5.4, ENG-3.3 · PSP: sì · aspetta: ENG-3.3 (`wip/replay-oracle`)

## dopo gate

- [ ] **PRF-9.1** — Un solo test di flag per i servizi di ogni passata di update_gba · S, 1 gg · dipende da: PRF-2.2, PRF-3.1, F-120 · PSP: sì · aspetta: PRF-2.2 (`wip/perf-method`), PRF-3.1 (`wip/perf-method`) · _si fa dentro SOA-5.2_
