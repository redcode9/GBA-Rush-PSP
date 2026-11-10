# wip/ui-foundation — Interfaccia: primo avvio, toast, scorciatoie, input e screenshot

- **Branch:** `features/state-of-the-art-development/wip/ui-foundation`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** EXT-4, EXT-6, UI-1, UI-2, UI-3, UI-4, UI-5, UI-6 (23 storie, circa 24,75 gg).
- **File principali:** menu e input in `source/src/gui.c`, `input_model.c`, toast (nuovo).
- **Dipende da:** nessun altro branch per partire; UI-3.1, UI-3.2, UI-4.3, UI-6.2 ed EXT-6.1 aspettano ticket di `wip/link-diagnostics`, `wip/savestate-v2`, `wip/library`, `wip/suspend-resume` e `wip/docs` (vedi sotto).
- **Prove su PSP:** gli id delle sessioni sono in `docs/hardware-tests.md`; i risultati vanno in `docs/HARDWARE-RESULTS.md`.
- **Nota:** UI-1.1, UI-1.2 e UI-1.5 sono della 1.4.4.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **UI-1.1** — dir.ini facoltativo, senza dialogo · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (f1585e7), manca la prova su PSP (`boot-no-dir-ini`)_
- [ ] **UI-1.2** — I colori personalizzati sopravvivono al riavvio · S, 0,5 gg · dipende da: UI-1.1 · PSP: sì · _codice pronto (484bfb0), manca la prova su PSP (`boot-no-dir-ini`)_
- [ ] **UI-1.5** — Pacchetto da estrarre alla radice del Memory Stick · S, 0,25 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (2c953a6), manca la prova su PSP (`install-ms-root`)_

## 1.5

- [ ] **EXT-4.1** — Campo browser_view nel ConfigModel e vista ripristinata · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (7bf076e), manca la prova su PSP (`browser-carousel-500`)_
- [ ] **UI-1.3** — Libreria vuota spiegata e gba_bios.bin nascosto · S, 1 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (fae1ddc), manca la prova su PSP_
- [ ] **UI-1.4** — Suggerimento al primo avvio sui tasti del menu · S, 0,5 gg · dipende da: UI-2.2, UI-4.4, CMP-1.1 · PSP: sì · _codice pronto (3d0af02), manca la prova su PSP_
- [ ] **UI-2.1** — Modulo puro dei toast · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: manca la verifica della CI_
- [ ] **UI-2.2** — Disegno dei toast in gioco · M, 1,5 gg · dipende da: UI-2.1 · PSP: sì · _codice pronto (ff7fa37), manca la prova su PSP; per il confronto con un toast sempre visibile la build PROFILE=diagnostic ha Diagnostica > «Fissa un avviso di prova»_
- [ ] **UI-3.1** — Tasti stato senza dialogo, con toast · M, 2 gg · dipende da: LNK-3.2 · PSP: sì · aspetta: LNK-3.2 (`wip/link-diagnostics`)
- [ ] **UI-3.2** — Il toast del salvataggio rapido rimanda ad 'Annulla ultimo salvataggio' · S, 0,25 gg · dipende da: UI-3.1, SAV-4.5 · PSP: sì · aspetta: SAV-4.5 (`wip/savestate-v2`)
- [ ] **UI-4.1** — Modulo puro degli accordi · M, 2 gg · dipende da: — · PSP: no · **subito** · _in parte: manca la verifica della CI_
- [ ] **UI-4.2** — Accordi nel ciclo di input e nella config per gioco · M, 2 gg · dipende da: UI-4.1 · PSP: sì · _codice pronto (ff92689), manca la prova su PSP_
- [ ] **UI-4.4** — Menu senza HOME: una tenuta di riserva che non esce mai · S, 1 gg · dipende da: UI-4.2 · PSP: sì · _codice pronto (e07bd55), manca la prova su PSP_
- [ ] **UI-5.1** — Modulo puro input_model: turbo e isteresi · M, 1,5 gg · dipende da: — · PSP: no · **subito** · _in parte: manca la verifica della CI_
- [ ] **UI-5.2** — input.c passa a input_model · S, 0,75 gg · dipende da: UI-5.1 · PSP: sì · _codice pronto (95101d5), manca la prova su PSP_
- [ ] **UI-5.3** — Velocità e modalità del turbo per gioco · S, 1 gg · dipende da: UI-5.2, UI-2.2 · PSP: sì · _codice pronto (26ff97e), manca la prova su PSP_
- [ ] **UI-6.1** — Writer PNG nuovo e testato · M, 2 gg · dipende da: — · PSP: no · **subito** · _in parte: manca la verifica della CI_
- [ ] **UI-6.2** — Azione Screenshot · S, 1,25 gg · dipende da: UI-6.1, UI-2.2, UI-4.2, SAV-7.1 · PSP: sì · aspetta: SAV-7.1 (`wip/suspend-resume`)
- [ ] **UI-6.3** — Opzione screenshot: nativo o come mostrato · S, 1 gg · dipende da: UI-6.2 · PSP: sì

## 1.6

- [ ] **EXT-6.1** — ADR: nessuna tenuta chiude il gioco · S, 0,25 gg · dipende da: ENG-17.3 · PSP: no · aspetta: ENG-17.3 (`wip/docs`)
- [ ] **UI-4.3** — Pagina Scorciatoie con cattura e conflitti · M, 2 gg · dipende da: UI-4.2, UI-12.4 · PSP: sì · aspetta: UI-12.4 (`wip/library`)
- [ ] **UI-4.5** — Azione 'Ciclo video' con toast · S, 1 gg · dipende da: UI-4.2, UI-2.2 · PSP: sì · _codice pronto (23f2e96), manca la prova su PSP: si fa nella sessione di UI-4.3, perché prima nessuna pagina assegna l'accordo del Ciclo video (aspetta UI-12.4, `wip/library`)_

## dopo gate

- [ ] **EXT-6.2** — (condizionato) Azione "Torna alla lista" assegnabile, senza accordo di default · S, 1 gg · dipende da: EXT-6.1, UI-4.3 · PSP: sì
