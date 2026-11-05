# wip/new-core-speed — Core New veloce

- **Branch:** `features/state-of-the-art-development/wip/new-core-speed`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-4 (8 storie, circa 18 gg).
- **File principali:** `source/src/arm_mips.c`, `arm_mips.h`.
- **Dipende da:** `wip/hardware-validation`, `wip/perf-method` (PRF-2.2, PRF-8.6), `wip/platform` (SOA-8.1).
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **SOA-4.1** — Profilo del core New su console: campioni per PC e per fase · M, 1,5 gg · dipende da: SOA-2.2, PRF-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`), PRF-2.2 (`wip/perf-method`) · _implementato 2026-11-04, manca: il profilo su PSP-1000 che somma al 100% e la ripartizione per fase di MKSC, FireRed e un titolo SMC in `docs/DESIGN-DECISIONS.md` (build `make psp-build DIAGNOSTIC_CORE_PROFILE=1`, cattura dal menu Diagnostica, file `benchmarks/perf-*-core.txt`)_
- [ ] **SOA-4.2** — Accessi in memoria in linea con regione predetta per sito · L, 4 gg · dipende da: SOA-4.1 · PSP: sì
- [ ] **SOA-4.3** — Disciplina dell'I-cache: runtime caldo contiguo ≤ 6 KiB e percorsi lenti lontani · M, 2,5 gg · dipende da: SOA-4.1 · PSP: sì
- [ ] **SOA-4.4** — MUL con interlock calcolato in linea e cicli dei salti fissati in traduzione · S, 1 gg · dipende da: SOA-4.1 · PSP: no · _implementato 2026-11-04, manca: SingleStepTests (cicli compresi) e lockstep con mGBA, dati e build non disponibili qui; contro l'interprete il fuzzer dà 0 differenze_
- [ ] **SOA-4.5** — SMC: ritiro dei soli blocchi toccati e interprete per i blocchi invalidati spesso · M, 3 gg · dipende da: SOA-4.1, PRF-8.6 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`)
- [ ] **SOA-4.6** — Stato caldo del core nella scratchpad · S, 1,5 gg · dipende da: SOA-8.1 · PSP: sì · aspetta: SOA-8.1 (`wip/platform`)
- [ ] **SOA-4.8** — Traduzione più rapida: costo per istruzione misurato e ridotto · S, 1,5 gg · dipende da: SOA-4.1 · PSP: no · _implementato 2026-11-04, manca: il costo per istruzione su PSP (`translate-ns-per-instruction` nel profilo) confrontato con quello del Classic_

## dopo gate

- [ ] **SOA-4.7** — (condizionata) Superblocchi sulle tracce calde · M, 3 gg · dipende da: SOA-4.2, SOA-4.3 · PSP: sì

## Note dell'ondata del 2026-11-04

- **SOA-4.1.** `ArmMips.phase` dice sempre che cosa fa il core (codice emesso, dispatch, accessi in C, traduzione, interprete, eventi fuori dal core). Con `ArmMips.sampling` ogni blocco annota il proprio ingresso (`bltzal` che collega senza saltare, più uno store: 2 istruzioni per ingresso, solo nella build diagnostica), così un campione nel codice emesso nomina il blocco. Il campionatore (`arm_gba.c`, solo con `DIAGNOSTIC_CORE_PROFILE=1`) è un thread a priorità 0x12 che si sveglia ogni 997 µs: se il thread di emulazione è pronto conta la fase, se è in attesa conta `waiting`. Il report (`core_profile.c`, testato sull'host) esce accanto al perf report come `perf-*-core.txt`. Il PC host esatto dentro un blocco richiederebbe il kernel: si campiona il blocco. Da verificare su console: che `sceKernelReferThreadRunStatus` distingua pronto da in attesa come previsto, e il costo del thread di campionamento.
- **SOA-4.4.** L'interlock della MUL era già calcolato in linea con `clz`; ora costa un'istruzione e un registro in meno (`ori 0xFF` prima di `clz` al posto di `min` con 3). I cicli dei salti diretti sono già costanti nell'`INSN` e quelli dei salti indiretti nell'ingresso di refill del bersaglio: nessun ciclo di salto si calcola a run time nel codice emesso. Il fuzzer prova ogni moltiplicazione sui bordi dei byte, con i due segni, da IWRAM e da ROM con prefetch: 0 differenze.
- **SOA-4.8.** `tests/core/translate_bench.c` (in check-core con BIOS e MKSC) trova 16384 blocchi senza eseguirli e misura decodifica e compilazione sul Mac. Prima → dopo, ns per istruzione guest (decodifica + compilazione): MKSC 20 + 61 → 19 + 42, FireRed 22 + 74 → 21 + 51, LeafGreen 22 + 72 → 20 + 48; codice identico (hash) per le due pulizie. Il profilo indicava i cicli su tutti i 240 temporanei a ogni istruzione e a ogni percorso lento (un terzo della compilazione) e gli azzeramenti del decoder; il resto è distribuito senza un punto caldo, quindi la decodifica in tabella non è giustificata dalla misura. `ArmMipsHost.clock` conta il tempo di decodifica e di compilazione; su PSP lo legge solo la build diagnostica.
- **Istruzioni host per istruzione guest** (Unicorn, `jit_fuzz` semi 1-30, 74,35 M istruzioni guest): 3,64 prima e 3,64 dopo (3,85 sui 200 casi di check-core, 750 M istruzioni, tutti concordi): le pulizie della traduzione non cambiano il codice e la MUL più corta pesa poco sul codice casuale. Il numero di MKSC (10,8) richiede il lockstep con mGBA, assente qui; `translate_bench` dà invece le parole di codice per istruzione sul codice vero (MKSC 5,54 calde e 7,20 fredde).
- **Sbloccati dopo il profilo su PSP:** SOA-4.2, SOA-4.3, SOA-4.5, SOA-4.7 aspettano la ripartizione per fase; SOA-4.6 aspetta SOA-8.1.
