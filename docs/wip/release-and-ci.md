# wip/release-and-ci — Rilascio, formati, profili di build e CI

- **Branch:** `features/state-of-the-art-development/wip/release-and-ci`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-1, ENG-2, ENG-5.1, ENG-5.4, ENG-18, ENG-19 (19 storie, circa 21,25 gg).
- **File principali:** release-gate nel `Makefile`, `scripts/package-release.sh`, workflow CI, `runtime_log*.c`.
- **Dipende da:** nessun altro branch.
- **Nota:** ENG-1.6 è l'unico bump dei formati della 1.5: gli altri branch aggiungono i loro campi lì.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [x] **ENG-1.1** — Catalogo dei test hardware, docs/HARDWARE-RESULTS.md e scheda di sessione · S, 1 gg · dipende da: — · PSP: no · **subito**
- [x] **ENG-1.2** — make release-gate e note di rilascio generate · M, 1,75 gg · dipende da: ENG-1.1, ENG-1.3 · PSP: no
- [ ] **ENG-1.3** — Manifest di build e SHA256SUMS nel pacchetto · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: `make package` con la toolchain PSPDev vera è verde in CI (run 42515671720); manca `make artifact-smoke` su un pacchetto vero, che la CI esegue solo sui tag_
- [ ] **ENG-1.4** — Sorgente corrispondente come asset di ogni release · S, 0,5 gg · dipende da: ENG-1.3 · PSP: no · _in parte: manca `make package` con la toolchain PSPDev vera sul tag 1.4.4 (provato solo su tag di prova)_
- [ ] **ENG-1.5** — Canale prerelease e promozione a binario identico, primo uso sulla 1.4.4 · S, 1 gg · dipende da: ENG-1.2, ENG-1.3, ENG-1.4, F-001, F-002, F-003 · PSP: sì
- [x] **ENG-2.6** — Controllo dei nomi vietati nel tree, nei nomi dei file e nei messaggi di commit · S, 0,5 gg · dipende da: ENG-1.2 · PSP: no
- [ ] **ENG-18.1** — Moduli di issue e template di PR · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: manca la verifica su GitHub (i tre moduli e i campi obbligatori), possibile solo dopo il merge nel branch di default_
- [ ] **ENG-18.2** — Triage, Discussions e annunci · S, 0,5 gg · dipende da: ENG-18.1, ENG-1.5 · PSP: no

## 1.5

- [ ] **ENG-1.6** — Build id nel binario, nell'interfaccia e nei formati · M, 2,25 gg · dipende da: ENG-1.3 · PSP: sì · _codice pronto (33400c0) e riga del CHANGELOG scritta (formati 6 e 7); manca solo la prova su PSP. La build da main porta il build id e l'installer di SOA-2.1 (`scripts/install-test-build.sh`) è nel tree: la prova va nella prossima sessione SOA-2.2 (pagina Diagnostica, report e log decodificato mostrano il build id del manifest)_
- [x] **ENG-2.1** — Lint degli script in make check e pattern binari senza distinzione di maiuscole · S, 0,5 gg · dipende da: — · PSP: no · **subito**

## 1.6

- [ ] **ENG-2.2** — Profili release e diagnostic con controlli a compile time · M, 1,5 gg · dipende da: — · PSP: sì · **subito** · _codice pronto (8bd2ee6), manca la prova su PSP_
- [ ] **ENG-2.3** — -Werror sul codice del progetto nella build PSP · M, 1,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **ENG-2.4** — Audit dell'ELF e soglia di heap all'avvio nel pacchetto · S, 1 gg · dipende da: ENG-1.3, ENG-2.2 · PSP: no · _in parte: `make package` passa l'audit di un TempGBA.elf vero in CI (run 42515671720); la soglia di heap aspetta PRF-4.2_
- [ ] **ENG-2.5** — CI GitHub Actions in un container fissato · M, 2 gg · dipende da: ENG-1.2, ENG-1.3, ENG-2.2 · PSP: no · _in parte: la PR 22 ha mostrato lo stato della CI (otto controlli verdi, quattro job per il push e quattro per la PR, che coprono make check, test-sanitize, check-cbmc, le due build PSP, make package e due build release con lo stesso EBOOT); i pacchetti apt delle due immagini sono fissati a uno snapshot dell'archivio Ubuntu (9c0414b) e la riproducibilità dell'EBOOT è scritta in CONTRIBUTING.md. Mancano la CI verde con il Dockerfile dello snapshot e, dal manutentore, una PR con un test rotto che diventa rossa e un tag stabile senza righe hardware che fa fallire il job di rilascio_
- [ ] **ENG-5.1** — make prove-regression REF=<tag> TEST=<nome> · M, 1,5 gg · dipende da: — · PSP: no · **subito** · _in parte: manca l'esito FAIL-AS-EXPECTED su un bug reale (serve make e un cc host)_
- [x] **ENG-5.4** — Controllo che ogni modulo puro abbia i suoi test · S, 0,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **ENG-19.1** — docs/UPSTREAM.md e script di revisione · S, 1 gg · dipende da: — · PSP: no · **subito** · _in parte: manca il controllo in check-docs (`scripts/check-docs.sh` arriva con ENG-17.2, wip/docs)_
- [ ] **ENG-19.2** — Proposte upstream verificate · S, 1,75 gg · dipende da: ENG-19.1 · PSP: no · _in parte: le proposte vanno aperte su libretro/gpsp dal maintainer e il loro esito registrato; CMP-11.2, 11.3 e 11.4 aspettano wip/classic-core e wip/test-roms_
- [ ] **ENG-19.3** — Revisione mensile di upstream · S, 0,5 gg · dipende da: ENG-19.1 · PSP: no · _in parte: manca l'avviso in check-docs (`sh scripts/upstream-review.sh --check-age`, da agganciare con ENG-17.2)_
