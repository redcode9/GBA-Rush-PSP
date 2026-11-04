# wip/media-engine — Renderer Exact sul Media Engine e presentazione

- **Branch:** `features/state-of-the-art-development/wip/media-engine`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** PRF-11, PRF-14, SOA-6 (19 storie, circa 43,75 gg).
- **File principali:** `source/prx/media_engine/`, `source/src/video_me.c`, `gba_ppu*.c`, `psp_video.c`.
- **Dipende da:** `wip/hardware-validation`, `wip/suspend-resume` (SAV-7.5).
- **Nota:** PRF-14 è superata da SOA-6: le sue storie si chiudono con quelle di SOA-6 che ne prendono gli obiettivi.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **PRF-11.1** — Misura del gate: cucitura e frame in ritardo · S, 1 gg · dipende da: PRF-2.1, PRF-1.5 · PSP: sì · aspetta: PRF-2.1 (`wip/perf-method`), PRF-1.5 (`wip/perf-method`)
- [ ] **PRF-11.5** — Latenza dell'input: telemetria nel perf report · M, 1,75 gg · dipende da: PRF-2.1, ENG-1.6 · PSP: sì · aspetta: PRF-2.1 (`wip/perf-method`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **SOA-6.1** — Attesa del ME con halt e interrupt invece del polling · M, 2,5 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-6.2** — Esperimento: flag di sincronizzazione nella memoria locale del ME o nella SRAM condivisa · S, 1,5 gg · dipende da: SOA-6.1 · PSP: sì
- [ ] **SOA-6.3** — Copie delle pagine sporche senza CPU e writeback per intervallo · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-6.4** — Presentazione: strisce ≤ 128 px dalla VRAM e scan-out diretto a scala 1x · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-6.5** — Codice caldo del ME entro 16 KiB e dati nella memoria locale del ME · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-6.6** — Sospensione, ripresa e soak di 8 ore e 100 sleep/wake · M, 2,5 gg · dipende da: SOA-6.1, SAV-7.5 · PSP: sì · aspetta: SAV-7.5 (`wip/suspend-resume`)
- [ ] **SOA-6.7** — Latenza misurata e frame di latenza scritto nell'aiuto · S, 1 gg · dipende da: PRF-11.5, SOA-6.4 · PSP: sì

## dopo gate

- [ ] **PRF-11.2** — Modulo puro frame_ring · S, 1 gg · dipende da: PRF-11.1 · PSP: no
- [ ] **PRF-11.3** — Triplo buffer in psp_video.c · M, 2,5 gg · dipende da: PRF-11.2, PRF-11.7 · PSP: sì
- [ ] **PRF-11.4** — Verifica su hardware: cucitura, latenza, fast-forward · S, 1,5 gg · dipende da: PRF-11.3, PRF-11.5 · PSP: sì
- [ ] **PRF-11.6** — Campionamento del pad più fitto del frame (con gate) · S, 1 gg · dipende da: PRF-11.5 · PSP: sì
- [ ] **PRF-11.7** — Flip inviato prima del vblank (con gate) · S, 1 gg · dipende da: PRF-11.5 · PSP: sì
- [ ] **PRF-11.8** — Ritmo e flip alla riga 160 (con gate) · L, 4 gg · dipende da: PRF-11.5, PRF-11.7, F-120, ENG-15.4 · PSP: sì · aspetta: ENG-15.4 (`wip/crash-safety`)
- [ ] **PRF-14.1** — Prototipo misurato del renderer sul ME · XL, 8 gg · dipende da: PRF-13.4, PRF-2.2 · PSP: sì · aspetta: PRF-13.4 (`wip/audio`), PRF-2.2 (`wip/perf-method`) · _superata: implementazione già nel main (SOA-6)_
- [ ] **PRF-14.2** — Presentazione nello stesso refresh e ritorno alla CPU per frame · L, 5 gg · dipende da: PRF-14.1, F-126 · PSP: sì · _superata da SOA-6.4_
- [ ] **PRF-14.3** — Opzione del renderer ME · S, 1 gg · dipende da: PRF-14.2 · PSP: no · _superata: opzione Exact già nel main_
- [ ] **PRF-14.4** — Soak, latenza e divergenze su hardware · M, 2,5 gg · dipende da: PRF-14.3, PRF-11.5 · PSP: sì · _superata da SOA-6.6 e SOA-6.7_
