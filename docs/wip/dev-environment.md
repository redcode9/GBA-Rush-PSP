# wip/dev-environment — Ambiente di verifica riproducibile

- **Branch:** `features/state-of-the-art-development/wip/dev-environment`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-10.4, SOA-1 (7 storie, circa 7 gg).
- **File principali:** `scripts/setup-dev.sh`, `scripts/check-*.sh`, regole check del `Makefile`.
- **Dipende da:** nessun altro branch.
- **Nota:** Da fare per primo: rende ripetibili check-core, check-render e la build PSP per chiunque.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **ENG-10.4** — Suite di test di terzi a hash fissi · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **SOA-1.1** — scripts/setup-dev.sh: toolchain pspdev e header SDK a versione fissata in ext/ · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **SOA-1.2** — mGBA 0.10.3 a 16 bit e harness di render e lockstep ricostruiti dallo script · S, 1 gg · dipende da: SOA-1.1 · PSP: no
- [ ] **SOA-1.3** — Unicorn con le patch Allegrex e dati di test della CPU a hash fissi · M, 1,5 gg · dipende da: SOA-1.1, ENG-10.4 · PSP: no
- [ ] **SOA-1.4** — PPSSPP headless ricostruibile e make check-eboot · M, 1,5 gg · dipende da: SOA-1.1 · PSP: no
- [ ] **SOA-1.5** — check-core e check-render rapidi per la CI e per ogni PR verso main · S, 1 gg · dipende da: SOA-1.2, SOA-1.3 · PSP: no
- [ ] **SOA-1.6** — make check senza dipendenze nascoste · S, 0,5 gg · dipende da: — · PSP: no · **subito**
