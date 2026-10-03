# krisCC

krisCC è un **Control Center personale Kirigami per KrisOS / Fedora bootc**. Non vuole sostituire Plasma System Settings: rete, utenti, firewall, display, audio e preferenze desktop restano agli strumenti KDE già presenti. L'interfaccia resta volutamente nello stile Breeze/Plasma, usando componenti Kirigami e icone KDE senza introdurre un tema proprietario.

## Architettura

Le regole di stabilità, compatibilità e integrazione sono definite in [ARCHITECTURE.md](ARCHITECTURE.md). Il documento è normativo per le nuove funzionalità: krisCC deve restare piccolo, capability-driven e con logica di sistema confinata nei backend.

## Cosa gestisce

La navigazione principale ha sette sezioni operative:

- **Dashboard**: stato essenziale di sistema, aggiornamenti, storage, CPU, RAM, temperatura, servizi chiave e Quick System Info.
- **Software RPM**: ricerca, installati, aggiornabili, provenienza Base/Persistente/Layer non richiesti e piano delle transazioni tramite `rk`.
- **Servizi & Rete**: servizi system/user, unità fallite, journal minimale filtrabile, controlli dei servizi comuni, NetworkManager, reconnect, flush DNS e identità Internet su richiesta.
- **Sistema & Boot**: BootC/rk, stato Control Center, cronologia locale, reboot firmware, kernel arguments, gestione UEFI NVRAM/BootOrder, GRUB/BLS e viste storage.
- **Backup & Recovery**: launcher per Back In Time per i backup personali, più stato e recovery `rk` di KrisOS.
- **Comandi**: bookmark diagnostici read-only, azioni rapide, script Bash personali rootless e vista Cron in sola lettura.
- **Strumenti & Fix**: riparazione audio, pulizia, diagnostica, support report e collegamenti agli strumenti esterni.

Flatpak resta delegato a **KDE Discover** e la gestione container a un'applicazione dedicata: non sono sezioni proprietarie di krisCC. Il registro **Cronologia** è una funzione interna e limitata, non una pagina top-level.

## Sicurezza

Le mutazioni privilegiate usano il proprietario naturale del dominio quando esiste un'API stabile: systemd, logind e NetworkManager vengono chiamati tramite D-Bus con autorizzazione Polkit e allowlist locale. `rk sync/add/rm/forget` resta il contratto privilegiato proprietario di KrisOS e il gate finale della policy RPM. BootC, repository e operazioni UEFI/GRUB che non hanno un'API proprietaria adeguata passano da `/usr/libexec/kriscc/admin`: Polkit autorizza l'helper e l'helper root valida l'operazione semantica e **tutti** gli argomenti prima di eseguire un binario a percorso fisso, senza shell libera. La policy usa `auth_admin` senza retention (`auth_admin_keep` è vietato).

La pulizia dei cestini non è privilegiata: l'helper `maintenance` gira come utente e rifiuta esplicitamente l'esecuzione come root. La gestione repository accetta soltanto URL HTTPS validati e ID validi. L'anteprima e ogni installazione/rimozione RPM persistente continuano a passare da `rk plan/add/rm`; rk resta il gate finale della policy KrisOS. I comandi personali restano rootless nel profilo utente; Flatpak e container sono gestiti da applicazioni dedicate.

## Compatibilità KrisOS

krisCC usa in via primaria il layout corrente di KrisOS:

- `/var/lib/krisos/packages.list`
- `/usr/share/krisos/owned-packages.txt`
- `/usr/bin/rk`

Il pacchetto RPM e l'eseguibile hanno una sola identità tecnica: **`krisCC`**. Non sono previsti alias, binari o compatibilità RPM con i vecchi nomi sperimentali.

krisCC è parte della base immutabile di KrisOS: le release normali del control center arrivano insieme a una nuova immagine KrisOS, non tramite `rk` o un aggiornamento RPM separato sul sistema installato.

## Release e rami

La versione pubblica di krisCC usa esclusivamente `X.Y.Z`. Il campo RPM `Release` resta fissato a `1`: durante un ciclo di test non si inventano versioni intermedie e il numero `X.Y.Z` è già quello della prossima stable.

- `main` è la linea ufficiale corrente;
- push e pull request verso `main` eseguono gli stessi gate di build, test, audit e RPM smoke;
- dopo un **push su `main` completamente verde**, la release GitHub fissa `testing` viene aggiornata automaticamente con soli tre file: RPM, source TXT dell'esatto commit e `SHA256SUMS`;
- il titolo e le note di `testing` riportano il commit esatto da provare sul KrisOS reale;
- i normali artefatti Actions conservano per 30 giorni lo stesso piccolo bundle, così una successiva promozione può recuperare l'RPM esatto anche se nel frattempo `testing` è avanzata;
- `Promote stable release` richiede il commit che è stato realmente provato e la conferma dell'acceptance test: recupera l'artefatto della build verde di quel commit, ne ricontrolla SHA256, metadati RPM e source TXT e crea `vX.Y.Z` **senza rebuild**;
- se una stable `vX.Y.Z` esiste già, il workflow accetta soltanto il caso idempotente in cui commit e artefatti coincidono esattamente; non sovrascrive una stable divergente.

Il bump `X.Y.Z` si fa quindi una sola volta all'inizio del ciclo destinato alla prossima stable. Le correzioni successive possono aggiornare `testing` senza ulteriori bump finché quella versione non viene promossa.

## Build locale

Dipendenze Fedora:

```bash
dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kirigami-devel kf6-kidletime-devel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/krisCC
```

`--background` resta disponibile soltanto come modalità esplicita di test/futuro uso. KrisOS non deve avviare krisCC automaticamente in sessione: il Control Center è on-demand e, chiusa la finestra, il processo termina. Durante una sessione attiva resta comunque single-instance:

```bash
./build/krisCC --background
```

## RPM e integrazione nell'immagine

Lo spec RPM è `packaging/krisCC.spec` e produce **`krisCC-0.8.2-*.rpm`**. La CI Fedora 45 costruisce il solo RPM binario, genera il source TXT dell'esatto commit, verifica l'RPM in un ambiente pulito e crea un `SHA256SUMS` a due voci che copre entrambi. Il bundle Actions usato tra i job contiene solo questi tre file: non include più SRPM, stage tar.gz o copie dei log.

Il flusso previsto per KrisOS è:

```text
krisCC source -> CI/test -> RPM identificato + SHA256 -> build KrisOS -> snapshot owned packages -> immagine BootC
```

L'RPM deve essere installato **prima** della generazione di `/usr/share/krisos/owned-packages.txt` e `owned-nevra.txt`, così krisCC viene riconosciuto come parte della base immutabile e non come pacchetto dell'overlay. La build KrisOS deve consumare un artefatto krisCC verificabile, non ricostruire implicitamente un altro repository durante la build dell'OS.

Esempio manuale:

```bash
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
git archive --format=tar.gz --prefix=krisCC-0.8.2/ \
  -o ~/rpmbuild/SOURCES/krisCC-0.8.2.tar.gz HEAD
cp packaging/krisCC.spec ~/rpmbuild/SPECS/krisCC.spec
rpmbuild -bb ~/rpmbuild/SPECS/krisCC.spec
```

Repository: https://github.com/krism-eu/krisCC

## Test reale

La CI verifica compilazione, caricamento QML/Kirigami, controlli DNF5 locali, spec RPM, installazione di staging e installazione/smoke dell'RPM. Per il pre-gate KrisOS45, durante Fedora 45 Branched `tests/f45-minimal-runtime.sh <rpm>` usa la base bootc Fedora 45 `:latest`, la sincronizza con i repository correnti e rifiuta qualsiasi sostituzione di un RPM già appartenente alla base sincronizzata; il digest verrà fissato alla stable. Prima di considerare una release definitiva vanno comunque provati sulla macchina reale: `rk plan/add/rm/sync`, autenticazione Polkit, ricerca RPM, apertura/gestione Flatpak tramite Discover, applicazione container esterna, Tools & Fix, servizi/rete, `bootc upgrade --check`, download/apply BootC, apertura di Back In Time e selezione one-shot UEFI/GRUB quando gli strumenti sono presenti.
