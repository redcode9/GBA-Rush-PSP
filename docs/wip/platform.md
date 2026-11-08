# wip/platform — Hardware PSP al limite e memoria per modello

- **Branch:** `features/state-of-the-art-development/wip/platform`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-14.4, PRF-4, PRF-5, PRF-6, SOA-8 (19 storie, circa 31,5 gg).
- **File principali:** buffer ROM in `source/src/memory.c`, VRAM in `psp_video.c`, `source/Makefile` (MEMSIZE).
- **Dipende da:** `wip/dev-environment`, `wip/release-and-ci` (ENG-1.6).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **PRF-4.1** — Misura della memoria nel runtime log (v6) e nel report (v7) · M, 2 gg · dipende da: ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)

## 1.6

- [ ] **PRF-4.2** — docs/RAM-BUDGET.md misurato su hardware · S, 1,5 gg · dipende da: PRF-4.1 · PSP: sì
- [ ] **PRF-4.3** — Soglia di memoria prima del link, con motivo localizzato · S, 1 gg · dipende da: PRF-4.2, PRF-4.4, LNK-3.2, F-099 · PSP: sì · aspetta: LNK-3.2 (`wip/link-diagnostics`)
- [ ] **PRF-4.4** — Memoria trattenuta e traccia delle allocazioni (solo build diagnostica) · S, 1 gg · dipende da: PRF-4.1, F-137 · PSP: no
- [ ] **PRF-5.1** — Modulo puro mem_budget · S, 1 gg · dipende da: PRF-4.1 · PSP: no
- [ ] **PRF-5.2** — Buffer ROM a passi di 1 MiB con riserva misurata · M, 2 gg · dipende da: PRF-5.1, PRF-4.2, PRF-1.1, CMP-1.4 · PSP: sì · aspetta: PRF-1.1 (`wip/perf-method`), CMP-1.4 (`wip/open-bios`)
- [ ] **SOA-8.1** — Sonda su console: scratchpad, D-cache, RAM e geometria delle cache · S, 1,5 gg · dipende da: SOA-1.1 · PSP: sì · aspetta: SOA-1.1 (`wip/dev-environment`)
- [ ] **SOA-8.2** — Copie di blocco con lv.q/sv.q per savestate, snapshot e rewind · S, 1,5 gg · dipende da: SOA-8.1 · PSP: sì
- [ ] **SOA-8.3** — Pagine ROM della PSP-1000 lette con sceDmacMemcpy da un thread ausiliario · M, 2 gg · dipende da: SOA-8.1, PRF-5.2 · PSP: sì
- [ ] **SOA-8.4** — Modelli 2000+: ROM residente, partizione volatile e VRAM da 4 MiB · M, 2,5 gg · dipende da: PRF-6.1 · PSP: sì
- [ ] **SOA-8.5** — Ritmo sul VBlank e ricampionamento 59,73→59,94 Hz senza scatti · M, 2 gg · dipende da: SOA-8.1 · PSP: sì
- [ ] **SOA-8.6** — Clock 333/166 MHz verificati in ogni percorso · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (58a35de), manca la prova su PSP. Ancora dovuti check-core e check-render (tocca `video_me.c`): non eseguiti qui, mancano Unicorn e mGBA. File caldi di altri branch da scrivere nella PR: `video_me.c` e `video_me.h` (`wip/media-engine`), `main_power_service` in `main.c` (`wip/suspend-resume`), `menu()` in `gui.c` (`wip/ui-foundation`) e le azioni del menu Link in `gui.c` (`wip/link-rooms`), `load_gamepak` in `memory.c` (`wip/rom-loading`), `scripts/check-lifecycle.sh` (`wip/dev-environment`), `tools/decode_runtime_log.py` (`wip/perf-method`). I record del clock sono i LINK_COUNTERS 16 e 17: 13-15 sono di `wip/media-engine`; nel merge con quel branch il clock del Media Engine va costruito sul suo `convert_busy_time` (ticks_per_ms), che dopo un cambio di clock deve aspettare a sua volta un clock nuovo del Media Engine prima di aprire un tratto._

## dopo gate

- [ ] **ENG-14.4** — Sonda della geometria delle cache su PSP · S, 1 gg · dipende da: ENG-2.2 · PSP: sì · aspetta: ENG-2.2 (`wip/release-and-ci`)
- [ ] **PRF-5.3** — Cache JIT allocate all'avvio e dimensionate per modello (con gate) · M, 2,5 gg · dipende da: PRF-5.1, PRF-1.5, F-120 · PSP: sì · aspetta: PRF-1.5 (`wip/perf-method`)
- [ ] **PRF-5.4** — Sostituzione delle pagine ROM second chance con soft-fault (con gate) · M, 2,5 gg · dipende da: PRF-5.2 · PSP: sì
- [ ] **PRF-6.1** — Spike: partizione volatile su ogni modello · S, 1,5 gg · dipende da: PRF-4.1, F-137 · PSP: sì
- [ ] **PRF-6.2** — Pagine ROM nella partizione volatile, invalidate attorno allo sleep · M, 3 gg · dipende da: PRF-6.1, PRF-6.4, SAV-7.5 · PSP: sì · aspetta: SAV-7.5 (`wip/suspend-resume`)
- [ ] **PRF-6.3** — Soak: 100 sospensioni con pagine nella partizione · S, 1,5 gg · dipende da: PRF-6.2, F-120 · PSP: sì
- [ ] **PRF-6.4** — Allocatore vm_pool per i due inquilini (pagine ROM e rewind) · S, 1 gg · dipende da: PRF-6.1 · PSP: no
