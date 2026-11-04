# wip/new-core-speed — Core New veloce

- **Branch:** `features/state-of-the-art-development/wip/new-core-speed`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-4 (8 storie, circa 18 gg).
- **File principali:** `source/src/arm_mips.c`, `arm_mips.h`.
- **Dipende da:** `wip/hardware-validation`, `wip/perf-method` (PRF-2.2, PRF-8.6), `wip/platform` (SOA-8.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **SOA-4.1** — Profilo del core New su console: campioni per PC e per fase · M, 1,5 gg · dipende da: SOA-2.2, PRF-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`), PRF-2.2 (`wip/perf-method`)
- [ ] **SOA-4.2** — Accessi in memoria in linea con regione predetta per sito · L, 4 gg · dipende da: SOA-4.1 · PSP: sì
- [ ] **SOA-4.3** — Disciplina dell'I-cache: runtime caldo contiguo ≤ 6 KiB e percorsi lenti lontani · M, 2,5 gg · dipende da: SOA-4.1 · PSP: sì
- [ ] **SOA-4.4** — MUL con interlock calcolato in linea e cicli dei salti fissati in traduzione · S, 1 gg · dipende da: SOA-4.1 · PSP: no
- [ ] **SOA-4.5** — SMC: ritiro dei soli blocchi toccati e interprete per i blocchi invalidati spesso · M, 3 gg · dipende da: SOA-4.1, PRF-8.6 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`)
- [ ] **SOA-4.6** — Stato caldo del core nella scratchpad · S, 1,5 gg · dipende da: SOA-8.1 · PSP: sì · aspetta: SOA-8.1 (`wip/platform`)
- [ ] **SOA-4.8** — Traduzione più rapida: costo per istruzione misurato e ridotto · S, 1,5 gg · dipende da: SOA-4.1 · PSP: no

## dopo gate

- [ ] **SOA-4.7** — (condizionata) Superblocchi sulle tracce calde · M, 3 gg · dipende da: SOA-4.2, SOA-4.3 · PSP: sì
