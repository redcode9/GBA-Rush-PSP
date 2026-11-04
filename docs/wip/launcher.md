# wip/launcher — Launcher single-game e link automatico

- **Branch:** `features/state-of-the-art-development/wip/launcher`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** GIO-7, LNK-16 (9 storie, circa 13,5 gg).
- **File principali:** launcher e bolle.
- **Dipende da:** `wip/save-store`, `wip/link-rooms` (LNK-5.3), `wip/link-diagnostics` (LNK-4.2).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 2.0

- [ ] **GIO-7.1** — Modulo puro launch_args · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **GIO-7.2** — Cartelle per bolla nell'emulatore e copia iniziale del salvataggio · M, 2 gg · dipende da: GIO-7.1, F-003 · PSP: sì
- [ ] **GIO-7.3** — Stub e builder: file di opzioni, percorsi relativi, ef0: · M, 2,5 gg · dipende da: GIO-7.2 · PSP: sì
- [ ] **GIO-7.4** — Bolla autonoma (facoltativa) · S, 1,5 gg · dipende da: GIO-7.3, F-139, F-140 · PSP: sì

## dopo gate

- [ ] **LNK-16.1** — Misura dello scatto all'avvio della rete · S, 1 gg · dipende da: LNK-5.3 · PSP: sì · aspetta: LNK-5.3 (`wip/link-rooms`)
- [ ] **LNK-16.2** — Impostazione 'Link automatico' per gioco e per bolla · S, 1,5 gg · dipende da: LNK-16.1, GIO-7.3 · PSP: no
- [ ] **LNK-16.3** — Innesco e scelta del ruolo · M, 2,5 gg · dipende da: LNK-16.2, LNK-4.2 · PSP: no · aspetta: LNK-4.2 (`wip/link-diagnostics`)
- [ ] **LNK-16.4** — Prova su hardware · S, 1 gg · dipende da: LNK-16.3, LNK-16.5 · PSP: sì
- [ ] **LNK-16.5** — Collegamento automatico impostato dalla bolla (ex GIO-7.5) · S, 0,5 gg · dipende da: LNK-16.3, GIO-7.3 · PSP: no
