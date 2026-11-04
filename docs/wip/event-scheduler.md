# wip/event-scheduler — Scheduler a scadenza unica e timer dal vivo

- **Branch:** `features/state-of-the-art-development/wip/event-scheduler`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-12.2, PRF-9.1, SOA-5 (7 storie, circa 14,5 gg).
- **File principali:** `update_gba` in `source/src/main.c`, timer e DMA in `memory.c`, `event_queue.c` (nuovo).
- **Dipende da:** `wip/test-roms` (CMP-12.1), `wip/replay-oracle` (ENG-3.3).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **CMP-12.2** — Timer letti dal vivo, avviati e fermati al ciclo giusto · M, 3 gg · dipende da: CMP-12.1, ENG-3.3, PRF-1.5 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`), ENG-3.3 (`wip/replay-oracle`), PRF-1.5 (`wip/perf-method`) · _sostituita da SOA-5.3: si chiude con quella_
- [x] **SOA-5.1** — Modulo puro event_queue con test host · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **SOA-5.2** — update_gba guidato dalla coda: righe, timer, DMA e SIO come eventi · L, 4 gg · dipende da: SOA-5.1, CMP-12.1 · PSP: sì · aspetta: CMP-12.1 (`wip/test-roms`)
- [ ] **SOA-5.3** — Timer letti dal vivo, avviati e fermati al ciclo (sostituisce CMP-12.2) · M, 2 gg · dipende da: SOA-5.2 · PSP: sì
- [ ] **SOA-5.4** — Eventi di riga saltati quando nessuno li osserva · M, 2 gg · dipende da: SOA-5.2 · PSP: sì
- [ ] **SOA-5.5** — Verifica su due modelli: core-rom, link, replay e A/B · S, 1,5 gg · dipende da: SOA-5.3, SOA-5.4, ENG-3.3 · PSP: sì · aspetta: ENG-3.3 (`wip/replay-oracle`)

## Note per SOA-5.2-5.4 (studio del 2026-11-04)

- **Eventi e ordine di oggi.** Una passata di `update_gba` serve, in quest'ordine e ognuno al più una volta: `main_power_service`, avanzamento di `cpu_ticks`, completamento SIO (`gba_sio_advance`), flag `gbc_sound_update`, timer 0-3 (solo quelli a prescaler sono eventi; la cascata scatta dentro l'overflow del timer prima, nella stessa passata), una sola transizione di riga (inizio HBlank: riga del renderer, DMA HBlank, IRQ HBlank, `video_present_poll` in VBlank; inizio riga: VCOUNT, VBlank a 160 con input/menu, presentazione, DMA VBlank; fine frame a 228 con `synchronize`), servizio del link, stallo DMA (`dma_cycle_count`, ripete la passata senza CPU), IF e risveglio/IRQ (ritardo di 9 cicli solo per il Classic).
- **Id della coda = ordine di oggi:** SIO 0, timer 1-4, riga 5, link 6, stallo DMA 7, ritardo IRQ 8. Prima versione: raccogliere tutti gli eventi in scadenza in una maschera e servirli per id, uno per passata, come oggi; così il Classic resta identico al ciclo e il replay si confronta. L'ordine per scadenza differisce solo nel superamento del budget del core New.
- **Scadenza unica.** I due core ricevono già un solo numero (il valore di ritorno di `update_gba`); basta calcolarlo come testa della coda meno `cpu_ticks`. Non servono modifiche a `cpu.c` né agli stub: l'avvio di timer, DMA e SIO continua a far uscire il core con gli alert di oggi. Il Classic scarta il superamento e atterra sulla scadenza, il New lo conta: la coda tratta gli eventi in ritardo come già dovuti.
- **Stato salvato.** Il savestate tiene `video_count`, `timer[].count`, `irq_ticks`: la coda si ricostruisce da questi dopo caricamento, reset e ogni `continue` che sostituisce la macchina, e si riconverte prima di scrivere. Formato invariato.
- **SIO e link.** Evento SIO a `cpu_ticks + cycles_remaining`, riarmato a fine passata; `gba_sio_advance` deve restare su ogni passata finché il trasferimento è attivo, perché `gba_sio_set_cycle` rifiuta un ciclo diverso da `current_cycle`. Evento link al minimo tra `link_cable_cycles_until_service`, il tick di 16384 cicli (oggi agganciato alle passate, non esatto) e subito se `pending_start`.
- **Rischi per il link.** Cambiare l'ordine a parità di ciclo sposta i cicli scritti in START e negli annunci; togliere passate (SOA-5.4) senza rendere esatto il tick di 16384 cicli ritarda la ricezione dei datagrammi fino a un frame; la staffetta Pokémon dipende da `pokemon.next_transfer` assoluto e dalla cadenza del timer 3 del gioco.
- **Core Classic congelato (decisione (m)).** Il Classic legge l'I/O direttamente in `mips_stub.S` (`execute_load_io_*`, open bus compreso): timer, VCOUNT e DISPSTAT calcolati alla lettura valgono solo per il core New (lettura C in `memory.c`, ciclo corrente = `cpu_ticks` + cicli dell'IRQ + `jit->core.cycles`). Con il Classic `update_gba` deve continuare ad aggiornare i registri e a servire ogni riga. SOA-5.3 e SOA-5.4 vanno quindi accettate sul solo core New.
- **Misure su PSP da prendere prima di SOA-5.4:** passate per frame (contatore delle iterazioni di `perf_monitor`) su MKSC con New, tempo per passata, frame con IRQ HBlank o DMA HBlank attivi, servizi del link al secondo e attese lockstep.

## dopo gate

- [ ] **PRF-9.1** — Un solo test di flag per i servizi di ogni passata di update_gba · S, 1 gg · dipende da: PRF-2.2, PRF-3.1, F-120 · PSP: sì · aspetta: PRF-2.2 (`wip/perf-method`), PRF-3.1 (`wip/perf-method`) · _si fa dentro SOA-5.2_
