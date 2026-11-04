# wip/mirror — Mirror: due GBA su ogni PSP

- **Branch:** `features/state-of-the-art-development/wip/mirror`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** LNK-12, SOA-7 (12 storie, circa 50 gg).
- **File principali:** `source/src/link_cable.c`, sessione in `link_net.c`, istanza B in `main.c` e `memory.c`.
- **Dipende da:** `wip/new-core-speed`, `wip/media-engine`, `wip/link-rooms` (LNK-7.1), `wip/link-techniques` (LNK-8.1), `wip/replay-oracle`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 2.0

- [ ] **SOA-7.1** — Budget a due core con New: macchina B senza rendering né audio · S, 1,5 gg · dipende da: SOA-4.1, LNK-12.1 · PSP: sì · aspetta: SOA-4.1 (`wip/new-core-speed`)
- [ ] **SOA-7.3** — Macchina B senza PPU né mixer: solo lo stato che il gioco osserva · M, 2,5 gg · dipende da: LNK-12.3 · PSP: sì

## dopo gate

- [ ] **LNK-12.1** — Misura del budget a due core (gate 2) · S, 2 gg · dipende da: F-025, F-027, LNK-1.3 · PSP: sì · aspetta: LNK-1.3 (`wip/link-diagnostics`)
- [ ] **LNK-12.2** — Spike di determinismo tra modelli (gate 3) · M, 3 gg · dipende da: F-120, F-122 · PSP: sì
- [ ] **LNK-12.3** — Istanza B del core · XL, 14 gg · dipende da: LNK-12.8 · PSP: sì
- [ ] **LNK-12.4** — Scambio degli input e ritardo calcolato dall'RTT · M, 4 gg · dipende da: LNK-12.1, LNK-12.2 · PSP: no
- [ ] **LNK-12.5** — Avvio della sessione a due core · M, 4 gg · dipende da: LNK-12.3, LNK-12.4, LNK-7.1, LNK-8.1, LNK-2.2, F-007 · PSP: no · aspetta: LNK-7.1 (`wip/link-rooms`), LNK-8.1 (`wip/link-techniques`), LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-12.6** — Impronta di stato e fine pulita · M, 3 gg · dipende da: LNK-12.5, F-120 · PSP: no
- [ ] **LNK-12.7** — Accettazione su hardware · S, 2 gg · dipende da: LNK-12.6 · PSP: sì
- [ ] **LNK-12.8** — Cache e stato del core dietro puntatori, senza seconda istanza · M, 4 gg · dipende da: LNK-12.1, LNK-12.2 · PSP: sì · _parte CPU in gran parte fatta dal core New_
- [ ] **SOA-7.2** — (condizionata) Spike: la CPU della macchina B sul Media Engine · L, 5 gg · dipende da: SOA-6.1, SOA-7.1 · PSP: sì · aspetta: SOA-6.1 (`wip/media-engine`)
- [ ] **SOA-7.4** — (condizionata) Rollback di 1-2 frame con snapshot in memoria · L, 5 gg · dipende da: SOA-7.3, GIO-1.2 · PSP: sì · aspetta: GIO-1.2 (`wip/rewind`)
