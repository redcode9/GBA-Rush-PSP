# wip/suspend-resume — Sospensione e ripresa sicure

- **Branch:** `features/state-of-the-art-development/wip/suspend-resume`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-8, SAV-7, SAV-8 (11 storie, circa 12 gg).
- **File principali:** callback di alimentazione in `source/src/main.c`, power lifecycle.
- **Dipende da:** `wip/save-store`, `wip/test-roms` (SAV-2.6).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **ENG-8.3** — Test di proprietà sulla macchina a stati power · S, 0,5 gg · dipende da: SAV-7.6 · PSP: no
- [ ] **SAV-7.1** — Finestra di sospensione senza allocazioni né dialoghi · S, 1 gg · dipende da: SAV-2.5 · PSP: sì · aspetta: SAV-2.5 (`wip/save-store`)
- [ ] **SAV-7.2** — Batteria in sospeso scritta per prima alla sospensione · S, 0,5 gg · dipende da: SAV-2.3 · PSP: sì · aspetta: SAV-2.3 (`wip/save-store`)
- [ ] **SAV-7.3** — POWER tenuto durante l'avvio e la chiusura del link · S, 0,5 gg · dipende da: SAV-3.2 · PSP: sì · aspetta: SAV-3.2 (`wip/save-store`)
- [ ] **SAV-7.5** — Matrice hardware di sospensione e ripresa · M, 2,5 gg · dipende da: SAV-7.1, SAV-7.2, SAV-7.3, SAV-7.6, SAV-2.6 · PSP: sì · aspetta: SAV-2.6 (`wip/test-roms`)
- [ ] **SAV-7.6** — Ingresso in standby con ogni combinazione di bit e chiusura del menu senza attese · S, 1 gg · dipende da: — · PSP: sì · **subito**
- [ ] **SAV-8.1** — Esperimento: il POWER aspetta l'unlock? · S, 1 gg · dipende da: SAV-7.5 · PSP: sì

## 2.0

- [ ] **ENG-8.1** — Iniettore diagnostico di eventi power e HOME · M, 1,5 gg · dipende da: ENG-6.2, ENG-2.2 · PSP: sì · aspetta: ENG-6.2 (`wip/replay-oracle`), ENG-2.2 (`wip/release-and-ci`)

## dopo gate

- [ ] **ENG-8.2** — Scenari di sospensione in PPSSPP · S, 1 gg · dipende da: ENG-8.1, ENG-6.3, ENG-7.3 · PSP: no · aspetta: ENG-6.3 (`wip/replay-oracle`), ENG-7.3 (`wip/replay-oracle`)
- [ ] **SAV-8.2** — Parcheggio prima della sospensione: AUTO, lock e handle chiusi · S, 1,5 gg · dipende da: SAV-8.1 · PSP: no
- [ ] **SAV-8.3** — Matrice ripetuta e rilascio · S, 1 gg · dipende da: SAV-8.2 · PSP: sì
