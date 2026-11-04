# wip/hardware-validation — Prova su console del core New e di Exact

- **Branch:** `features/state-of-the-art-development/wip/hardware-validation`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-2 (5 storie, circa 6 gg).
- **File principali:** `docs/HARDWARE-RESULTS.md`, `scripts/package-release.sh`.
- **Dipende da:** `wip/dev-environment` (SOA-1.1).
- **Nota:** Tiene anche il calendario delle sessioni hardware degli altri branch: le console di prova sono due.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **SOA-2.1** — Build di prova da main con New ed Exact e installer che archivia prima i log · S, 0,5 gg · dipende da: SOA-1.1 · PSP: sì · aspetta: SOA-1.1 (`wip/dev-environment`)
- [ ] **SOA-2.2** — Sessione da soli: MKSC, FireRed, LeafGreen e Golden Sun su Classic/New × CPU/ME · M, 1,5 gg · dipende da: SOA-2.1 · PSP: sì
- [ ] **SOA-2.3** — Sessione link: gara MKSC e scambio FireRed↔LeafGreen con New ed Exact nei due ruoli · S, 1 gg · dipende da: SOA-2.2 · PSP: sì
- [ ] **SOA-2.4** — Analisi dei log e decisioni scritte per i gate di SOA-4, SOA-6 e SOA-7 · S, 1 gg · dipende da: SOA-2.2, SOA-2.3 · PSP: no
- [ ] **SOA-2.5** — Correzione dei difetti trovati su hardware · M, 2 gg · dipende da: SOA-2.4 · PSP: sì
