# wip/new-core — Core New: parità con il Classic, poi predefinito

- **Branch:** `features/state-of-the-art-development/wip/new-core`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-3, SOA-10 (10 storie, circa 15 gg).
- **File principali:** `source/src/arm_gba.c`, `arm_decode.c`, `arm_interp.c`, `arm_core.h`, `arm_ir.h`.
- **Dipende da:** `wip/hardware-validation`, `wip/open-bios` (CMP-1.2, CMP-3.2, CMP-3.3), `wip/test-roms`, `wip/game-compat` (CMP-9.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **SOA-3.1** — Work RAM allineata per l'accesso diretto del core New · S, 1 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-3.2** — Idle loop di game_config.txt con il core New · M, 1,5 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`)
- [ ] **SOA-3.3** — BIOS integrato con il core New: HLE di Div/DivArm e open bus a run time · M, 2 gg · dipende da: CMP-1.2, CMP-3.3 · PSP: sì · aspetta: CMP-1.2 (`wip/open-bios`), CMP-3.3 (`wip/open-bios`)
- [ ] **SOA-3.4** — HALT e IRQ sbloccati con il core New · S, 1 gg · dipende da: CMP-3.2, CMP-11.1 · PSP: sì · aspetta: CMP-3.2 (`wip/open-bios`), CMP-11.1 (`wip/test-roms`)

## 1.6

- [ ] **SOA-3.5** — Core-rom completa verde con New su due modelli · M, 1,5 gg · dipende da: CMP-11.1, CMP-12.1, SOA-3.3, SOA-3.4 · PSP: sì · aspetta: CMP-11.1 (`wip/test-roms`), CMP-12.1 (`wip/test-roms`)
- [ ] **SOA-3.6** — Savestate tra core e tra BIOS: matrice delle quattro combinazioni · S, 1 gg · dipende da: SOA-3.3 · PSP: sì
- [ ] **SOA-3.7** — Passata di compatibilità con New sui 40 titoli di CMP-9.4 · M, 2,5 gg · dipende da: SOA-3.5, CMP-9.1 · PSP: sì · aspetta: CMP-9.1 (`wip/game-compat`)

## 2.0

- [ ] **SOA-10.1** — Confronto finale Classic/New e decisione scritta · S, 1,5 gg · dipende da: SOA-3.7, SOA-4.2 · PSP: sì · aspetta: SOA-4.2 (`wip/new-core-speed`)
- [ ] **SOA-10.2** — New predefinito, con migrazione delle impostazioni e Classic come ripiego per gioco · S, 1 gg · dipende da: SOA-10.1 · PSP: sì
- [ ] **SOA-10.3** — Rimozione del Classic e chiusura delle storie solo Classic · M, 2 gg · dipende da: SOA-10.2 · PSP: no
