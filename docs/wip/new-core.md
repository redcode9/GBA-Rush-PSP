# wip/new-core — Core New: parità con il Classic, poi predefinito

- **Branch:** `features/state-of-the-art-development/wip/new-core`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-3, SOA-10 (10 storie, circa 15 gg).
- **File principali:** `source/src/arm_gba.c`, `arm_decode.c`, `arm_interp.c`, `arm_core.h`, `arm_ir.h`.
- **Dipende da:** `wip/hardware-validation`, `wip/open-bios` (CMP-1.2, CMP-3.2, CMP-3.3), `wip/test-roms`, `wip/game-compat` (CMP-9.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **SOA-3.1** — Work RAM allineata per l'accesso diretto del core New · S, 1 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: misura su MKSC con `lockstep --count` (serve mGBA) e compare-benchmarks 3+3 con New su PSP-1000; vedi le note_
- [ ] **SOA-3.2** — Idle loop di game_config.txt con il core New · M, 1,5 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: replay di 2 minuti identico con e senza salto (oracolo di replay o lockstep con mGBA) e guadagno su PSP su due titoli con idle loop verificato; vedi le note_
- [ ] **SOA-3.3** — BIOS integrato con il core New: HLE di Div/DivArm e open bus a run time · M, 2 gg · dipende da: CMP-1.2, CMP-3.3 · PSP: sì · aspetta: CMP-1.2 (`wip/open-bios`), CMP-3.3 (`wip/open-bios`)
- [ ] **SOA-3.4** — HALT e IRQ sbloccati con il core New · S, 1 gg · dipende da: CMP-3.2, CMP-11.1 · PSP: sì · aspetta: CMP-3.2 (`wip/open-bios`), CMP-11.1 (`wip/test-roms`)

## 1.6

- [ ] **SOA-3.5** — Core-rom completa verde con New su due modelli · M, 1,5 gg · dipende da: CMP-11.1, CMP-12.1, SOA-3.3, SOA-3.4 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), CMP-12.1 (`wip/test-roms`)
- [ ] **SOA-3.6** — Savestate tra core e tra BIOS: matrice delle quattro combinazioni · S, 1 gg · dipende da: SOA-3.3 · PSP: sì · _implementato 2026-11-04 (test host in `tests/test_open_bios.py`), manca: prova su PSP con FireRed, da ripetere dopo SOA-3.3_
- [ ] **SOA-3.7** — Passata di compatibilità con New sui 40 titoli di CMP-9.4 · M, 2,5 gg · dipende da: SOA-3.5, CMP-9.1 · PSP: sì · aspetta: CMP-9.1 (`wip/game-compat`)

## 2.0

- [ ] **SOA-10.1** — Confronto finale Classic/New e decisione scritta · S, 1,5 gg · dipende da: SOA-3.7, SOA-4.2 · PSP: sì · aspetta: SOA-4.2 (`wip/new-core-speed`)
- [ ] **SOA-10.2** — New predefinito, con migrazione delle impostazioni e Classic come ripiego per gioco · S, 1 gg · dipende da: SOA-10.1 · PSP: sì
- [ ] **SOA-10.3** — Rimozione del Classic e chiusura delle storie solo Classic · M, 2 gg · dipende da: SOA-10.2 · PSP: no

## Note

**2026-11-04, SOA-3.1.** L'allineamento statico della work RAM non arriva sulla PSP: `psp-prxgen` scrive un solo program header con `p_align` 0x10, quindi il loader colloca l'EBOOT dove vuole e `aligned(0x40000)` in `memory.c` allinea solo l'offset dentro il modulo, al costo di circa 257 KB di bss. Al suo posto `arm_mips.c` piega la metà bassa dell'indirizzo della RAM nell'offset di load e store, con qualunque allineamento: un accesso a indirizzo indovinato passa da 5 a 3 istruzioni per formare l'indirizzo (`lui` nel delay slot, `ext`, `addu`), uno a indirizzo noto da 2 a 1 (solo `lui`). Il fuzzer non misura la differenza: i suoi programmi casuali fanno pochi accessi diretti riusciti (3,50 istruzioni host per istruzione guest con RAM allineata e non allineata). La misura su MKSC: `lockstep --jit --mode run --count --unaligned-ram --script tests/core/mksc_race.txt` (costruito come in `scripts/check-core.sh`) sul commit prima e su quello dopo, e senza `--unaligned-ram` come riferimento; poi compare-benchmarks 3+3 con New su PSP-1000.

**2026-11-04, SOA-3.2.** Stessa semantica del Classic: un B (anche condizionale, o un BL ARM) all'indirizzo della voce, quando salta, chiude il run come se il budget fosse finito e `update_gba()` passa al prossimo evento. Il backend lascia quei salti all'interprete. Non coperti, a differenza del Classic: il BL Thumb e la SWI a un indirizzo della lista. L'interruttore a run time (F-143) è la riga di `game_config.txt`. Nelle ROM italiane delle prove le voci non puntano al salto del ciclo: FireRed (BPRI) 080008c6 è un `bx r0` e LeafGreen (BPGI) 080008b6 il `bne` prima del ciclo; il salto del ciclo di attesa è a 080008c2 in entrambe. Per la misura su PSP servono voci verificate (CMP-2.3) o una voce corretta a mano.

**2026-11-04, SOA-3.6.** Il test salva con ciascun core e ciascun BIOS e ricarica con i due core: stesso BIOS, stessi registri in ogni modo; altro BIOS, rifiuto con il messaggio che nomina il BIOS richiesto. Modi provati: IRQ in Thumb e system; FIQ, che nessun gioco usa, solo come banco.
