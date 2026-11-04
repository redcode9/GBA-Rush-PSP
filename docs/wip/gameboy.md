# wip/gameboy — Modulo Game Boy e link Game Boy

- **Branch:** `features/state-of-the-art-development/wip/gameboy`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** GIO-8, LNK-17 (19 storie, circa 51,5 gg).
- **File principali:** modulo GB (nuovo).
- **Dipende da:** `wip/save-store`, `wip/savestate-v2`.
- **Nota:** Sconsigliato, dietro gate.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## dopo gate

- [ ] **GIO-8.1** — Spike di gate: domanda, PRX e velocità · S, 2 gg · dipende da: F-027, F-024 · PSP: sì
- [ ] **GIO-8.2** — Import intatto di TGB Dual da MasterBoy e build host · M, 4 gg · dipende da: GIO-8.1 · PSP: no
- [ ] **GIO-8.3** — Modulo puro gb_cart: header, mapper, dimensione del salvataggio · S, 1,5 gg · dipende da: GIO-8.2 · PSP: no
- [ ] **GIO-8.4** — Core come PRX caricato solo per .gb/.gbc · L, 5 gg · dipende da: GIO-8.2, F-027 · PSP: sì
- [ ] **GIO-8.5** — Video, audio, input e menu per il GB · L, 5,25 gg · dipende da: GIO-8.4 · PSP: sì
- [ ] **GIO-8.6** — Browser e launcher per .gb/.gbc · M, 2 gg · dipende da: GIO-8.5 · PSP: sì
- [ ] **GIO-8.7** — Stati di salvataggio GB validati · M, 3 gg · dipende da: GIO-8.5, F-007 · PSP: sì
- [ ] **GIO-8.8** — Salvataggi di batteria GB a RAM ferma · M, 2,5 gg · dipende da: GIO-8.5, F-003, F-006 · PSP: sì
- [ ] **GIO-8.9** — RTC MBC3 con trailer VBA-M/BGB · M, 2 gg · dipende da: GIO-8.8, CMP-7.3 · PSP: sì · aspetta: CMP-7.3 (`wip/game-compat`)
- [ ] **GIO-8.10** — Colori: palette DMG, palette SGB, correzione LCD del GBC · M, 3 gg · dipende da: GIO-8.5 · PSP: sì
- [ ] **GIO-8.11** — Trucchi GB: GameShark e Game Genie · M, 2 gg · dipende da: GIO-8.5, GIO-4.3 · PSP: sì · aspetta: GIO-4.3 (`wip/cheats`)
- [ ] **GIO-8.12** — Suite di accuratezza in make check-gb · M, 3,25 gg · dipende da: GIO-8.2 · PSP: no
- [ ] **GIO-8.13** — Tilt MBC7 con la levetta · S, 1 gg · dipende da: GIO-8.5 · PSP: sì
- [ ] **GIO-8.14** — Documenti, crediti e sorgente · S, 1 gg · dipende da: GIO-8.4, F-140 · PSP: no
- [ ] **LNK-17.1** — Seriale GB agganciata al livello sessione · M, 3 gg · dipende da: F-091 · PSP: no
- [ ] **LNK-17.2** — Doppio Game Boy in lockstep di input · L, 6 gg · dipende da: LNK-17.1 · PSP: no
- [ ] **LNK-17.3** — Identità della cartuccia e motivi · S, 1 gg · dipende da: LNK-17.2 · PSP: no
- [ ] **LNK-17.4** — Opzionale: prestito della cartuccia in RAM · M, 3 gg · dipende da: LNK-17.3, LNK-7.1 · PSP: no · aspetta: LNK-7.1 (`wip/link-rooms`)
- [ ] **LNK-17.5** — Prova su hardware · S, 1 gg · dipende da: LNK-17.3 · PSP: sì
