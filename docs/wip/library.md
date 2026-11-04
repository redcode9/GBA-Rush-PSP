# wip/library — Libreria e arte dei giochi

- **Branch:** `features/state-of-the-art-development/wip/library`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** EXT-7, EXT-8, UI-12, UI-13, UI-15, UI-17 (20 storie, circa 32,5 gg).
- **File principali:** browser in `source/src/gui.c`, `boxart.c`, strumenti desktop in `tools/`.
- **Dipende da:** `wip/ui-foundation`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **UI-12.1** — Ultimo gioco preselezionato · S, 0,5 gg · dipende da: — · PSP: sì · **subito**

## 1.6

- [ ] **EXT-7.1** — ADR: niente pacchetti di arte derivata, composizione locale · S, 0,25 gg · dipende da: ENG-17.3 · PSP: no · aspetta: ENG-17.3 (`wip/docs`)
- [ ] **EXT-8.1** — ADR: niente USB per il giocatore, si esce all'XMB · S, 0,25 gg · dipende da: ENG-17.3 · PSP: no · aspetta: ENG-17.3 (`wip/docs`)
- [ ] **UI-12.2** — Nomi puliti · S, 1 gg · dipende da: UI-9.6 · PSP: no · aspetta: UI-9.6 (`wip/ui-ge`)
- [ ] **UI-12.3** — Badge del salvataggio, dimensione e numero di giochi · M, 1,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-12.4** — Preferiti e menu di opzioni per ROM su TRIANGOLO · M, 3,5 gg · dipende da: — · PSP: sì · **subito**

## 2.0

- [ ] **UI-12.5** — Vista unificata facoltativa · M, 3 gg · dipende da: F-027, EXT-4.1 · PSP: sì · aspetta: EXT-4.1 (`wip/ui-foundation`)

## dopo gate

- [ ] **EXT-7.2** — (condizionato) Repository di sfondi originali con licenza e provenienza · M, 1,5 gg · dipende da: EXT-7.1, UI-13.1 · PSP: sì
- [ ] **EXT-8.2** — (condizionato) Spike: USB e rimontaggio senza riavvio, solo build diagnostica · M, 1,5 gg · dipende da: EXT-8.1 · PSP: sì
- [ ] **EXT-8.3** — (condizionato) Voce di menu "Connessione USB" · M, 2 gg · dipende da: EXT-8.2 · PSP: sì
- [ ] **UI-13.1** — Sfondo per gioco a 480x272 con cache e versione sfocata · M, 3 gg · dipende da: F-027 · PSP: sì
- [ ] **UI-13.2** — Layout artistico del browser · M, 3 gg · dipende da: UI-13.1, UI-8.2, EXT-4.1 · PSP: sì · aspetta: UI-8.2 (`wip/ui-ge`), EXT-4.1 (`wip/ui-foundation`)
- [ ] **UI-13.3** — Barre laterali sfocate, spente di default · M, 2 gg · dipende da: UI-13.1, UI-8.2 · PSP: sì · aspetta: UI-8.2 (`wip/ui-ge`)
- [ ] **UI-13.4** — Arte dietro il caricamento (ex UI-14.3) · S, 0,5 gg · dipende da: UI-14.1, UI-13.1 · PSP: sì · aspetta: UI-14.1 (`wip/rom-loading`)
- [ ] **UI-15.1** — Verifica dei termini e decisione · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **UI-15.2** — Modalità 'Recupera box art' · M, 2 gg · dipende da: UI-15.1 · PSP: no
- [ ] **UI-15.3** — Rinomina di pacchetti di arte e composizione degli sfondi · M, 2 gg · dipende da: UI-15.1 · PSP: no
- [ ] **UI-17.1** — Spike: PMF generato dal nostro script · M, 3 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-17.2** — ICON1.PMF nel pacchetto · S, 1 gg · dipende da: UI-17.1 · PSP: sì
- [ ] **UI-17.3** — Splash all'avvio senza attese · S, 0,5 gg · dipende da: — · PSP: sì · **subito**
