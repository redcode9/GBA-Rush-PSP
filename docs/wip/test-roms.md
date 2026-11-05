# wip/test-roms — ROM di prova: salvataggi, core-rom e raster

- **Branch:** `features/state-of-the-art-development/wip/test-roms`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-3.1, CMP-4.1, CMP-5.1, CMP-11.1, CMP-12.1, ENG-10.3, EXT-3.1, SAV-2.6 (8 storie, circa 13,25 gg).
- **File principali:** `tests/core-rom/`, `tests/save-rom/`, `tests/raster-rom/` (nuove).
- **Dipende da:** nessun altro branch.
- **Nota:** Sblocca CMP, EXT-3, SAV-2.7 e SOA-3: si parte subito.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **SAV-2.6** — ROM di test dei salvataggi, con include e protocollo d'esito comuni alle ROM di test (devkitARM, solo sorgente) · M, 2,5 gg · dipende da: — · PSP: sì · **subito** · _implementato 2026-11-04, manca: i sette target in mGBA (SOA-1.2) e su PSP-1000 con il `.sav` letto da `tools/decode_test_rom.py`; build con devkitARM non provata (qui arm-none-eabi-gcc 15)_

## 1.5

- [ ] **CMP-3.1** — ROM di prova del core (tests/core-rom) con esito su schermo e in SRAM · M, 1,25 gg · dipende da: SAV-2.6 · PSP: sì · _implementato 2026-11-04, manca: PPSSPP e PSP-1000, e `halt_ime0` FAIL sulla 1.4.2; al posto di `scripts/check-core-rom.sh` c'è `make test-roms`_
- [ ] **CMP-5.1** — Caso core-rom ewram_stack e prova del difetto su 1.4.2 · S, 1 gg · dipende da: CMP-3.1 · PSP: sì · _implementato 2026-11-04, manca: FAIL atteso sulla 1.4.2 in PPSSPP e su PSP-1000, sessione con Kirby e Mario Golf, righe in `docs/HARDWARE-RESULTS.md` e nota nel README se il difetto si conferma_
- [ ] **CMP-11.1** — Casi CPU della core-rom: shift per registro, cambi di modo, BX, serie condizionali e IRQ sbloccati · M, 2,25 gg · dipende da: CMP-3.1, ENG-10.4 · PSP: sì · aspetta: ENG-10.4 (`wip/dev-environment`) · _implementato 2026-11-04, manca: tutti PASS in mGBA, PPSSPP, PSP-1000 e un secondo modello con la 1.4.3 con «Stack optimize» acceso e spento, esiti in `docs/HARDWARE-RESULTS.md` con lo sha256 di `EBOOT.PBP`, e gli esiti di jsmolka `arm.gba` e `thumb.gba` (ENG-10.4); valori attesi solo da GBATEK; il caso di controllo della latenza è `cpu_irq_latency`, che non conta i cicli: controlla solo che un IRQ alzato con I=0 entri una volta sola prima che girino 16 istruzioni dopo lo store che lo alza. I 9 cicli del ticket sono anche il conteggio di GBA Rush (`irq_ticks = 9` in `source/src/main.c`, ricavato dal datasheet ARM7TDMI); un caso che li misura con un timer manca ancora e il valore atteso va confermato su un GBA vero o in mGBA. `cpu_irq_latency` è l'unico degli 11 casi che fallisce su rom_run (il core New prende l'IRQ a fine blocco)_
- [ ] **CMP-12.1** — Casi di tempi e bus nella core-rom e nella ROM dei salvataggi · M, 3,25 gg · dipende da: CMP-3.1, SAV-2.6, ENG-10.4 · PSP: sì · aspetta: ENG-10.4 (`wip/dev-environment`) · _implementato 2026-11-04, manca: i 104 casi (43 timer, 15 prefetch, 30 branch, 4 vram, 12 bus) registrati in mGBA, oggi i valori vengono solo da GBATEK; PPSSPP, PSP-1000 e PSP-2000 con la 1.4.3 e i FAIL in `docs/HARDWARE-RESULTS.md`; punteggi per sezione delle suite timer e timing di mGBA e di jsmolka `memory.gba` (ENG-10.4). Su rom_run falliscono per limiti noti di rom_run e del core New `timer_start_delay`, `timer_stop_hold`, 11 prefetch, i 4 vram e 6 letture del bus dei salvataggi (elenco in `scripts/check-test-roms.sh`)_
- [ ] **EXT-3.1** — Generatore dei vettori e casi di decompressione e copia nella core-rom · M, 1 gg · dipende da: CMP-3.1, CMP-1.1 · PSP: no · aspetta: CMP-1.1 (`wip/open-bios`) · _implementato 2026-11-04, manca: la corsa in PPSSPP con `gba_bios.bin`. Verificato: generatore deterministico (`make test` lo rigenera e confronta), `bios_vectors.h` di 51 KiB, i 18 casi passano su rom_run sia con il BIOS ufficiale sia con quello integrato_

## 1.6

- [ ] **ENG-10.3** — ROM di test GPL per gli effetti raster · M, 1,5 gg · dipende da: SAV-2.6 · PSP: no · _implementato 2026-11-04, manca: due corse in mGBA con lo stesso frame e l'esito nel protocollo, e il confronto con `make check-render` (`tests/render/raster.txt`); build con devkitARM non provata. rom_run non ha PPU né DMA e IRQ in HBlank, quindi lì i 3 casi falliscono e si controlla solo che la ROM giri. La ROM è in `tests/raster-rom/`, accanto alle altre, invece di `tests/test-roms/raster/`_

## dopo gate

- [ ] **CMP-4.1** — Caso core-rom pc_high e decisione documentata · S, 0,5 gg · dipende da: CMP-3.1, F-149 · PSP: sì · _implementato 2026-11-04, manca: esito sulla 1.4.2 in PPSSPP e su PSP-1000 (atteso bad jump) e, se possibile, su un GBA vero; la decisione è in `docs/DESIGN-DECISIONS.md`_

## Note di lavoro

- `make test-roms` compila le ROM in `build/test-roms` con arm-none-eabi-gcc (o `DEVKITARM`) e le esegue su `tests/core/rom_run.c`, l'interprete del core New con una GBA minima: dice che la ROM gira e scrive la firma, non che un valore atteso è giusto. Con `BIOS=` (dump personale) i casi open bus del BIOS devono passare tutti; con il BIOS integrato `bios_openbus_irq` e `bios_openbus_swi` falliscono, perché esce da indirizzi diversi.
- Un caso nuovo: una voce nella tabella di `cases_<area>.c` (codice a mano in `cases_<area>_asm.s`); un'area nuova va nel `Makefile` della ROM e nella lista di `source/main.c`, prima di `cases_pc`, che resta ultima. I nomi dei casi viaggiano nella firma: il decoder non va toccato.
- I casi che falliscono su rom_run per limiti noti (non errori delle ROM) sono in `core_allow` e `bus_allow` di `scripts/check-test-roms.sh`, ognuno con il motivo; quando il core New li corregge si tolgono da lì.
- `cases_stm` (11 casi, i test di CMP-12.4) è già nella core-rom; la correzione di CMP-12.4 tocca il core Classic, bloccato dalla decisione (m): resta da fare sul core New. La metà di test di CMP-12.4 sta in questo branch perché i casi controllano i due core, mentre la correzione Classic di CMP-12.4 resta congelata in `wip/classic-core`. Su rom_run fallisce solo `stm_vram`.
- `tests/core-rom/gen_shift_table.py` scrive in fase di build i valori attesi di `cpu_shift_reg`; `tools/make_bios_vectors.py` scrive `bios_vectors.h`, che invece è committato e va rigenerato quando lo script cambia.
- Il logo dell'header resta vuoto: le ROM partono solo con l'avvio diretto ("Boot from BIOS" spento; in mGBA senza BIOS o con "Skip BIOS intro").
