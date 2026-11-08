# wip/save-store — Salvataggi che non si perdono

- **Branch:** `features/state-of-the-art-development/wip/save-store`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** EXT-1, SAV-1, SAV-2 (tranne SAV-2.6), SAV-3, SAV-5 (26 storie, circa 21 gg).
- **File principali:** backup in `source/src/memory.c`, `save_store.c` e `save_format.c` (nuovi), avvisi in `gui.c`.
- **Dipende da:** `wip/test-roms` (SAV-2.6).
- **Nota:** Primo lavoro della 1.4.4. `features/save-read-fixes` (SAV-1 e SAV-2.8) è unito qui da eefb684.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **EXT-1.1** — Rilevamento sui DMA da 17 o 81 unità, con test host del protocollo · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: codice e test pronti (e182282), manca l'esecuzione di test_eeprom sul vero memory.c (header PSPSDK, `make check`)_
- [ ] **EXT-1.2** — Caso "scrittura prima della lettura" nella ROM di test dei salvataggi · S, 0,5 gg · dipende da: SAV-2.6, EXT-1.1 · PSP: sì · aspetta: SAV-2.6 (`wip/test-roms`)
- [x] **SAV-1.1** — Modulo puro save_format: tipo dal tag, taglia nativa, clamp · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [x] **SAV-1.2** — Interfaccia SaveStoreIo (exists, read, write) e file system finto con guasti iniettabili · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [x] **SAV-1.3** — save_store_load: assente, letto o illeggibile · S, 0,5 gg · dipende da: SAV-1.2 · PSP: no
- [ ] **SAV-1.4** — load_backup ricablato, binding sceIo condiviso e blocco delle scritture · M, 1,5 gg · dipende da: SAV-1.1, SAV-1.3 · PSP: no · _in parte: codice pronto (27bb9f7), manca l'esecuzione di test_memory_backup sul vero memory.c (header PSPSDK, `make check` e ASan)_
- [ ] **SAV-1.5** — Avviso di salvataggio illeggibile: riprova o gioca senza salvare · S, 0,5 gg · dipende da: SAV-1.4 · PSP: sì · _codice pronto (27bb9f7), manca la prova su PSP_
- [ ] **SAV-1.6** — .sav.orig conservato dallo store ed evento SAVE_LOAD nel runtime log · S, 0,5 gg · dipende da: SAV-1.4, SAV-2.1 · PSP: no · _in parte: codice pronto (47205b4), manca la prova che il vero memory.c registri SAVE_LOAD (test_memory_backup, header PSPSDK)_
- [x] **SAV-1.7** — Argine immediato per gli stati che indicizzano fuori dal buffer · S, 0,5 gg · dipende da: SAV-1.1 · PSP: no
- [ ] **SAV-1.8** — Validazione hardware del percorso di lettura · S, 1 gg · dipende da: SAV-1.4, SAV-1.5, SAV-1.6, SAV-1.7 · PSP: sì
- [x] **SAV-2.1** — Scrittura in streaming e recupero nello store puro · M, 2 gg · dipende da: SAV-1.2, SAV-1.3 · PSP: no
- [x] **SAV-2.2** — Binding sceIo condiviso, lato scrittura · S, 0,5 gg · dipende da: SAV-1.4 · PSP: no
- [ ] **SAV-2.3** — save_backup tramite lo store, con nuovi tentativi e avvisi · M, 1,5 gg · dipende da: SAV-2.1, SAV-2.2 · PSP: no · _in parte: codice pronto (3b64ac1), manca l'esecuzione di test_memory_backup sul vero memory.c (header PSPSDK, `make check`)_
- [ ] **SAV-2.4** — Misura di ogni flush nel runtime log · S, 0,5 gg · dipende da: SAV-2.3 · PSP: sì · _codice pronto (e50ef0b), manca la prova su PSP_
- [ ] **SAV-2.5** — Stati .svs in streaming tramite lo store, senza il buffer da 512 KiB · M, 1 gg · dipende da: SAV-2.1, SAV-2.2 · PSP: sì · _codice pronto (bd4e259), manca la prova su PSP_
- [ ] **SAV-2.7** — Validazione hardware delle scritture e rilascio 1.4.4 · M, 1,5 gg · dipende da: SAV-2.3, SAV-2.4, SAV-2.5, SAV-2.6, SAV-2.8, SAV-2.9, SAV-1.8 · PSP: sì · aspetta: SAV-2.6 (`wip/test-roms`)
- [ ] **SAV-2.8** — Carica stato da file non sovrascrive lo stato AUTO scelto · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (1ef2e55), manca la prova su PSP_
- [ ] **SAV-2.9** — Una sospensione durante la domanda di ripresa non perde lo stato AUTO · S, 0,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (63b814b), manca la prova su PSP_

## 1.5

- [x] **SAV-3.1** — save_store_ring_push: rotazione verificata delle generazioni · S, 0,5 gg · dipende da: SAV-2.1 · PSP: no
- [x] **SAV-3.2** — Flush e copia in main_link_start · S, 0,5 gg · dipende da: SAV-3.1, SAV-2.3 · PSP: no
- [x] **SAV-3.3** — Ripristina salvataggio pre-link nel menu Link · M, 1 gg · dipende da: SAV-3.2 · PSP: no
- [ ] **SAV-3.4** — Validazione con uno scambio reale · S, 0,5 gg · dipende da: SAV-3.3 · PSP: sì

## dopo gate

- [ ] **SAV-5.1** — Modulo puro save_writer: consegna a generazioni · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **SAV-5.2** — Thread writer con snapshot o scrittura a blocchi · M, 1 gg · dipende da: SAV-5.1, SAV-2.3 · PSP: sì
- [ ] **SAV-5.3** — Svuotamento sincronizzato nei punti critici · S, 0,5 gg · dipende da: SAV-5.2 · PSP: sì
- [ ] **SAV-5.4** — Misura A/B del guadagno · S, 1 gg · dipende da: SAV-5.3 · PSP: sì
