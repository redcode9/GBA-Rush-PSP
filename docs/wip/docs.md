# wip/docs — Documentazione, README e larghezza dei testi

- **Branch:** `features/state-of-the-art-development/wip/docs`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-9, ENG-16, ENG-17 (11 storie, circa 13,75 gg).
- **File principali:** `README.md`, `docs/`, `scripts/check-messages.sh`.
- **Dipende da:** nessun altro branch.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **ENG-9.1** — Controllo host della larghezza dei messaggi · M, 1,5 gg · dipende da: — · PSP: no · **subito**

## 1.6

- [ ] **ENG-16.1** — Ristrutturazione del README secondo il piano · M, 1,5 gg · dipende da: ENG-1.2 · PSP: no · aspetta: ENG-1.2 (`wip/release-and-ci`)
- [ ] **ENG-16.2** — Tabelle di controlli e impostazioni, indice della documentazione · S, 1 gg · dipende da: ENG-17.2 · PSP: no
- [ ] **ENG-16.3** — Tabella di compatibilità per gioco generata dai report · M, 1,5 gg · dipende da: PRF-1.4, PRF-1.5, ENG-1.6 · PSP: no · aspetta: PRF-1.4 (`wip/perf-method`), PRF-1.5 (`wip/perf-method`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **ENG-17.1** — docs/TEST-MAP.md verificato · S, 0,75 gg · dipende da: ENG-5.4 · PSP: no · aspetta: ENG-5.4 (`wip/release-and-ci`)
- [ ] **ENG-17.2** — docs/INTERNALS.md e controllo dei documenti · M, 1,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-17.3** — docs/DESIGN-DECISIONS.md con gli esperimenti scartati · S, 1 gg · dipende da: ENG-17.2 · PSP: no
- [ ] **ENG-17.4** — docs/TROUBLESHOOTING.md per sintomi · S, 1 gg · dipende da: ENG-17.2 · PSP: no

## 2.0

- [ ] **ENG-9.2** — Navigatore diagnostico dei menu con dump dei frame · M, 2 gg · dipende da: F-053, F-048, ENG-6.2 · PSP: sì · aspetta: ENG-6.2 (`wip/replay-oracle`)
- [ ] **ENG-16.4** — Media reali e striscia di screenshot · S, 1,5 gg · dipende da: ENG-16.1, ENG-9.2 · PSP: sì
- [ ] **ENG-16.5** — Tabella per scena: mediana, minimo e p99 per modello · S, 0,5 gg · dipende da: ENG-16.3, PRF-1.7 · PSP: no · aspetta: PRF-1.7 (`wip/perf-method`)
