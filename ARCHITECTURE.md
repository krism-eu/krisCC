# krisCC — Contratto architetturale

**Stato:** normativo  
**Obiettivo:** mantenere krisCC snello, integrato con KrisOS e stabile nel tempo, riducendo al minimo la manutenzione richiesta.

krisCC non deve diventare una raccolta di wrapper grafici per comandi di sistema. Deve essere un livello di controllo piccolo e prevedibile sopra contratti KrisOS espliciti e stabili.

## 1. Principio guida

**Integrazione forte, dipendenze deboli.**

La UI conosce il significato dello stato del sistema, non il dettaglio con cui quello stato viene ricavato.

Esempio corretto:

```text
QML -> RkBackend.needsSync
```

Esempio da evitare:

```text
QML -> grep/findmnt/file marker/output testuale
```

Quando cambia un componente sottostante, l'adattamento deve rimanere confinato nel backend responsabile.

## 2. Strati consentiti

```text
QML / Kirigami
      |
      v
Backend krisCC
      |
      +--> API read-only di sistema
      |
      +--> Polkit -> rk oppure /usr/libexec/kriscc/admin con operazioni semantiche allowlisted
      |
      v
KrisOS / rk / bootc / systemd / DNF / filesystem
```

Regole:

- QML non implementa logica di sistema.
- QML non costruisce comandi privilegiati.
- Un backend possiede un solo dominio funzionale.
- Le mutazioni root passano esclusivamente da `rk` oppure dal piccolo helper `/usr/libexec/kriscc/admin`.
- Polkit autorizza l'entry point; la validazione completa degli argomenti avviene anche nel processo privilegiato, non solo nella policy.
- Nessun helper privilegiato accetta shell libera, pipeline o path arbitrari forniti dalla UI.
- Operazioni che non richiedono davvero root, come la pulizia dei cestini dell'utente, restano fuori da Polkit.

## 3. Fonte della verità

krisCC non duplica la logica di KrisOS.

Sono fonti della verità i componenti proprietari del dominio, per esempio:

- `rk` per il layer RPM persistente;
- `bootc` per lo stato e gli aggiornamenti image-based;
- systemd per unità, timer e sessioni;
- DNF5 per informazioni repository;
- Flatpak e Podman per i rispettivi profili utente.

krisCC legge e presenta lo stato; non reimplementa solver, recovery, deployment o policy.

## 4. Contratti stabili

Ogni integrazione deve avere un confine piccolo e documentabile.

Ordine di preferenza:

1. output machine-readable e versionato;
2. API o file di stato con formato stabile;
3. output testuale solo se il contratto è intenzionalmente stabile e il parser è centralizzato e coperto da fixture.

Non è consentito distribuire parsing dello stesso contratto in più moduli o in QML.

Per `rk`, il contratto machine-readable è `rk status --json` con `schema: 1`. Il solo punto autorizzato a interpretarlo è `RkBackend`; Dashboard, Recovery e altre viste consumano esclusivamente proprietà tipizzate. Output testuale e dettagli di implementazione non attraversano il confine del backend.

## 5. Compatibilità e cambiamenti futuri

Una funzionalità non deve presumere che un comando o una capability esista per sempre.

Le letture devono fallire in modo morbido:

- `available=false` o stato equivalente;
- messaggio breve e non tecnico nella UI;
- dettaglio tecnico separato quando utile;
- il resto dell'applicazione continua a funzionare.

Le mutazioni devono fallire in modo chiuso:

- se programma, argomenti o stato non corrispondono all'allowlist, l'operazione non parte;
- nessun fallback più permissivo;
- nessun passaggio automatico a shell generiche.

I fallback legacy sono temporanei, read-only e devono essere rimossi quando termina la migrazione che li giustifica.

## 6. Privilegi

Ogni operazione amministrativa segue:

```text
stato -> conferma -> autorizzazione -> operazione -> verifica -> refresh
```

Un codice di uscita zero non è sufficiente quando esiste uno stato verificabile.

Esempi:

- dopo `rk sync` o `rk forget`, rileggere lo stato RK;
- dopo una manutenzione filesystem, rileggere lo stato pertinente quando disponibile.

Gli helper privilegiati devono:

- fare una sola cosa;
- accettare un insieme chiuso di modalità;
- validare nuovamente gli input;
- non seguire symlink o mount inattesi quando operano sul filesystem;
- essere direttamente testabili fuori dalla UI.

## 7. UI stabile e nativa

krisCC usa Plasma/Kirigami e il tema dell'utente.

Non deve introdurre:

- tema proprietario completo;
- palette hardcoded che sostituiscono i colori semantici;
- componenti grafici custom quando Kirigami offre già l'equivalente;
- stato applicativo codificato solo tramite colore.

La Dashboard mostra eccezioni e salute, non dettagli tecnici rari.

Le funzioni specialistiche rimangono nelle pagine appropriate. krisCC non deve diventare il posto da cui si può fare qualunque cosa.

## 8. Criterio per aggiungere una funzione

Una funzione entra in krisCC solo se sono definiti tutti questi elementi:

1. fonte della verità;
2. stato iniziale leggibile;
3. capability detection;
4. operazione ammessa;
5. comportamento in errore;
6. recovery o comportamento dopo errore;
7. verifica post-operazione quando possibile;
8. test automatici;
9. ownership chiara del backend.

Se uno di questi elementi manca, la funzione non è ancora pronta per il Control Center.

## 9. Comandi diagnostici e azioni personali

La sezione **Predefiniti** della pagina Comandi è una cassetta degli attrezzi read-only per l'uso quotidiano.

I comandi predefiniti devono essere frequenti, utili per diagnosi locale, con argomenti fissi, senza input libero, senza `sudo` e senza shell costruita dall'utente. Le funzioni rare o specifiche di un sottosistema restano nella pagina del sottosistema invece di moltiplicare i bookmark.

La sezione **Miei comandi** è separata dal contratto amministrativo: conserva in `~/.config/krisCC/custom-actions.json` comandi o script Bash scelti esplicitamente dall'utente. Queste azioni:

- non passano da Polkit;
- non ricevono elevazione automatica;
- non vengono eseguite quando krisCC gira come root;
- operano dalla home con l'ambiente e i privilegi dell'utente;
- hanno configurazione versionata, fail-closed, privata (`0600`) e con scrittura atomica;
- usano `/usr/bin/bash` a percorso fisso, output limitato e un process group dedicato, così annullamento e timeout terminano anche i processi figli.

Un'azione personale che diventa una funzione amministrativa stabile deve essere promossa a backend ufficiale con capability detection, allowlist e test; non va resa privilegiata dentro il meccanismo custom.

## 10. Test come contratto di compatibilità

La CI deve proteggere sia il comportamento sia i confini architetturali.

Per ogni contratto strutturato sono richiesti, dove applicabile:

- fixture valide;
- fixture incomplete o future/non riconosciute;
- test di capability assente;
- test degli stati degradati;
- test positivi e negativi delle allowlist;
- build strict;
- QML lint;
- smoke test runtime;
- build e installazione dell'RPM;
- smoke test dell'RPM installato.

Una modifica che richiede di allentare l'allowlist o spostare parsing nel QML deve essere considerata una regressione architetturale salvo motivazione esplicita.

## 11. Politica di manutenzione

L'obiettivo non è supportare indefinitamente ogni implementazione storica.

Preferenze:

- meno compatibilità implicita;
- più capability detection;
- pochi adattatori centrali;
- rimozione dei fallback conclusa la migrazione;
- contratti versionati invece di euristiche.

Il codice migliore per krisCC è quello che continuerà a funzionare anche quando cambiano dettagli interni di KrisOS, perché quei dettagli non attraversano il confine del backend.

## 12. Definition of Done

Una modifica strutturale è pronta solo quando:

- il QML non conosce dettagli inutili dell'implementazione;
- il backend espone stato tipizzato;
- le operazioni privilegiate sono allowlisted in modo stretto;
- gli errori di lettura non rompono l'app;
- gli errori di mutazione non degradano verso comportamenti più permissivi;
- la CI protegge il nuovo contratto;
- l'RPM installato passa lo smoke test;
- la modifica non introduce una nuova dipendenza di manutenzione evitabile.

Queste regole hanno precedenza sulla comodità di implementare rapidamente una nuova funzione.


## 13. Ciclo di release

La versione pubblica segue esclusivamente `X.Y.Z`. Ogni nuova candidata cambia `Z`; il campo RPM `Release` resta `1` e non fa parte della versione mostrata, del tag o del nome della linea. I tag sono `vX.Y.Z`.

Il ramo di integrazione corrente usa il nome breve `trial/X.Y` (per esempio `trial/0.7`). `main` resta la linea ufficiale dopo acceptance. I rami `stable/X.Y` sono fotografie congelate di una linea precedente, non rami di manutenzione continua.

Una release segue questo percorso:

```text
main -> CI completa -> RPM candidato immutabile -> acceptance host KrisOS -> promozione stable dello stesso artefatto
```

La promozione stable non deve ricompilare il pacchetto. Deve verificare checksum, identità EVR e appartenenza del commit a `main`, quindi cambiare soltanto lo stato della release già validata.

Una vecchia stable resta disponibile tramite tag, release e ramo congelato. Non si introducono fallback nel codice corrente solo per mantenerla compatibile.