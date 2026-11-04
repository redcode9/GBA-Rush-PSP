# wip/ui-ge — Interfaccia 2.0 sul GE, testo e menu di gioco

- **Branch:** `features/state-of-the-art-development/wip/ui-ge`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** UI-7, UI-8, UI-9, UI-10, UI-11, UI-16 (25 storie, circa 47,75 gg).
- **File principali:** disegno in `source/src/gui.c`, filtri in `psp_video.c`, font.
- **Dipende da:** `wip/ui-foundation` (UI-2, UI-4), `wip/platform` (PRF-4).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **UI-9.1** — Accenti italiani tramite GBK e stringhe fuori tabella · S, 1 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-9.4** — Sonda della codifica dei nomi file · S, 1 gg · dipende da: ENG-1.6 · PSP: sì · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **UI-9.6** — ROM con nomi non ASCII: trovate, aperte e mostrate, con salvataggi dal nome stabile · M, 3 gg · dipende da: UI-9.4, UI-9.1 · PSP: sì
- [ ] **UI-10.5** — Reset del gioco sicuro e annullabile · S, 0,75 gg · dipende da: SAV-2.3 · PSP: sì · aspetta: SAV-2.3 (`wip/save-store`)

## 1.6

- [ ] **UI-7.1** — Preset '2x intero (ritaglio)' · S, 1 gg · dipende da: — · PSP: sì · **subito**

## 2.0

- [ ] **UI-7.2** — Sharp bilinear come terzo filtro a 150% e USER · M, 2,5 gg · dipende da: UI-7.1, F-127 o, se lo spike PPSSPP fallisce, ENG-6.6 · PSP: sì · aspetta: ENG-6.6 (`wip/replay-oracle`)
- [ ] **UI-7.3** — Filtro LCD a griglia, a punti e a scanline · M, 2,5 gg · dipende da: UI-7.2, UI-10.1, F-127 o, se lo spike PPSSPP fallisce, ENG-6.6 · PSP: sì · aspetta: ENG-6.6 (`wip/replay-oracle`)
- [ ] **UI-8.1** — Modulo puro ui_batch e rasterizzatore di riferimento · M, 3 gg · dipende da: — · PSP: no · **subito**
- [ ] **UI-8.2** — Backend PSP del batch per rettangoli, immagini e pulizia · M, 2,5 gg · dipende da: UI-8.1, UI-8.4, F-127 o, se lo spike PPSSPP fallisce, ENG-6.6 · PSP: sì · aspetta: ENG-6.6 (`wip/replay-oracle`)
- [ ] **UI-8.3** — Interpolazioni a passi fissi e 'Riduci movimento' · S, 1 gg · dipende da: UI-8.2 · PSP: no
- [ ] **UI-8.4** — Golden per lingua e tema in make check · M, 2 gg · dipende da: UI-8.1 · PSP: no
- [ ] **UI-8.5** — Testo come sprite dalla cache dei glifi · M, 3 gg · dipende da: UI-8.2 · PSP: sì
- [ ] **UI-9.2** — Atlante anti-aliasing latino generato al build · M, 3 gg · dipende da: UI-8.5, F-027 · PSP: sì
- [ ] **UI-9.3** — CJK anti-aliasing con ripiego bitmap · M, 3 gg · dipende da: UI-9.2 · PSP: sì
- [ ] **UI-10.1** — Sfondo del menu: frame in pausa a schermo intero con velatura · M, 2 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-10.2** — Anteprima live delle opzioni video · S, 1 gg · dipende da: UI-10.1, UI-12.4 · PSP: sì · aspetta: UI-12.4 (`wip/library`)
- [ ] **UI-10.3** — Miniature degli slot · M, 2 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-10.4** — Animazione di apertura e banda di selezione · S, 1 gg · dipende da: UI-8.3 · PSP: sì
- [ ] **UI-11.1** — Stato live sulle righe principali · S, 1 gg · dipende da: — · PSP: sì · **subito**
- [ ] **UI-11.2** — Azione Pausa con scheda sul frame congelato · S, 0,5 gg · dipende da: UI-4.2, UI-10.1 · PSP: sì · aspetta: UI-4.2 (`wip/ui-foundation`)
- [ ] **UI-11.3** — Scheda al risveglio · M, 1,5 gg · dipende da: UI-11.2, SAV-7.5, SAV-7.6, ENG-8.1 · PSP: sì · aspetta: SAV-7.5 (`wip/suspend-resume`), SAV-7.6 (`wip/suspend-resume`), ENG-8.1 (`wip/suspend-resume`)
- [ ] **UI-16.1** — Misura dell'heap attorno al cambio gioco · M, 1,5 gg · dipende da: F-027, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **UI-16.2** — Soak di 200 cambi per modello su hardware · M, 3 gg · dipende da: UI-16.1, ENG-6.2 · PSP: sì · aspetta: ENG-6.2 (`wip/replay-oracle`)

## dopo gate

- [ ] **UI-9.5** — UTF-8 a runtime (solo se UI-9.4 o LNK-5.4 lo richiedono) · L, 4 gg · dipende da: UI-9.4, UI-9.3 · PSP: sì
- [ ] **UI-16.3** — Ripiego se la deriva resta (condizionato) · S, 1 gg · dipende da: UI-16.2, UI-2.2 · PSP: sì · aspetta: UI-2.2 (`wip/ui-foundation`)
