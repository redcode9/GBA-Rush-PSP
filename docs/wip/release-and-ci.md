# wip/release-and-ci — Rilascio, formati, profili di build e CI

- **Branch:** `features/state-of-the-art-development/wip/release-and-ci`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-1, ENG-2, ENG-5.1, ENG-5.4, ENG-18, ENG-19 (19 storie, circa 21,25 gg).
- **File principali:** release-gate nel `Makefile`, `scripts/package-release.sh`, workflow CI, `runtime_log*.c`.
- **Dipende da:** nessun altro branch.
- **Nota:** ENG-1.6 è l'unico bump dei formati della 1.5: gli altri branch aggiungono i loro campi lì.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **ENG-1.1** — Catalogo dei test hardware, docs/HARDWARE-RESULTS.md e scheda di sessione · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-1.2** — make release-gate e note di rilascio generate · M, 1,75 gg · dipende da: ENG-1.1, ENG-1.3 · PSP: no
- [ ] **ENG-1.3** — Manifest di build e SHA256SUMS nel pacchetto · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-1.4** — Sorgente corrispondente come asset di ogni release · S, 0,5 gg · dipende da: ENG-1.3 · PSP: no
- [ ] **ENG-1.5** — Canale prerelease e promozione a binario identico, primo uso sulla 1.4.4 · S, 1 gg · dipende da: ENG-1.2, ENG-1.3, ENG-1.4, F-001, F-002, F-003 · PSP: sì
- [ ] **ENG-2.6** — Controllo dei nomi vietati nel tree, nei nomi dei file e nei messaggi di commit · S, 0,5 gg · dipende da: ENG-1.2 · PSP: no
- [ ] **ENG-18.1** — Moduli di issue e template di PR · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-18.2** — Triage, Discussions e annunci · S, 0,5 gg · dipende da: ENG-18.1, ENG-1.5 · PSP: no

## 1.5

- [ ] **ENG-1.6** — Build id nel binario, nell'interfaccia e nei formati · M, 2,25 gg · dipende da: ENG-1.3 · PSP: sì
- [ ] **ENG-2.1** — Lint degli script in make check e pattern binari senza distinzione di maiuscole · S, 0,5 gg · dipende da: — · PSP: no · **subito**

## 1.6

- [ ] **ENG-2.2** — Profili release e diagnostic con controlli a compile time · M, 1,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **ENG-2.3** — -Werror sul codice del progetto nella build PSP · M, 1,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **ENG-2.4** — Audit dell'ELF e soglia di heap all'avvio nel pacchetto · S, 1 gg · dipende da: ENG-1.3, ENG-2.2 · PSP: no
- [ ] **ENG-2.5** — CI GitHub Actions in un container fissato · M, 2 gg · dipende da: ENG-1.2, ENG-1.3, ENG-2.2 · PSP: no
- [ ] **ENG-5.1** — make prove-regression REF=<tag> TEST=<nome> · M, 1,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-5.4** — Controllo che ogni modulo puro abbia i suoi test · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-19.1** — docs/UPSTREAM.md e script di revisione · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-19.2** — Proposte upstream verificate · S, 1,75 gg · dipende da: ENG-19.1 · PSP: no
- [ ] **ENG-19.3** — Revisione mensile di upstream · S, 0,5 gg · dipende da: ENG-19.1 · PSP: no
