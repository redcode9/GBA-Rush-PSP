# wip/hardware-validation — Prova su console del core New e di Exact

- **Branch:** `features/state-of-the-art-development/wip/hardware-validation`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** SOA-2 (5 storie, circa 6 gg).
- **File principali:** `docs/HARDWARE-RESULTS.md`, `scripts/package-release.sh`.
- **Dipende da:** `wip/dev-environment` (SOA-1.1).
- **Nota:** Tiene anche il calendario delle sessioni hardware degli altri branch: le console di prova sono due.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **SOA-2.1** — Build di prova da main con New ed Exact e installer che archivia prima i log · S, 0,5 gg · dipende da: SOA-1.1 · PSP: sì · aspetta: SOA-1.1 (`wip/dev-environment`) · _implementato 2026-11-04, manca: installazione sulle due PSP con il permesso dell'utente e lo sha256 riletto dalle due schede uguale (pacchetto e installer pronti, sotto; la build è venuta con la toolchain locale, senza `setup-dev.sh`)_
- [ ] **SOA-2.2** — Sessione da soli: MKSC, FireRed, LeafGreen e Golden Sun su Classic/New × CPU/ME · M, 1,5 gg · dipende da: SOA-2.1 · PSP: sì · _piano pronto 2026-11-04 (sotto), manca: la sessione sulle due PSP, con Golden Sun sulle schede_
- [ ] **SOA-2.3** — Sessione link: gara MKSC e scambio FireRed↔LeafGreen con New ed Exact nei due ruoli · S, 1 gg · dipende da: SOA-2.2 · PSP: sì · _piano pronto 2026-11-04 (sotto), manca: la sessione sulle due PSP e `tools/gen3_sav_check.py` (ENG-10.1, `wip/link-diagnostics`)_
- [ ] **SOA-2.4** — Analisi dei log e decisioni scritte per i gate di SOA-4, SOA-6 e SOA-7 · S, 1 gg · dipende da: SOA-2.2, SOA-2.3 · PSP: no
- [ ] **SOA-2.5** — Correzione dei difetti trovati su hardware · M, 2 gg · dipende da: SOA-2.4 · PSP: sì

## Pacchetto di prova (SOA-2.1)

- **Build:** da `features/state-of-the-art-development/main` 700de75 (questo branch aggiunge solo script e documenti, l'EBOOT è lo stesso), pspdev v20260801, psp-gcc 15.2. Una build pulita ridà gli stessi byte.
- **EBOOT.PBP:** `5dfde160d28be296b91890fd8be009c0fb00fb18605bb618bd08b0e91302438f` (5 608 188 byte); `exception.prx` `75d7347e9f5898acb1396dd0133c5ed5e952d431278168b88ff0dbe20c930762`; `media_engine.prx` `10cd22cb758ca882ec8872d97afd19e941634f1c6131c283398205feca856e76`. Riga in `docs/HARDWARE-RESULTS.md`.
- **Pacchetto:** `make package VERSION=test-700de75` → `dist/GBA-Rush-PSP-test-700de75.zip`, `check-package.sh` verde. Il nome dice il commit di main da cui viene la build, non una versione: le versioni sono solo `X.Y.Z` e questa build si presenta come 1.4.3. Lo sha256 dello zip cambia con la data dell'ultimo commit (le date dei file nello zip), quello dell'EBOOT no.
- **La build si presenta come 1.4.3** (`GBA_RUSH_VERSION` non è ancora cambiato): nei report della Diagnostica `version=1.4.3`. Il legame con la build lo danno lo sha256 e la cartella di `artifacts/hardware/` in cui l'installer li ha raccolti.
- **Installer:** `sh scripts/install-test-build.sh [--dry-run] [--no-media-engine] dist/GBA-Rush-PSP-test-700de75.zip`. Per ogni PSP montata (PSP_2004 → `PSP/GAME/CAT_Emulators/gpSP`, PSP_1004 → `PSP/GAME/gpSP`): copia e confronta `logs/`, `benchmarks/`, `traces/`, `GBA/SAV/`, `tempgba_crash.log` e la build che sostituisce in `artifacts/hardware/<data>-<ora>/<volume>/` del checkout principale anche se lanciato da un worktree (`git worktree remove` cancellerebbe senza avvisi la cartella ignorata, unica copia dei log tolti dalla scheda); controlla lo spazio (lascia 2 MB liberi, `PSP_MIN_FREE_KB`); copia i tre file; smonta e rimonta la scheda per rileggerli e confrontare lo sha256 (`installed.sha256` nella cartella); toglie dalla scheda solo log, report e crash log già archiviati; espelle. Non tocca mai salvataggi, savestate, ROM, BIOS, trucchi e impostazioni. `--no-media-engine` archivia e toglie `media_engine.prx`, così Exact disegna sulla CPU; rilanciato senza l'opzione lo rimette. Provato solo con `--dry-run` su un albero finto (`tests/test_install_test_build.py` in `make check`).
- **Ogni collegamento USB durante le sessioni passa dall'installer:** archivia i log del blocco appena finito e riparte con log puliti. Il runtime log tiene solo le ultime 4 sessioni (`logs/session-1..4.gbrlog`, una per avvio dell'EBOOT): mai più di 4 avvii tra due collegamenti.

## Piano della sessione da soli (SOA-2.2, con CMP-1.1 e la prova di sospensione)

**Che cosa serve sulle schede.** PSP-2000: `mariokart.gba`, `rosso.gba` + `ROSSO.sav`, `verde.gba` + `verde.sav` ci sono già; manca Golden Sun. PSP-1000 (Memory Stick da ~31 MB, 5,6 MB di EBOOT): una ROM grande alla volta; oggi ha `mariokart.gba` (FireRed è in `~/Development/PSP-Backups/psp1004-firered-before-mariokart-20261101`). Golden Sun lo porta l'utente. Su entrambe resta `gba_bios.bin`, tranne nella prova CMP-1.1.

**Impostazioni, uguali per tutta la sessione** (il report le registra): Video > Video renderer: Exact; Interfaccia > Show FPS: sì (il contatore finisce in ME o CPU, e in NEW con il core New); Prestazioni > CPU clock freq: 333 MHz; frameskip automatico (predefinito); avanti veloce: velocità massima, attivazione tenendo premuto (predefiniti).

**Una prova** (una riga della tabella) = 60 s di Diagnostica:
1. Caricare il savestate della scena del gioco (creato una volta all'inizio, slot 1): MKSC gara Grand Prix 50 cc, Coppa Fungo, primo circuito, dal via; FireRed e LeafGreen all'aperto nella prima città, camminando; Golden Sun nella prima città, camminando.
2. Diagnostica > Start recording, Continua, giocare 60 s facendo sempre le stesse cose, poi Diagnostica > Stop recording > Save report (`benchmarks/perf-<data>-<ora>.txt`).
3. Annotare il suffisso del contatore FPS (ME/CPU, NEW) e qualsiasi difetto visto.
4. Solo per MKSC, subito dopo: altri 30 s di Diagnostica tenendo premuto l'avanti veloce. Senza limite il tempo del frame (`frame-time-p50/p95-us`) è il costo di emulazione più presentazione: è il numero che serve al gate di SOA-4 (≤ 7,0 ms con New).

**Un gioco** = Classic poi New, nello stesso avvio dell'EBOOT:
1. Prestazioni > Advanced settings > CPU core: Classic, Gioco > Riavvia ROM, una prova.
2. CPU core: New, Gioco > Riavvia ROM, controllare che il contatore finisca in NEW, una prova.
3. Solo per MKSC e solo nel blocco ME: Video renderer: New (il renderer di oggi) e una prova per core, come riferimento senza Exact; poi di nuovo Exact.
4. Esci da GBA Rush PSP (menu Esci; non spegnere): solo all'uscita il runtime log scrive i contatori del ME (record 9-11, cumulati dall'avvio) e del core (record 12).

**PSP-2000** (scheda SD, tutte le ROM insieme):
1. USB: `sh scripts/install-test-build.sh dist/GBA-Rush-PSP-test-700de75.zip` (archivia i log della 1.4.3). Annotare lo sha256 stampato.
2. Blocco ME: MKSC, FireRed, LeafGreen, Golden Sun (4 avvii). Il contatore deve finire in ME.
3. Prova di sospensione con il ME acceso, nello stesso avvio di Golden Sun dopo le sue prove (così il blocco resta di 4 avvii): 20 cicli con l'interruttore (5 s spenta, poi riaccesa). Ogni volta: il gioco riparte, l'immagine si muove, il suono c'è, il contatore resta in ME. Se diventa CPU annotare il ciclo; se la console si blocca, tenere l'interruttore 10 s, annotare il ciclo e non cancellare niente. Poi Esci.
4. USB: `... --no-media-engine ...` (archivia il blocco ME).
5. Blocco CPU: gli stessi 4 giochi; il contatore deve finire in CPU.
6. USB: installer senza opzioni (archivia il blocco CPU e rimette il ME).

**PSP-1000** (una ROM alla volta; a ogni collegamento USB si cambia la ROM o il ME, mai i due insieme, e si lancia sempre l'installer, che archivia anche `GBA/SAV/`):
1. USB: installer senza opzioni. MKSC con il ME.
2. USB: `--no-media-engine`. MKSC con la CPU.
3. USB: togliere `mariokart.gba` (copia di riserva già nei backup), copiare `rosso.gba` e `ROSSO.sav`; rinominare `gba_bios.bin` in `gba_bios.bin.off`; installer con `--no-media-engine`.
4. **CMP-1.1, senza `gba_bios.bin`:** FireRed con Classic fino al gioco (Continua dal salvataggio, non dai savestate, che con il BIOS integrato vengono rifiutati con un messaggio), poi CPU core: New, Riavvia ROM, di nuovo fino al gioco (il BIOS integrato non è mai stato provato con New). Non salvare in gioco. Esci.
5. USB: rimettere `gba_bios.bin`; installer `--no-media-engine`. FireRed con la CPU (gioco completo, 2 prove).
6. USB: installer senza opzioni. FireRed con il ME; poi la prova di sospensione (20 cicli, come sopra) ed Esci.
7. USB: FireRed via (salvataggio archiviato), copiare `verde.gba` + `verde.sav`; installer senza opzioni. LeafGreen con il ME.
8. USB: `--no-media-engine`. LeafGreen con la CPU.
9. USB: LeafGreen via, copiare Golden Sun; rinominare `gba_bios.bin` in `gba_bios.bin.off`; installer `--no-media-engine`. **CMP-1.1:** Golden Sun con Classic e con New fino al gioco senza `gba_bios.bin`. Esci.
10. USB: rimettere `gba_bios.bin`; installer `--no-media-engine`. Golden Sun con la CPU.
11. USB: installer senza opzioni. Golden Sun con il ME.
12. USB: installer senza opzioni (archivia l'ultimo blocco); rimettere la ROM che l'utente vuole tenere.

**Che cosa si salva.** Tutto lo raccoglie l'installer in `artifacts/hardware/<data>-<ora>/PSP_2004|PSP_1004/`: `benchmarks/perf-*.txt` (un report per prova, nell'ordine del piano), `logs/session-*.gbrlog` (uno per avvio: ROM aperte con il core, sospensioni e riprese, record 9-12 all'uscita), `tempgba_crash.log` se c'è stato un crash, `installed.sha256`. Annotare a mano, per ogni cartella, il blocco (ME/CPU) e l'ordine delle prove: il report non dice né il core né dove disegna Exact.

**Durata stimata:** PSP-2000 circa 1 h 30 (18 prove, 2 USB, 20 sospensioni); PSP-1000 circa 2 h 30 per i 12 collegamenti.

**Lettura dei risultati:** `python3 tools/decode_runtime_log.py artifacts/hardware/<cartella>/<volume>/logs` per gli eventi (`rom-open ... core=`, `power-suspend/resume`, `exact-renderer=`, `me-frames/frames-late/me-busy-us-per-frame`, `log-us-per-frame/...`, `cpu-core=... blocks-compiled=`); dai report `emulated-frames`, `presented-frames`, `skipped-frames`, `frame-time-p50/p95/p99-us`, `renderer-total-us` diviso per `presented-frames`. Le righe vanno in `docs/HARDWARE-RESULTS.md`, con l'id di prova `solo-<gioco>-<core>-<me|cpu>` finché ENG-1.1 non porta il catalogo.

## Piano della sessione link (SOA-2.3)

**Prima:** SOA-2.2 finita su entrambe e installer senza opzioni su entrambe. Core New, Video renderer Exact, Show FPS sì. PSP-2000 host e PSP-1000 join, poi il contrario. In ogni gara o scambio Diagnostica 60 s su entrambe le PSP dall'inizio (il p99 del tempo di frame nei due ruoli è la misura del link per il principio 8), poi Collegamento > disconnetti ed Esci: alla chiusura il runtime log scrive i gruppi 0-8 del link e i record 9-12 della sessione link.

1. **Gare con il ME acceso:** MKSC, gara Grand Prix 50 cc in link, primo circuito, fino al traguardo. Gara A con la PSP-2000 host, gara B con la PSP-1000 host (2 avvii per PSP).
2. USB su entrambe: installer `--no-media-engine` (archivia le gare A e B). Video renderer: New, perché Exact sulla CPU è più lento e mescolerebbe il costo del renderer con quello del link.
3. **Gare con il ME spento:** gara C (PSP-2000 host) e gara D (PSP-1000 host). Il confronto A/B contro C/D dà l'RTT con il ME acceso e spento (obiettivo SOA-6: al massimo +1 ms).
4. USB su entrambe: installer senza opzioni (archivia C e D, rimette il ME). Sulla PSP-1000 togliere `mariokart.gba` e copiare `verde.gba` con un salvataggio di LeafGreen che abbia un Pokémon da scambiare. Video renderer: Exact.
5. **Scambi:** PSP-2000 FireRed (`rosso.gba`, `ROSSO.sav`), PSP-1000 LeafGreen. Scambio 1 con la PSP-2000 host: un Pokémon per parte nel Centro Comunicazioni, il gioco salva da solo a fine scambio; uscire dal centro, disconnettere, Esci. Scambio 2 con la PSP-1000 host: lo scambio inverso.
6. USB su entrambe: installer senza opzioni. Archivia i log e i `.sav` dopo gli scambi, per il controllo di `tools/gen3_sav_check.py`.

**Lettura:** `python3 tools/decode_runtime_log.py --summary .../logs`: gruppo 0 (RTT, massimo, rinvii), 6 (RTT minimo), 3 (attese), 8 (frame, frame disegnati, attesa del V-Blank), 9-11 (ME), 12 (core). Gli fps di un ruolo sono i frame del gruppo 8 divisi per il tempo tra `link-state ... status=connected` e `link-error`. `tools/analyze_link_benchmark.py`, citato dal ticket, è stato tolto con la pila del protocollo v3 (84e2616): il decoder basta per questi numeri. `tools/gen3_sav_check.py` arriva con ENG-10.1 (`wip/link-diagnostics`), non ancora in main.

## Calendario delle sessioni hardware

| Sessione | Branch | Console | Che cosa | Stato |
|---|---|---|---|---|
| 1 | `wip/hardware-validation` (+ CMP-1.1 di `wip/open-bios`) | PSP-2000, PSP-1000 | SOA-2.1 installazione, SOA-2.2, CMP-1.1, 20 sospensioni con il ME | pronta, aspetta l'utente |
| 2 | `wip/hardware-validation` | le due insieme | SOA-2.3 gare e scambi | dopo la 1 |
