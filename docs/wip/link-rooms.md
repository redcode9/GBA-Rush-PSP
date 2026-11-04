# wip/link-rooms — Link: stanze e invio di salvataggi

- **Branch:** `features/state-of-the-art-development/wip/link-rooms`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** LNK-5, LNK-7 (10 storie, circa 20,5 gg).
- **File principali:** sessione in `source/src/link_*.c`, menu Link in `gui.c`.
- **Dipende da:** `wip/link-diagnostics` (LNK-2.2), `wip/save-store`, `wip/savestate-v2`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 2.0

- [ ] **LNK-5.1** — Modulo puro per il nome della stanza · S, 1,5 gg · dipende da: LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-5.2** — Il worker crea e cerca per nome di stanza · M, 3 gg · dipende da: LNK-5.1, LNK-2.3 · PSP: sì · aspetta: LNK-2.3 (`wip/link-diagnostics`)
- [ ] **LNK-5.3** — Elenco delle stanze e numero di stanza nel menu · M, 2,5 gg · dipende da: LNK-5.2 · PSP: no
- [ ] **LNK-5.4** — Nickname del partner dalla lista peer · S, 1,5 gg · dipende da: LNK-5.2 · PSP: sì
- [ ] **LNK-5.5** — Prova con tre console · S, 1 gg · dipende da: LNK-5.3, LNK-5.4 · PSP: sì
- [ ] **LNK-7.1** — Modulo puro di trasferimento · M, 2,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-7.2** — Messaggi di trasferimento nel cavo · M, 2 gg · dipende da: LNK-7.1, LNK-2.2 · PSP: no · aspetta: LNK-2.2 (`wip/link-diagnostics`)
- [ ] **LNK-7.3** — Menu: invia e ricevi · M, 2,5 gg · dipende da: LNK-7.2, F-003, F-007, F-045 · PSP: sì
- [ ] **LNK-7.4** — Prova hardware con interruzioni · S, 1 gg · dipende da: LNK-7.3 · PSP: sì
- [ ] **LNK-7.5** — Opzionale: condivisione dal browser senza caricare il gioco · M, 3 gg · dipende da: LNK-7.4, LNK-5.3 · PSP: sì
