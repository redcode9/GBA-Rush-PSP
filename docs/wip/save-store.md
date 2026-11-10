# wip/save-store — Salvataggi che non si perdono

- **Branch:** `features/state-of-the-art-development/wip/save-store`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** EXT-1, SAV-1, SAV-2 (tranne SAV-2.6), SAV-3, SAV-5 (26 storie, circa 21 gg).
- **File principali:** backup in `source/src/memory.c`, `save_store.c` e `save_format.c` (nuovi), avvisi in `gui.c`.
- **Dipende da:** nessuna attesa su altri branch: SAV-2.6 è in main (b5cffbf), ENG-1.1 ed ENG-1.2 (`wip/release-and-ci`) sono in main 8ace097, unito qui da 89769bd.
- **Nota:** Primo lavoro della 1.4.4, che per la decisione (l) resta una tappa interna di lavoro: il rilascio è la 1.5.0. `features/save-read-fixes` (SAV-1 e SAV-2.8) è unito qui da 5dde920.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **EXT-1.1** — Rilevamento sui DMA da 17 o 81 unità, con test host del protocollo · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: codice e test pronti (601520e, e8f809c, bfcd1a4), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_eeprom sul vero memory.c, psp-build)_
- [ ] **EXT-1.2** — Caso "scrittura prima della lettura" nella ROM di test dei salvataggi · S, 0,5 gg · dipende da: SAV-2.6, EXT-1.1 · PSP: sì · _codice pronto (a58d9dc, 8f4729c) e prova `save-eeprom8k-wfirst` nel catalogo hardware (cc76d87, 4796757), mancano `make test-roms` con un compilatore ARM (build della ROM, core New e mGBA; la CI non lo esegue) e le prove su 1.4.2 e sul candidato, su due PSP_
- [x] **SAV-1.1** — Modulo puro save_format: tipo dal tag, taglia nativa, clamp · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [x] **SAV-1.2** — Interfaccia SaveStoreIo (exists, read, write) e file system finto con guasti iniettabili · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [x] **SAV-1.3** — save_store_load: assente, letto o illeggibile · S, 0,5 gg · dipende da: SAV-1.2 · PSP: no
- [ ] **SAV-1.4** — load_backup ricablato, binding sceIo condiviso e blocco delle scritture · M, 1,5 gg · dipende da: SAV-1.1, SAV-1.3 · PSP: no · _in parte: codice pronto (27c6538), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_memory_backup sul vero memory.c, psp-build per gui.c e main.c)_
- [ ] **SAV-1.5** — Avviso di salvataggio illeggibile: riprova o gioca senza salvare · S, 0,5 gg · dipende da: SAV-1.4 · PSP: sì · _codice pronto (27c6538) con una differenza dal ticket da confermare nella PR: all'avvio da launcher (ROM passata in argv) due no chiudono l'emulatore invece di aprire il menu (`main.c`, provato da `tests/test_startup.py`); il caso della directory `<rom>.sav` è nella prova `save-read-matrix` (ee6873e), mancano la prova su PSP e la CI verde dopo il push_
- [ ] **SAV-1.6** — .sav.orig conservato dallo store ed evento SAVE_LOAD nel runtime log · S, 0,5 gg · dipende da: SAV-1.4, SAV-2.1 · PSP: no · _in parte: codice pronto (35df986, 834f812, 1481187, 9e3ca7c), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_memory_backup, che prova che il vero memory.c registra SAVE_LOAD)_
- [ ] **SAV-1.7** — Argine immediato per gli stati che indicizzano fuori dal buffer · S, 0,5 gg · dipende da: SAV-1.1 · PSP: no · _in parte: codice e test puri pronti (27c6538, bcc9296), manca la compilazione di memory.c e ASan (`make check`, `make test-sanitize` in CI)_
- [ ] **SAV-1.8** — Validazione hardware del percorso di lettura · S, 1 gg · dipende da: SAV-1.4, SAV-1.5, SAV-1.6, SAV-1.7 · PSP: sì · _in parte: docs/saves.md (650222a, f9d4407) e la prova `save-read-matrix` nel catalogo hardware (ee6873e, 41bcb7e), mancano la sessione hardware e le note della 1.5.0 generate da `make release-gate` (ENG-1.2), che leggono `docs/releases/1.5.0.txt`: l'id `save-read-matrix` oggi è solo in `docs/releases/1.4.4.txt` (file di wip/release-and-ci)_
- [x] **SAV-2.1** — Scrittura in streaming e recupero nello store puro · M, 2 gg · dipende da: SAV-1.2, SAV-1.3 · PSP: no
- [ ] **SAV-2.2** — Binding sceIo condiviso, lato scrittura · S, 0,5 gg · dipende da: SAV-1.4 · PSP: no · _in parte: codice pronto (de9cf19, 1656c1f) e test host del binding in `tests/test_save_io.py` (c2469b3, e604ae7, 9428196), manca la compilazione di gui.c (psp-build in CI) e l'esecuzione di test_save_io.py in `make check` con gcc_
- [ ] **SAV-2.3** — save_backup tramite lo store, con nuovi tentativi e avvisi · M, 1,5 gg · dipende da: SAV-2.1, SAV-2.2 · PSP: no · _in parte: codice pronto (02d90bc, 0d684aa, ee14bc5, 9278190), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_memory_backup sul vero memory.c, psp-build per gui.c e main.c)_
- [ ] **SAV-2.4** — Misura di ogni flush nel runtime log · S, 0,5 gg · dipende da: SAV-2.3 · PSP: sì · _codice pronto (d32696a, 12c64cb), mancano un id nel catalogo hardware per le tre sessioni su PSP-1000 (Flash 128 KiB, SRAM, scambio in link) con p50, p95 e massimo della durata, le sessioni stesse e la CI verde dopo il push_
- [ ] **SAV-2.5** — Stati .svs in streaming tramite lo store, senza il buffer da 512 KiB · M, 1 gg · dipende da: SAV-2.1, SAV-2.2 · PSP: sì · _codice pronto (da dc4fecc a 843f80c), mancano un id nel catalogo hardware (spegnimento forzato durante il salvataggio di uno stato, poi si carica il precedente; uno stato della nuova build con `gba_bios.bin` si carica in 1.4.2 e viceversa), la prova su PSP e la CI verde dopo il push_
- [ ] **SAV-2.7** — Validazione hardware delle scritture e rilascio 1.4.4 · M, 1,5 gg · dipende da: SAV-2.3, SAV-2.4, SAV-2.5, SAV-2.6, SAV-2.8, SAV-2.9, SAV-1.8 · PSP: sì · _in parte: docs/saves.md (650222a, f9d4407) e le prove `save-powerloss-100x`, `save-card-full` e `save-trade-fr-emerald` nel catalogo hardware (ab6cfd5, 7fd60a2), mancano la sessione hardware, gli id delle prove in `docs/releases/1.5.0.txt` (file di wip/release-and-ci: oggi sono solo in `1.4.4.txt`, e `make release-gate` legge il file della sua versione) e le note della 1.5.0 generate da `make release-gate` (ENG-1.2) dopo le sessioni_
- [ ] **SAV-2.8** — Carica stato da file non sovrascrive lo stato AUTO scelto · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (e3defe4) e prova `savestate-load-auto-file` nel catalogo hardware (79ce851), mancano la prova su PSP e `make check` verde in CI_
- [ ] **SAV-2.9** — Una sospensione durante la domanda di ripresa non perde lo stato AUTO · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (28b41f2) e prova `sleep-resume-question` nel catalogo hardware (79ce851), mancano la prova su PSP e `make check` verde in CI_

## 1.5

- [x] **SAV-3.1** — save_store_ring_push: rotazione verificata delle generazioni · S, 0,5 gg · dipende da: SAV-2.1 · PSP: no
- [ ] **SAV-3.2** — Flush e copia in main_link_start · S, 0,5 gg · dipende da: SAV-3.1, SAV-2.3 · PSP: no · _in parte: codice pronto (e4b43d2, b6095dc), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_memory_backup sul vero memory.c, psp-build per gui.c e main.c); SAV-2.3 è ancora in parte_
- [ ] **SAV-3.3** — Ripristina salvataggio pre-link nel menu Link · M, 1 gg · dipende da: SAV-3.2 · PSP: no · _in parte: codice pronto (463bfca, 99e11fc, e4f9f0e), manca la CI verde dopo il push (`make check` e `make test-sanitize` per test_memory_backup sul vero memory.c; gui.c lo compila solo psp-build, non `make check`)_
- [ ] **SAV-3.4** — Validazione con uno scambio reale · S, 0,5 gg · dipende da: SAV-3.3 · PSP: sì · _in parte: docs/saves.md (650222a, f9d4407) e la durata della copia pre-link nel runtime log (e992f93, d40651b), mancano la voce del CHANGELOG sulla durata della copia pre-link nel runtime log, un id nel catalogo hardware, la sessione hardware e la CI verde dopo il push_

## dopo gate

- [ ] **SAV-5.1** — Modulo puro save_writer: consegna a generazioni · S, 1 gg · dipende da: — · PSP: no · _parte solo se la baseline di SAV-2.4 fa scattare il gate_
- [ ] **SAV-5.2** — Thread writer con snapshot o scrittura a blocchi · M, 1 gg · dipende da: SAV-5.1, SAV-2.3 · PSP: sì
- [ ] **SAV-5.3** — Svuotamento sincronizzato nei punti critici · S, 0,5 gg · dipende da: SAV-5.2 · PSP: sì
- [ ] **SAV-5.4** — Misura A/B del guadagno · S, 1 gg · dipende da: SAV-5.3 · PSP: sì
