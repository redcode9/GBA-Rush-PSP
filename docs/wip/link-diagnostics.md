# wip/link-diagnostics — Link: verifica, motivi dei fallimenti, integrità, stato, audio

- **Branch:** `features/state-of-the-art-development/wip/link-diagnostics`, staccato da `features/state-of-the-art-development/main`.
- **Ticket:** ENG-10.1, ENG-10.2, LNK-1, LNK-2, LNK-3, LNK-4, LNK-6 (21 storie, circa 28,5 gg).
- **File principali:** `source/src/link_*.c`, `psp_adhoc.c`, `tools/gen3_*.py`.
- **Dipende da:** nessun altro branch.
- **Nota:** LNK-1.1 ed ENG-10.1 servono alla 1.4.4.
- **Regole:** quelle di «Branch di lavoro dell'epica» in `docs/BACKLOG.md`. Il testo completo di ogni ticket (cosa, file, test, accettazione, licenza) è in `docs/BACKLOG.md` sotto il suo ID. Si spunta un ticket quando la sua accettazione è verificata; questo file si cancella nella PR che chiude il branch.
- **Legenda:** «subito» = nessuna dipendenza, si può iniziare oggi; «aspetta» = dipendenze in altri branch, si parte quando sono in main.

## 1.4.4

- [ ] **ENG-10.1** — tools/gen3_sav_check.py: validatore di salvataggi Gen-3 · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-1.1** — Sessione accoppiata sulla 1.4.3 pubblicata e coppia mista · M, 2,25 gg · dipende da: — · PSP: sì · **subito**

## 1.5

- [ ] **LNK-1.2** — Istogrammi del link e fps nel log di runtime v6 · S, 1,5 gg · dipende da: ENG-1.6, CMP-1.4 · PSP: no · aspetta: ENG-1.6 (`wip/release-and-ci`), CMP-1.4 (`wip/open-bios`)
- [ ] **LNK-1.3** — Misura dei giochi in lockstep su due PSP · S, 1,5 gg · dipende da: LNK-1.2, CMP-1.1 · PSP: sì · aspetta: CMP-1.1 (`wip/open-bios`)
- [ ] **LNK-2.1** — Prefisso congelato e rilevamento del formato di rete · S, 1,5 gg · dipende da: LNK-2.2 · PSP: no
- [ ] **LNK-2.2** — HELLO esteso: versione del partner e capacità · S, 2 gg · dipende da: LNK-1.1, LNK-1.2 · PSP: no
- [ ] **LNK-2.3** — Limite alla ricerca del joiner, con motivo · S, 1 gg · dipende da: LNK-1.2 · PSP: sì
- [ ] **LNK-2.4** — Canale ad hoc nel log · S, 1 gg · dipende da: LNK-1.2 · PSP: no
- [ ] **LNK-2.5** — Verifica del consiglio sul canale e tabella di troubleshooting · S, 1,5 gg · dipende da: LNK-1.1, LNK-2.2, LNK-2.3, LNK-2.4 · PSP: sì
- [ ] **LNK-3.1** — Cheat in pausa durante la sessione · S, 1 gg · dipende da: — · PSP: sì · **subito**
- [ ] **LNK-3.2** — Motivi dei blocchi come toast, non come dialoghi · S, 1,5 gg · dipende da: LNK-3.1, F-045 · PSP: sì
- [ ] **LNK-3.3** — Conferma prima di staccare mentre il gioco usa il link · S, 0,5 gg · dipende da: — · PSP: sì · **subito**
- [ ] **LNK-3.4** — Impronta delle coppie del relay, in sola registrazione · M, 2 gg · dipende da: LNK-1.1, LNK-1.2, LNK-2.2 · PSP: no

## 1.6

- [ ] **ENG-10.2** — Strumento Gen-3 per cinque giochi, con clonazione d'identità · M, 1,5 gg · dipende da: ENG-10.1 · PSP: no
- [ ] **LNK-4.1** — Letterale della tecnica nelle tabelle MSG · S, 0,75 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-4.2** — Modulo puro dello stato link · S, 1 gg · dipende da: LNK-4.1 · PSP: no
- [ ] **LNK-4.3** — Indicatore e toast di sessione · S, 1,5 gg · dipende da: LNK-4.2, F-045 · PSP: sì

## 2.0

- [ ] **LNK-6.1** — Rampe di sfumatura nel modulo puro · S, 1 gg · dipende da: — · PSP: no · **subito**
- [ ] **LNK-6.2** — Niente coda buttata nelle attese: l'audio scorre e sfuma · M, 2 gg · dipende da: LNK-6.1, LNK-1.1, LNK-1.2, ENG-1.6 · PSP: sì · aspetta: ENG-1.6 (`wip/release-and-ci`)

## dopo gate

- [ ] **LNK-3.5** — Chiudere il link su un'impronta discordante · S, 1 gg · dipende da: LNK-3.4, F-005, F-045 · PSP: sì
- [ ] **LNK-6.3** — Cuscinetto più grande, dimensionato dai dati · S, 1,5 gg · dipende da: LNK-6.2, LNK-1.3 · PSP: sì
