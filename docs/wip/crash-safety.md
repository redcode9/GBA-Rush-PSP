# wip/crash-safety — Guasti senza perdite e sicurezza sul campo

- **Branch:** `features/state-of-the-art-development/wip/crash-safety`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-4.2, ENG-15, SAV-6 (12 storie, circa 13,5 gg).
- **File principali:** salto non mappabile in `source/src/cpu.c`, `exception.c`, `stall_guard.c` (nuovo).
- **Dipende da:** `wip/test-roms`, `wip/save-store` (SAV-2.3), `wip/release-and-ci` (ENG-1.6).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **ENG-15.1** — Modulo puro stall_guard · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-15.2** — Thread watchdog con record critici e flush STALL · M, 2 gg · dipende da: ENG-15.1, ENG-1.6 · PSP: sì · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **ENG-15.3** — Record 'sessione precedente non chiusa' all'avvio · M, 1,5 gg · dipende da: ENG-15.2 · PSP: sì
- [ ] **ENG-15.4** — Regola degli interruttori di emergenza · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-15.5** — Chiavi avanzate testuali (solo su richiesta) · M, 2 gg · dipende da: ENG-15.4, ENG-18.2 · PSP: sì · aspetta: ENG-18.2 (`wip/release-and-ci`)
- [ ] **SAV-6.1** — Contenimento del salto non mappabile in cpu.c · M, 2 gg · dipende da: CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **SAV-6.2** — Menu del guasto: Riavvia, Carica stato, Esci · S, 1 gg · dipende da: SAV-6.1 · PSP: no
- [ ] **SAV-6.3** — Batteria in salvo senza fidarsi del gioco impazzito · S, 0,5 gg · dipende da: SAV-6.1, SAV-2.3 · PSP: sì · aspetta: SAV-2.3 (`wip/save-store`)
- [ ] **SAV-6.4** — Record del guasto nel runtime log · S, 0,5 gg · dipende da: SAV-6.1 · PSP: no
- [ ] **SAV-6.5** — Schermata di crash nativa: batteria in salvo su richiesta · S, 0,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **SAV-6.6** — Validazione con la ROM di test e rilascio · S, 1 gg · dipende da: SAV-6.2, SAV-6.3, SAV-6.4, SAV-6.5, SAV-2.6 · PSP: sì · aspetta: SAV-2.6 (`wip/test-roms`)

## dopo gate

- [ ] **CMP-4.2** — Maschera solo nel ramo del bad jump, con record nel runtime log · S, 0,5 gg · dipende da: CMP-4.1, F-008, ENG-1.6 · PSP: sì · aspetta: CMP-4.1 (`wip/test-roms`), ENG-1.6 (`wip/release-and-ci`)
