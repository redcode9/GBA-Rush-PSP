# wip/link-techniques — Link: tecnica per gioco, relay, adattatore wireless

- **Branch:** `features/state-of-the-art-development/wip/link-techniques`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** LNK-8, LNK-9, LNK-10, LNK-11, LNK-13, LNK-14, LNK-15 (28 storie, circa 62,75 gg).
- **File principali:** `source/src/link_cable.c`, `rfu.c` (nuovo).
- **Dipende da:** `wip/link-diagnostics`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## dopo gate

- [ ] **LNK-8.1** — Funzione pura di scelta della tecnica · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-8.2** — Override nel profilo per gioco, deciso dall'host · M, 2 gg · dipende da: LNK-8.1, LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-8.3** — Prova hardware dell'override · S, 0,5 gg · dipende da: LNK-8.2 · PSP: sì
- [ ] **LNK-9.1** — Modulo puro di pacing · S, 1 gg · dipende da: LNK-1.3 · PSP: no · aspetta: LNK-1.3 (`wip/link-diagnostics`)
- [ ] **LNK-9.2** — Glue: vblank in più e audio che segue · M, 2 gg · dipende da: LNK-9.1, LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-9.3** — A/B su hardware · S, 1 gg · dipende da: LNK-9.2 · PSP: sì
- [ ] **LNK-10.1** — Finestra di raggruppamento in link_net_build · S, 1,5 gg · dipende da: LNK-1.3 · PSP: no · aspetta: LNK-1.3 (`wip/link-diagnostics`)
- [ ] **LNK-10.2** — Politica di raggruppamento nelle stanze Pokémon · S, 1,5 gg · dipende da: LNK-10.1 · PSP: no
- [ ] **LNK-10.3** — A/B su hardware nella stanza di FireRed · S, 0,75 gg · dipende da: LNK-10.2, PRF-1.1 · PSP: sì · aspetta: PRF-1.1 (`wip/perf-method`)
- [ ] **LNK-11.1** — AW2 nel banco rom_link prima del port · S, 2 gg · dipende da: LNK-1.3 · PSP: no · aspetta: LNK-1.3 (`wip/link-diagnostics`)
- [ ] **LNK-11.2** — Port del relay AW da libretro a commit fisso, con link_frag · L, 5 gg · dipende da: LNK-11.1, LNK-8.1, LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-11.3** — Test host del relay AW · M, 3 gg · dipende da: LNK-11.2 · PSP: no
- [ ] **LNK-11.4** — Battaglia AW completa su hardware · S, 1,5 gg · dipende da: LNK-11.3 · PSP: sì
- [ ] **LNK-13.1** — Stella di peer nel worker ad hoc · M, 3 gg · dipende da: LNK-1.1 · PSP: no · aspetta: LNK-1.1 (`wip/link-diagnostics`)
- [ ] **LNK-13.2** — Relay a N giocatori · L, 5,5 gg · dipende da: LNK-13.1, LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-13.3** — Test a 3-4 console · M, 3 gg · dipende da: LNK-13.2 · PSP: no
- [ ] **LNK-13.4** — Prova a 3-4 PSP · S, 1,5 gg · dipende da: LNK-13.3 · PSP: sì
- [ ] **LNK-14.1** — rfu.c da libretro a commit fisso · S, 1 gg · dipende da: LNK-1.1 · PSP: no · aspetta: LNK-1.1 (`wip/link-diagnostics`)
- [ ] **LNK-14.2** — Adapter come dispositivo gba_sio · L, 5 gg · dipende da: LNK-14.1, LNK-2.2, LNK-8.1 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-14.3** — Frame RFU frammentati su messaggi da 40 byte · M, 2 gg · dipende da: LNK-14.2 · PSP: no
- [ ] **LNK-14.4** — Stanza Adapter e ritmo · M, 2 gg · dipende da: LNK-5.1, LNK-14.3 · PSP: no · aspetta: LNK-5.1 (`wip/link-rooms`)
- [ ] **LNK-14.5** — Union Room a 3-4 PSP · L, 4 gg · dipende da: LNK-13.1, LNK-14.4 · PSP: no
- [ ] **LNK-14.6** — Prova su hardware · M, 2 gg · dipende da: LNK-14.5, LNK-14.7 · PSP: sì
- [ ] **LNK-14.7** — Ciclo di vita dell'Adapter e coda a buffer singolo · M, 2 gg · dipende da: LNK-14.2 · PSP: no
- [ ] **LNK-15.1** — Parser puro dei file carta · M, 2 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-15.2** — Distributore locale su rfu.c · L, 5 gg · dipende da: LNK-14.2, LNK-15.1 · PSP: no
- [ ] **LNK-15.3** — Menu 'Mystery Gift da file' · S, 1,5 gg · dipende da: LNK-15.2 · PSP: no
- [ ] **LNK-15.4** — Prova su hardware · S, 1 gg · dipende da: LNK-15.3, F-003 · PSP: sì
