# wip/audio — Audio: mixer M4A nativo, 32 kHz e Media Engine

- **Branch:** `features/state-of-the-art-development/wip/audio`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** PRF-12, PRF-13, SOA-9 (12 storie, circa 33 gg).
- **File principali:** `source/src/sound.c`, `m4a_mixer.c` (nuovo).
- **Dipende da:** `wip/new-core-speed` (SOA-4.1), `wip/media-engine` (SOA-6.6), `wip/perf-method`.
- **Nota:** Dietro gate; PRF-13 è assorbita da SOA-9.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **PRF-12.1** — Misura del gate: build A/B a 32000 Hz · S, 1,5 gg · dipende da: PRF-3.2, PRF-1.1, CMP-1.4, F-137 · PSP: sì · aspetta: PRF-3.2 (`wip/perf-method`), PRF-1.1 (`wip/perf-method`), CMP-1.4 (`wip/open-bios`)

## dopo gate

- [ ] **PRF-12.2** — Frequenza di mix a run-time · M, 2,5 gg · dipende da: PRF-12.1 · PSP: sì
- [ ] **PRF-12.3** — Opzione per gioco "Qualità audio" · S, 1,5 gg · dipende da: PRF-12.2 · PSP: no
- [ ] **PRF-12.4** — Verifica d'ascolto e di deriva · S, 1 gg · dipende da: PRF-12.3 · PSP: sì
- [ ] **PRF-13.1** — Licenza e spike di avvio del Media Engine · L, 4 gg · dipende da: PRF-1.5, SAV-7.5, F-137 · PSP: sì · aspetta: PRF-1.5 (`wip/perf-method`), SAV-7.5 (`wip/suspend-resume`) · _avvio e licenza del ME già risolti dal PRX nostro_
- [ ] **PRF-13.2** — Modulo puro apu_cmd: coda di comandi audio e sintesi condivisa · M, 3 gg · dipende da: PRF-13.1 · PSP: no
- [ ] **PRF-13.3** — Mix audio sul ME con ritorno automatico alla CPU · L, 5 gg · dipende da: PRF-13.2 · PSP: sì
- [ ] **PRF-13.4** — Soak dell'audio sul ME · M, 2 gg · dipende da: PRF-13.3 · PSP: sì
- [ ] **SOA-9.1** — Firma del mixer M4A e quota misurata sui titoli di riferimento · S, 1,5 gg · dipende da: SOA-4.1 · PSP: sì · aspetta: SOA-4.1 (`wip/new-core-speed`)
- [ ] **SOA-9.2** — Mixer M4A nativo in C con hash audio identici · L, 5 gg · dipende da: SOA-9.1 · PSP: no
- [ ] **SOA-9.3** — Aggancio nei due core con interruttore e ripiego · M, 2 gg · dipende da: SOA-9.2 · PSP: sì
- [ ] **SOA-9.4** — (condizionata) Mixer sul ME accanto al renderer, con un solo programma ME · L, 4 gg · dipende da: SOA-9.3, SOA-6.6 · PSP: sì · aspetta: SOA-6.6 (`wip/media-engine`)
