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
- [x] **UI-2.1** — Modulo puro dei toast · S, 1 gg · dipende da: — · PSP: no · **subito** · _CI verde su 4a3d359 (run 42798826357): `test_toast` passa in make check e in make test-sanitize (ASan e UBSan), e `check-performance.sh` passa con `toast.c` senza allocazioni né I/O_
- [ ] **UI-2.2** — Disegno dei toast in gioco · M, 1,5 gg · dipende da: UI-2.1 · PSP: sì · _codice pronto (ff7fa37; con Exact sul Media Engine un frame in ritardo si ripresenta prima di toast, FPS e volume, che quindi restano: 822344d), manca la prova su PSP; per il confronto con un toast sempre visibile la build PROFILE=diagnostic ha Diagnostica > «Fissa un avviso di prova»_
- [ ] **UI-3.1** — Tasti stato senza dialogo, con toast · M, 2 gg · dipende da: LNK-3.2 · PSP: sì · aspetta: LNK-3.2 (`wip/link-diagnostics`)
- [ ] **UI-3.2** — Il toast del salvataggio rapido rimanda ad 'Annulla ultimo salvataggio' · S, 0,25 gg · dipende da: UI-3.1, SAV-4.5 · PSP: sì · aspetta: SAV-4.5 (`wip/savestate-v2`)
- [x] **UI-4.1** — Modulo puro degli accordi · M, 2 gg · dipende da: — · PSP: no · **subito** · _CI verde su 4a3d359 (run 42798826357): `test_input_chord` passa in make check e in make test-sanitize (ASan e UBSan)_
- [ ] **UI-4.2** — Accordi nel ciclo di input e nella config per gioco · M, 2 gg · dipende da: UI-4.1 · PSP: sì · _codice pronto (ff92689), manca la prova su PSP. Dopo la revisione: le scorciatoie globali restano separate da quelle dei giochi (3732ef9): un gioco senza .cfg (e il menu senza gioco) parte da quelle di tempgba.cfg, e all'uscita tempgba.cfg riceve quelle globali, mai quelle del gioco in corso; la pagina di UI-4.3 dovrà cambiare quelle globali solo senza gioco caricato, come fa la pagina Turbo. Il tasto che risponde al «Salvare/Caricare lo stato?» aperto da un accordo arriva al gioco solo dopo essere stato rilasciato (a013198), e un caricamento rifiutato o annullato non ripete più la riga 159 né ferma il turbo (9bccfd2). «Le associazioni personalizzate restano per gioco» si prova su PSP solo quando la pagina di UI-4.3 permette di cambiare un accordo: accordo diverso in un gioco A, poi un gioco B senza .cfg (deve avere le scorciatoie globali), poi uscita, riavvio e controllo di A e di B_
- [ ] **UI-4.4** — Menu senza HOME: una tenuta di riserva che non esce mai · S, 1 gg · dipende da: UI-4.2 · PSP: sì · _codice pronto (e07bd55), manca la prova su PSP_
- [x] **UI-5.1** — Modulo puro input_model: turbo e isteresi · M, 1,5 gg · dipende da: — · PSP: no · **subito** · _CI verde su 4a3d359 (run 42798826357): `test_input_model` passa in make check e in make test-sanitize (ASan e UBSan)_
- [ ] **UI-5.2** — input.c passa a input_model · S, 0,75 gg · dipende da: UI-5.1 · PSP: sì · _codice pronto (95101d5), manca la prova su PSP_
- [ ] **UI-5.3** — Velocità e modalità del turbo per gioco · S, 1 gg · dipende da: UI-5.2, UI-2.2 · PSP: sì · _codice pronto (26ff97e), manca la prova su PSP. Dopo la revisione (3732ef9): il turbo globale resta separato da quello dei giochi. Un gioco senza .cfg (e il menu senza gioco) parte dal turbo di tempgba.cfg; la pagina Turbo cambia quello globale solo senza gioco caricato, altrimenti solo quello del gioco; all'uscita tempgba.cfg riceve il turbo globale, mai quello del gioco in corso. Nella prova su PSP: Interruttore su A in un gioco A, poi un gioco B senza .cfg (deve partire dal turbo globale), poi uscita, riavvio e controllo di A, di B e della pagina Turbo prima di caricare un gioco_
- [x] **UI-6.1** — Writer PNG nuovo e testato · M, 2 gg · dipende da: — · PSP: no · **subito** · _CI verde su 4a3d359 (run 42798826357): `test_png_write` passa in make check e in make test-sanitize (ASan e UBSan); le build PSP release e diagnostic compilano `png_write.c` e non più `png.c`. Dopo la revisione: il test prova anche il margine IDAT da 24 byte, l'ultimo che `png_idat_open` rifiuta (dbdbe6c), e i due messaggi rimasti di `png.c` sono tolti dalle 5 tabelle (48d1183)_
- [ ] **UI-6.2** — Azione Screenshot · S, 1,25 gg · dipende da: UI-6.1, UI-2.2, UI-4.2, SAV-7.1 · PSP: sì · aspetta: SAV-7.1 (`wip/suspend-resume`)
- [ ] **UI-6.3** — Opzione screenshot: nativo o come mostrato · S, 1 gg · dipende da: UI-6.2 · PSP: sì

## 1.6

- [ ] **EXT-6.1** — ADR: nessuna tenuta chiude il gioco · S, 0,25 gg · dipende da: ENG-17.3 · PSP: no · aspetta: ENG-17.3 (`wip/docs`)
- [ ] **UI-4.3** — Pagina Scorciatoie con cattura e conflitti · M, 2 gg · dipende da: UI-4.2, UI-12.4 · PSP: sì · aspetta: UI-12.4 (`wip/library`)
- [ ] **UI-4.5** — Azione 'Ciclo video' con toast · S, 1 gg · dipende da: UI-4.2, UI-2.2 · PSP: sì · _codice pronto (23f2e96; con UI-7.1 in main il ciclo passa anche da 2x intero, tra 150% GU e 16:9 Sharp: 5d5c96f; l'accordo ferma un benchmark in corso come quelli di salvataggio e caricamento: 7e93a03), manca la prova su PSP: si fa nella sessione di UI-4.3, perché prima nessuna pagina assegna l'accordo del Ciclo video (aspetta UI-12.4, `wip/library`)_

## dopo gate

- [ ] **EXT-6.2** — (condizionato) Azione "Torna alla lista" assegnabile, senza accordo di default · S, 1 gg · dipende da: EXT-6.1, UI-4.3 · PSP: sì
