# wip/savestate-v2 — Formato savestate v2

- **Branch:** `features/state-of-the-art-development/wip/savestate-v2`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SAV-4 (6 storie, circa 6 gg).
- **File principali:** savestate in `source/src/memory.c`, `savestate_trailer.c` e `savestate_check.c` (nuovi).
- **Dipende da:** `wip/save-store` (SAV-1.7, SAV-2.5), `wip/open-bios` (CMP-1.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **SAV-4.1** — Modulo puro savestate_trailer: trailer v2 dopo i 512 KiB · M, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: test host verdi anche con UBSan e prova CBMC scritta; manca la verifica della CI (`make test-sanitize`, cioè il fuzz da 100k input sotto ASan, e `make check-cbmc`, mai eseguita)_
- [ ] **SAV-4.2** — Inventario dei campi e modulo puro savestate_check · M, 1,5 gg · dipende da: SAV-1.7 · PSP: no · aspetta: SAV-1.7 (`wip/save-store`)
- [ ] **SAV-4.3** — Scrittura del trailer v2 e decoder per PC · S, 0,5 gg · dipende da: SAV-4.1, SAV-2.5 · PSP: no · aspetta: SAV-2.5 (`wip/save-store`)
- [ ] **SAV-4.4** — Caricamento validato prima di applicare · M, 1,5 gg · dipende da: SAV-4.1, SAV-4.2, SAV-4.3 · PSP: sì
- [ ] **SAV-4.5** — Annulla ultimo salvataggio di stato · S, 0,5 gg · dipende da: SAV-2.5 · PSP: no · aspetta: SAV-2.5 (`wip/save-store`)
- [ ] **SAV-4.6** — Migrazione e compatibilità verificate · S, 1 gg · dipende da: SAV-4.4, SAV-4.5 · PSP: sì
