# wip/game-compat — Tabella dei giochi, RTC e titoli difficili

- **Branch:** `features/state-of-the-art-development/wip/game-compat`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** CMP-2, CMP-7, CMP-8, CMP-9, CMP-10 (20 storie, circa 28,75 gg).
- **File principali:** `game_db.c` e `gba_rtc.c` (nuovi), `game_config.txt` e RTC in `memory.c`.
- **Dipende da:** `wip/open-bios` (CMP-1.4), `wip/replay-oracle`, `wip/perf-method`.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.5

- [ ] **CMP-9.1** — Sessione di misura sui 13 titoli difficili sulla 1.4.3 · S, 1,5 gg · dipende da: PRF-8.6 · PSP: sì · aspetta: PRF-8.6 (`wip/perf-method`)
- [ ] **CMP-10.1** — Verifica dei titoli GBP senza handshake e dell'input · S, 0,5 gg · dipende da: — · PSP: sì · **subito**

## 1.6

- [ ] **CMP-2.1** — Modulo game_db e tabella libretro a commit fisso · M, 1,5 gg · dipende da: — · PSP: no · **subito**
- [ ] **CMP-2.2** — Applicare il profilo al caricamento, con game_config.txt che vince · M, 2 gg · dipende da: CMP-2.1 · PSP: sì
- [ ] **CMP-2.3** — Guardia di plausibilità per gli idle loop della tabella · S, 1 gg · dipende da: CMP-2.2 · PSP: sì
- [ ] **CMP-2.4** — Idle loop della tabella attivi solo se verificati · M, 3,5 gg · dipende da: CMP-2.3, F-120, PRF-1.1, PRF-1.5, F-137 · PSP: sì · aspetta: PRF-1.1 (`wip/perf-method`), PRF-1.5 (`wip/perf-method`)
- [ ] **CMP-2.5** — Profilo attivo nella pagina game_config e nel runtime log · S, 0,5 gg · dipende da: CMP-2.2, CMP-1.4, ENG-1.6 · PSP: no · aspetta: CMP-1.4 (`wip/open-bios`), ENG-1.6 (`wip/release-and-ci`)
- [ ] **CMP-7.1** — Modulo gba_rtc puro con test di protocollo · M, 2 gg · dipende da: — · PSP: no · **subito**
- [ ] **CMP-7.2** — memory.c delega a gba_rtc senza cambiare il formato dei savestate · S, 1 gg · dipende da: CMP-7.1 · PSP: sì
- [ ] **CMP-7.3** — Sorgente dell'orologio: a muro (default) o a frame guest, con guardia · S, 0,5 gg · dipende da: CMP-7.1 · PSP: no
- [ ] **CMP-8.1** — Corpus di hack misurato su 1.4.2 (gate) · S, 1 gg · dipende da: F-118 · PSP: sì
- [ ] **CMP-8.4** — Header non valido con "Boot from BIOS": partire direttamente · S, 0,5 gg · dipende da: CMP-2.1, CMP-1.4 · PSP: sì · aspetta: CMP-1.4 (`wip/open-bios`)
- [ ] **CMP-9.4** — Elenco pubblicato: almeno 40 titoli al gameplay e al salvataggio su due modelli · M, 3,25 gg · dipende da: CMP-9.1, CMP-11.3, CMP-11.4, CMP-12.2, CMP-12.3, CMP-12.4, CMP-12.5, ENG-16.3, PRF-3.1 · PSP: sì · aspetta: CMP-11.3 (`wip/classic-core`), CMP-11.4 (`wip/classic-core`), CMP-12.2 (`wip/event-scheduler`), CMP-12.3 (`wip/classic-core`), CMP-12.4 (`wip/classic-core`), CMP-12.5 (`wip/classic-core`), ENG-16.3 (`wip/docs`), PRF-3.1 (`wip/perf-method`)

## dopo gate

- [ ] **CMP-7.4** — (condizionato) Ora impostata dal gioco conservata per gioco · M, 1,5 gg · dipende da: CMP-7.2, F-007 · PSP: sì
- [ ] **CMP-8.2** — Righe per hack noti con chiave CRC · S, 1 gg · dipende da: CMP-8.1, CMP-2.2 · PSP: sì
- [ ] **CMP-8.3** — Ripiego stretto per ROM sconosciute oltre 16 MiB con motore Pokemon · S, 1 gg · dipende da: CMP-8.1, CMP-2.1, CMP-1.4, F-002 · PSP: sì · aspetta: CMP-1.4 (`wip/open-bios`)
- [ ] **CMP-9.2** — (condizionato) Driver 2 Advance a velocità piena con i gate di F-031 · S, 0,5 gg · dipende da: CMP-9.1, F-031, PRF-3.1 · PSP: sì · aspetta: PRF-3.1 (`wip/perf-method`)
- [ ] **CMP-9.3** — (condizionato) Difetti grafici dei titoli difficili sul renderer per scanline, se si riproducono · M, 2 gg · dipende da: CMP-9.1, F-126 · PSP: sì
- [ ] **CMP-9.5** — (condizionato) Correzione generale per un titolo difficile rimasto · M, 2 gg · dipende da: CMP-9.4, F-120 · PSP: sì
- [ ] **CMP-10.2** — (solo se la 10.1 fallisce) Porting di gbp.c come dispositivo SIO · M, 2 gg · dipende da: CMP-10.1, F-007 · PSP: sì
