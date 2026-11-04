# wip/open-bios — BIOS integrato: HLE di Div, IntrWait, open bus e HALT

- **Branch:** `features/state-of-the-art-development/wip/open-bios`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-1, CMP-3 (tranne CMP-3.1), EXT-3 (tranne EXT-3.1) (11 storie, circa 14 gg).
- **File principali:** `source/bios/`, `source/src/open_bios.h`, `bios_hle.c` (nuovo), BIOS in `memory.c`, HALT in `main.c`.
- **Dipende da:** `wip/test-roms` (CMP-3.1).
- **Nota:** CMP-1.1 è già nel main dell'epica: restano revisione, build PSP, prova breve e merge verso develop (decisione (l)).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **CMP-1.1** — Chiudere e fondere il branch features/builtin-open-bios · S, 1 gg · dipende da: SAV-2.7, ENG-2.1 · PSP: sì · aspetta: SAV-2.7 (`wip/save-store`), ENG-2.1 (`wip/release-and-ci`) · _già nel main dell'epica con il core New e il renderer Exact; restano revisione, build PSP, prova breve su PSP-1000 e merge verso develop (decisione (l))_
- [ ] **CMP-1.2** — Div e DivArm in HLE nel dynarec · M, 2,5 gg · dipende da: CMP-1.1, CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **CMP-1.3** — IntrWait che termina anche sui flag 8-13 · S, 0,5 gg · dipende da: CMP-1.1, CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **CMP-1.4** — Tipo di BIOS e SWI nel runtime log (v6) e nel perf report (v7) · M, 1,5 gg · dipende da: CMP-1.1, CMP-1.2, ENG-1.6 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **CMP-1.5** — Passata di compatibilità su PSP con il BIOS integrato · M, 1,5 gg · dipende da: CMP-1.2, CMP-1.3, CMP-1.4 · PSP: sì
- [ ] **CMP-1.6** — Benchmark SWI-intensivo sui due BIOS · S, 1 gg · dipende da: CMP-1.4, CMP-1.5 · PSP: sì
- [ ] **CMP-3.2** — HALT termina su IE&IF anche con IME=0 o CPSR.I=1 · S, 1,5 gg · dipende da: CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **CMP-3.3** — Valore open bus del BIOS dopo una SWI scritto a run time · S, 0,5 gg · dipende da: CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **EXT-3.2** — Valori aritmetici registrati col BIOS ufficiale ed esecuzione sul BIOS integrato · M, 1 gg · dipende da: EXT-3.1, CMP-1.2 · PSP: sì · aspetta: EXT-3.1 (`wip/test-roms`)

## dopo gate

- [ ] **CMP-1.7** — Div e DivArm corretti nel sorgente del BIOS integrato (se l'HLE è rifiutata) · M, 2 gg · dipende da: CMP-1.1, CMP-3.1 · PSP: sì · aspetta: CMP-3.1 (`wip/test-roms`)
- [ ] **EXT-3.3** — (condizionato) SoundBias aggiorna SOUNDBIAS e MultiBoot restituisce 1 · S, 1 gg · dipende da: EXT-3.2, CMP-1.3 · PSP: sì
