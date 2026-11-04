# wip/test-roms — ROM di prova: salvataggi, core-rom e raster

- **Branch:** `features/state-of-the-art-development/wip/test-roms`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-3.1, CMP-4.1, CMP-5.1, CMP-11.1, CMP-12.1, ENG-10.3, EXT-3.1, SAV-2.6 (8 storie, circa 13,25 gg).
- **File principali:** `tests/core-rom/`, `tests/save-rom/`, `tests/raster-rom/` (nuove).
- **Dipende da:** nessun altro branch.
- **Nota:** Sblocca CMP, EXT-3, SAV-2.7 e SOA-3: si parte subito.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **SAV-2.6** — ROM di test dei salvataggi, con include e protocollo d'esito comuni alle ROM di test (devkitARM, solo sorgente) · M, 2,5 gg · dipende da: — · PSP: sì · **subito**

## 1.5

- [ ] **CMP-3.1** — ROM di prova del core (tests/core-rom) con esito su schermo e in SRAM · M, 1,25 gg · dipende da: SAV-2.6 · PSP: sì
- [ ] **CMP-5.1** — Caso core-rom ewram_stack e prova del difetto su 1.4.2 · S, 1 gg · dipende da: CMP-3.1 · PSP: sì
- [ ] **CMP-11.1** — Casi CPU della core-rom: shift per registro, cambi di modo, BX, serie condizionali e IRQ sbloccati · M, 2,25 gg · dipende da: CMP-3.1, ENG-10.4 · PSP: sì · aspetta: ENG-10.4 (`wip/dev-environment`)
- [ ] **CMP-12.1** — Casi di tempi e bus nella core-rom e nella ROM dei salvataggi · M, 3,25 gg · dipende da: CMP-3.1, SAV-2.6, ENG-10.4 · PSP: sì · aspetta: ENG-10.4 (`wip/dev-environment`)
- [ ] **EXT-3.1** — Generatore dei vettori e casi di decompressione e copia nella core-rom · M, 1 gg · dipende da: CMP-3.1, CMP-1.1 · PSP: no · aspetta: CMP-1.1 (`wip/open-bios`)

## 1.6

- [ ] **ENG-10.3** — ROM di test GPL per gli effetti raster · M, 1,5 gg · dipende da: SAV-2.6 · PSP: no

## dopo gate

- [ ] **CMP-4.1** — Caso core-rom pc_high e decisione documentata · S, 0,5 gg · dipende da: CMP-3.1, F-149 · PSP: sì
