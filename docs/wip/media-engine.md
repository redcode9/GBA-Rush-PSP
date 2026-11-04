# wip/media-engine — Renderer Exact sul Media Engine e presentazione

- **Branch:** `features/state-of-the-art-development/wip/media-engine`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** PRF-11, PRF-14, SOA-6 (19 storie, circa 43,75 gg).
- **File principali:** `source/prx/media_engine/`, `source/src/video_me.c`, `gba_ppu*.c`, `psp_video.c`.
- **Dipende da:** `wip/hardware-validation`, `wip/suspend-resume` (SAV-7.5).
- **Nota:** PRF-14 è superata da SOA-6: le sue storie si chiudono con quelle di SOA-6 che ne prendono gli obiettivi.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.6

- [ ] **PRF-11.1** — Misura del gate: cucitura e frame in ritardo · S, 1 gg · dipende da: PRF-2.1, PRF-1.5 · PSP: sì · aspetta: PRF-2.1 (`wip/perf-method`), PRF-1.5 (`wip/perf-method`)
- [ ] **PRF-11.5** — Latenza dell'input: telemetria nel perf report · M, 1,75 gg · dipende da: PRF-2.1, ENG-1.6 · PSP: sì · aspetta: PRF-2.1 (`wip/perf-method`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **SOA-6.1** — Attesa del ME con halt e interrupt invece del polling · M, 2,5 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: prova su console (RTT ad-hoc con ME acceso e spento, frame persi rispetto al polling), registri dell'interrupt da confermare; la fine frame verso la CPU principale resta a polling (vedi note)_
- [ ] **SOA-6.2** — Esperimento: flag di sincronizzazione nella memoria locale del ME o nella SRAM condivisa · S, 1,5 gg · dipende da: SOA-6.1 · PSP: sì
- [ ] **SOA-6.3** — Copie delle pagine sporche senza CPU e writeback per intervallo · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: costo video ≤ 0,8 ms per frame su MKSC misurato su console, check-render (serve mGBA)_
- [ ] **SOA-6.4** — Presentazione: strisce ≤ 128 px dalla VRAM e scan-out diretto a scala 1x · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: tempo di GE misurato e pubblicato, foto su PSP-1000 per ogni scala, check-video su console_
- [ ] **SOA-6.5** — Codice caldo del ME entro 16 KiB e dati nella memoria locale del ME · M, 2 gg · dipende da: SOA-2.2 · PSP: sì · aspetta: SOA-2.2 (`wip/hardware-validation`) · _implementato 2026-11-04, manca: p99 del rendering di MKSC sul ME sotto 12 ms misurato su console (record 13)_
- [ ] **SOA-6.6** — Sospensione, ripresa e soak di 8 ore e 100 sleep/wake · M, 2,5 gg · dipende da: SOA-6.1, SAV-7.5 · PSP: sì · aspetta: SAV-7.5 (`wip/suspend-resume`) · _implementato 2026-11-04 (parte codice), manca: soak di 8 ore e 100 sleep/wake su PSP-1000 e su Go o PSP-2000, SAV-7.5_
- [ ] **SOA-6.7** — Latenza misurata e frame di latenza scritto nell'aiuto · S, 1 gg · dipende da: PRF-11.5, SOA-6.4 · PSP: sì

## dopo gate

- [ ] **PRF-11.2** — Modulo puro frame_ring · S, 1 gg · dipende da: PRF-11.1 · PSP: no
- [ ] **PRF-11.3** — Triplo buffer in psp_video.c · M, 2,5 gg · dipende da: PRF-11.2, PRF-11.7 · PSP: sì
- [ ] **PRF-11.4** — Verifica su hardware: cucitura, latenza, fast-forward · S, 1,5 gg · dipende da: PRF-11.3, PRF-11.5 · PSP: sì
- [ ] **PRF-11.6** — Campionamento del pad più fitto del frame (con gate) · S, 1 gg · dipende da: PRF-11.5 · PSP: sì
- [ ] **PRF-11.7** — Flip inviato prima del vblank (con gate) · S, 1 gg · dipende da: PRF-11.5 · PSP: sì
- [ ] **PRF-11.8** — Ritmo e flip alla riga 160 (con gate) · L, 4 gg · dipende da: PRF-11.5, PRF-11.7, F-120, ENG-15.4 · PSP: sì · aspetta: ENG-15.4 (`wip/crash-safety`)
- [ ] **PRF-14.1** — Prototipo misurato del renderer sul ME · XL, 8 gg · dipende da: PRF-13.4, PRF-2.2 · PSP: sì · aspetta: PRF-13.4 (`wip/audio`), PRF-2.2 (`wip/perf-method`) · _superata: implementazione già nel main (SOA-6)_
- [ ] **PRF-14.2** — Presentazione nello stesso refresh e ritorno alla CPU per frame · L, 5 gg · dipende da: PRF-14.1, F-126 · PSP: sì · _superata da SOA-6.4_
- [ ] **PRF-14.3** — Opzione del renderer ME · S, 1 gg · dipende da: PRF-14.2 · PSP: no · _superata: opzione Exact già nel main_
- [ ] **PRF-14.4** — Soak, latenza e divergenze su hardware · M, 2,5 gg · dipende da: PRF-14.3, PRF-11.5 · PSP: sì · _superata da SOA-6.6 e SOA-6.7_

## Note dell'ondata del 2026-11-04 (SOA-6.1, 6.3, 6.4, 6.5, 6.6)

Niente di questo è mai girato su una PSP: PPSSPP non esegue il ME. Ogni comportamento nuovo del ME è spento per difetto e si accende con un interruttore, così la sessione hardware può confrontare le varianti; senza interruttori l'EBOOT fa quello che faceva prima, salvo il formato interno del log (versione 2, vedi sotto). Il core Classic non è stato toccato.

**Interruttori (F-143).** Il file `media_engine.cfg` accanto all'EBOOT contiene un solo numero, decimale o esadecimale con `0x`, somma degli interruttori voluti; si legge una volta, quando il ME parte, e il record 14 del runtime log dice quali erano accesi. Definiti in `source/src/video_me.h`:

| Valore | Nome | Ticket | Effetto |
|---|---|---|---|
| 0x01 | `VIDEO_ME_COPY_QUADS` | SOA-6.3 | blocchi sporchi copiati nel ring con `lv.q`/`sv.q` (il thread di emulazione prende l'attributo VFPU con `sceKernelChangeCurrentThreadAttr`) |
| 0x02 | `VIDEO_ME_NO_CACHE_FLUSH` | SOA-6.3 | niente `sceKernelDcacheWritebackAll` per un frame del ME che segue un altro frame del ME; il ring si scrive già per intervalli in `publish()` |
| 0x04 | `VIDEO_ME_QUICK_FALLBACK` | SOA-6.6 | se il ME non ha letto nulla dall'inizio del frame precedente, la CPU disegna finché non ha recuperato (ritorno alla CPU entro un frame) |
| 0x08 | `VIDEO_ME_SLEEP` | SOA-6.1 | dopo circa 25 µs di inattività il ME fa `halt`; la CPU, dopo ogni pubblicazione, se vede `sleeping` chiama `media_engine_wake` del PRX (scrive 1 in 0xBC100044) |
| 0x10 | `VIDEO_ME_DIRECT_1X` | SOA-6.4 | a scala 1x e con Show FPS spento, il ME disegna in tre immagini 480×272 in RAM (816 KiB) che il display mostra con `sceDisplaySetFrameBuf`, senza GE |
| 0x20 | `VIDEO_ME_SLEEP_CLEAR` | SOA-6.1 | con 0x08: dopo ogni risveglio il ME scrive -1 in 0xBC100004, come fa boot.S |

Ordine consigliato per la sessione: 0 (base), poi 0x04, 0x03, 0x07, 0x10, 0x08, infine 0x28; una variante per volta, con i record 9-15 e il perf report di ciascuna.

**Record nuovi del runtime log** (LINK_COUNTERS, accodati per R1, decoder e test aggiornati; i record 9-11 erano pieni e cambiarne il significato chiederebbe un bump): 13 = tempo di lavoro del ME per frame finito, mediana/p99/massimo in µs (istogramma a passi di 2^16 tick, circa 0,39 ms a 333 MHz); 14 = interruttori, risvegli del ME, frame disegnati sulla CPU per il ritorno rapido; 15 = present del GE, µs di GE per present (dalla coda del list al callback di fine con id 0x6E1), frame mostrati direttamente dalla RAM. Se un altro branch accoda gli stessi numeri, chi unisce per secondo li sposta.

**Formato del log versione 2** (interno fra EBOOT e `media_engine.prx`, che escono insieme): i blocchi di un comando DATA partono su 16 byte (0-3 parole inutilizzate dopo l'intestazione); comando TARGET (indice in `targets[4]` della mailbox e stride); campi `wait`, `sleeping` e `frame_time[64]` nella mailbox. `palette_ram`, `oam_ram` e `vram` in `memory.c` sono allineati a 16 byte.

**Ipotesi da verificare su console**
- SOA-6.1: che `halt` del ME si svegli con le linee abilitate in Status (IM2-IM7) e IE a 0, e che la scrittura di 1 in 0xBC100044 dalla CPU principale interrompa il ME. Se il risveglio non arriva, il watchdog (60 frame, o subito con 0x04) torna alla CPU; se il risveglio resta pendente, l'attesa ricade nel polling ogni circa 25 µs. 0x20 resta separato perché 0xBC100004 potrebbe non essere solo del ME.
- SOA-6.1, parte non fatta: la CPU principale continua a sondare `completed` nelle righe di V-Blank (al più 68 letture non cached per frame). Un gestore dell'IRQ 31 nel PRX dovrebbe togliere quello di Sony (`sceKernelReleaseIntrHandler`) e saper azzerare l'interrupt dal lato della CPU principale: con un interrupt non azzerato la console si blocca, quindi va fatto solo dopo aver visto su console come si comporta 0xBC100044.
- SOA-6.3: la copia con `sceDmacMemcpy` da un thread ausiliario non è stata fatta (236 byte/µs, più lenta di una copia della CPU per blocchi da 64 byte); da riprendere solo se `lv.q` non basta.
- SOA-6.4: le strisce erano già di 64 px da una texture in VRAM (≤ 128 px); nuova è la misura del GE (record 15). Lo scan-out diretto richiede che `sceDisplaySetFrameBuf` accetti un indirizzo in RAM principale: se lo rifiuta l'immagine resta quella di prima e lo scan-out non si riprova. In modalità diretta non si vedono l'icona HOLD, il messaggio di sleep del GBA, la barra del volume e lo stato dell'avanti veloce; screenshot e anteprima del menu mostrano l'ultimo frame passato dal GE.
- SOA-6.5: codice (8,7 KB su 12 KB di limite controllato da `media-engine-check`) e dati caldi (lettore e copia della memoria video, 104 KB di .bss) stavano già nell'eDRAM del ME; nuova è la misura per frame (record 13). Se il p99 supera 12 ms, la prima prova è disegnare la riga in un buffer nell'eDRAM e copiarla in RAM, per non far leggere al ME le righe che sovrascrive.
- SOA-6.6: fermo e ripartenza del ME a sospensione e ripresa li faceva già il gestore sysevent del PRX; il tempo di lavoro non mescola più due avvii diversi e `sleeping` si azzera a ogni avvio. Il soak resta tutto da fare.

**Check di questa ondata:** `make check`, `make test-sanitize`, `make psp-build` e `media-engine-check` verdi; check-render non eseguito (serve mGBA 0.10.3 a 16 bit); check-core non necessario (nessun file `arm_*` toccato).
