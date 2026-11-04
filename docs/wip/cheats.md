# wip/cheats — Trucchi

- **Branch:** `features/state-of-the-art-development/wip/cheats`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** GIO-3, GIO-4, GIO-5, GIO-6 (17 storie, circa 31,5 gg).
- **File principali:** `source/src/cheats.c`, `codebreaker.c`.
- **Dipende da:** `wip/open-bios` (CMP-1.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **GIO-3.1** — Parser estratto nel modulo puro cheat_file · M, 3 gg · dipende da: — · PSP: no · **subito**
- [ ] **GIO-3.2** — Limiti al parsing e budget statico per voce · M, 2 gg · dipende da: GIO-3.1 · PSP: sì
- [ ] **GIO-3.3** — Accessori riservati ai trucchi e tabella delle patch ROM · M, 3 gg · dipende da: GIO-3.2, ENG-1.6 · PSP: sì · aspetta: ENG-1.6 (`wip/release-and-ci`)
- [ ] **GIO-3.5** — Esiti dei trucchi localizzati · S, 1 gg · dipende da: GIO-3.2, GIO-3.3 · PSP: no
- [ ] **GIO-3.6** — Pool di codici condiviso · M, 2 gg · dipende da: GIO-3.1 · PSP: no
- [ ] **GIO-3.7** — Fuzz deterministico del parser in make check · S, 1 gg · dipende da: GIO-3.2 · PSP: no
- [ ] **GIO-4.1** — Caricamento automatico della lista del gioco · S, 1,5 gg · dipende da: GIO-3.1 · PSP: sì
- [ ] **GIO-4.2** — Scelte ricordate per hash del codice · M, 2 gg · dipende da: GIO-4.1, F-003 · PSP: sì
- [ ] **GIO-4.3** — Import dei file RetroArch .cht · M, 3 gg · dipende da: GIO-3.2 · PSP: no
- [ ] **GIO-4.4** — Errori per voce e liste lunghe · S, 1,5 gg · dipende da: GIO-3.1 · PSP: no
- [ ] **GIO-6.1** — Strumento host cheat_lint · S, 1 gg · dipende da: GIO-4.3 · PSP: no
- [ ] **GIO-6.2** — Script scripts/build-cheat-pack.py · M, 2,5 gg · dipende da: GIO-6.1 · PSP: no
- [ ] **GIO-6.3** — Asset di release e controlli del pacchetto · S, 1 gg · dipende da: GIO-6.2, F-139 · PSP: no
- [ ] **GIO-6.4** — LEGGIMI dei trucchi in cinque lingue · S, 1,5 gg · dipende da: GIO-4.4 · PSP: no

## 2.0

- [ ] **GIO-5.1** — Intestazione con conteggi, suggerimenti e 'Disattiva tutti' · M, 2 gg · dipende da: GIO-4.4 · PSP: sì
- [ ] **GIO-5.2** — Toast 'N trucchi attivi' all'avvio del gioco · S, 0,5 gg · dipende da: GIO-4.2, F-045 · PSP: sì
- [ ] **GIO-5.3** — Pagina trucchi dal menu opzioni della ROM nel browser · M, 3 gg · dipende da: GIO-4.2, GIO-5.1, UI-12.4 · PSP: sì · aspetta: UI-12.4 (`wip/library`)
