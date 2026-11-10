# wip/rom-loading — Caricamento delle ROM: zip, ROM grandi e piccole, avanzamento

- **Branch:** `features/state-of-the-art-development/wip/rom-loading`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-6, EXT-2, EXT-5, UI-14 (11 storie, circa 10 gg).
- **File principali:** `source/src/zip.c`, `load_gamepak` e paginazione in `memory.c`.
- **Dipende da:** `wip/save-store` (EXT-1.1), `wip/classic-core` (CMP-12.5).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **CMP-6.1** — Gate: Classic NES Series su 1.4.2 · S, 0,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **EXT-2.1** — Modulo puro zip_rom: inflate a blocchi con esiti distinti e CRC32 · M, 1,5 gg · dipende da: — · PSP: no · **subito** · _in parte: codice e test host pronti (37756d8, f9643c6, f140760), passano normali e con UBSan; manca la prova sotto ASan (`make test-sanitize`)_
- [ ] **EXT-2.2** — zip.c sopra zip_rom e messaggio per lo zip danneggiato · S, 0,5 gg · dipende da: EXT-2.1 · PSP: sì · _codice pronto (c60f182), manca la prova su PSP; per la sessione lo zip in streaming si scrive con `zipfile.ZipFile(stream non seekable, 'w', compression=zipfile.ZIP_DEFLATED)` e il suo primo header locale deve avere `flags & 8` e metodo 8 (uno zip in streaming con la ROM non compressa, metodo 0, è rifiutato di proposito)_
- [ ] **EXT-5.1** — Finestra EEPROM decisa in read16_eeprom, con test host su una ROM paginata da 32 MiB · S, 1 gg · dipende da: EXT-1.1 · PSP: no · aspetta: EXT-1.1 (`wip/save-store`)
- [ ] **EXT-5.2** — Variante da 32 MiB della ROM di test dei salvataggi · S, 1 gg · dipende da: SAV-2.6, EXT-5.1 · PSP: sì · aspetta: EXT-1.1 (`wip/save-store`), tramite EXT-5.1

## 1.6

- [ ] **UI-14.1** — Barra di avanzamento reale · S, 1 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (c60f182, e9b56d4, a022834), manca la prova su PSP_
- [ ] **UI-14.2** — Tempi delle fasi nel runtime log · S, 1 gg · dipende da: ENG-1.6 · PSP: no · _in parte: codice e test host pronti (2640464, ff027c6), passano normali e con UBSan; mancano `make check` (test_perf_dma_probe), `make test-sanitize` e le build PSP in CI_

## dopo gate

- [ ] **CMP-6.2** — Mirroring senza copia tramite MAP_REGION · S, 1 gg · dipende da: CMP-6.1, CMP-3.1 · PSP: sì
- [ ] **CMP-6.3** — (condizionato) Letture oltre la fine della ROM · S, 0,5 gg · dipende da: CMP-6.2, CMP-3.1 · PSP: sì
- [ ] **CMP-6.4** — (condizionato) Classic NES sul dynarec con la finestra di prefetch di CMP-12.5 · S, 1 gg · dipende da: CMP-6.2, CMP-12.5 · PSP: sì · aspetta: CMP-12.5 (`wip/classic-core`)
- [ ] **EXT-5.3** — (condizionato) LDRH del dynarec da 0x0D sotto 0x0DFFFF00 legge la ROM · S, 1 gg · dipende da: EXT-5.2 · PSP: sì
