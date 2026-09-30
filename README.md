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
- **Backup & Recovery**: creazione, anteprima, verifica e ripristino degli snapshot `tar.gz`, più stato e recovery `rk`. Il restore valida in streaming tutti i membri e i tipi prima dell'estrazione.
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

La versione pubblica di krisCC usa esclusivamente `X.Y.Z`. Ogni candidata successiva incrementa `Z`; non usiamo suffissi pubblici come `-2`, `-5` o simili. Il campo RPM `Release` resta fissato a `1` come metadato tecnico del formato RPM e non viene mostrato dall'app né usato nei tag. I tag candidati/stable sono quindi `vX.Y.Z`.

- `0.8` è il ramo di sviluppo/acceptance della linea 0.8, derivato da 0.7.10;
- `0.7` resta il ramo di integrazione/acceptance della linea 0.7;
- `main` è la linea ufficiale corrente e riceve `0.8` solo dopo CI e acceptance test.
- `stable/0.6` è una fotografia congelata della precedente linea 0.6 e punta a `v0.6.0-2`. Non riceve sviluppo ordinario né backport automatici.
- ogni push e pull request verso `main` costruisce e verifica l'RPM in CI senza pubblicarlo automaticamente; un candidato prerelease viene pubblicato solo con dispatch esplicito sul `main` validato;
- la promozione a stable avviene esplicitamente solo dopo l'acceptance test su un host KrisOS reale e riusa esattamente lo stesso RPM già verificato, senza rebuild.

In questo modo la vecchia linea resta recuperabile senza obbligarci a mantenerla in parallelo, mentre `main` rimane l'unico ramo di sviluppo supportato.

## Build locale

Dipendenze Fedora:

```bash
dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel qt6-qtdeclarative-devel kf6-kirigami-devel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/krisCC
```

`--background` resta disponibile soltanto come modalità esplicita di test/futuro uso. KrisOS non deve avviare krisCC automaticamente in sessione: il Control Center è on-demand e, chiusa la finestra, il processo termina. Durante una sessione attiva resta comunque single-instance:

```bash
./build/krisCC --background
```

## RPM e integrazione nell'immagine

Lo spec RPM è `packaging/krisCC.spec` e produce **`krisCC-0.8.0-*.rpm`**. La CI Fedora 45 costruisce l'RPM, lo installa in un ambiente pulito, riesegue lo smoke test e produce `SHA256SUMS` dell'artefatto RPM.

Il flusso previsto per KrisOS è:

```text
krisCC source -> CI/test -> RPM identificato + SHA256 -> build KrisOS -> snapshot owned packages -> immagine BootC
```

L'RPM deve essere installato **prima** della generazione di `/usr/share/krisos/owned-packages.txt` e `owned-nevra.txt`, così krisCC viene riconosciuto come parte della base immutabile e non come pacchetto dell'overlay. La build KrisOS deve consumare un artefatto krisCC verificabile, non ricostruire implicitamente un altro repository durante la build dell'OS.

Esempio manuale:

```bash
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
git archive --format=tar.gz --prefix=krisCC-0.8.0/ \
  -o ~/rpmbuild/SOURCES/krisCC-0.8.0.tar.gz HEAD
cp packaging/krisCC.spec ~/rpmbuild/SPECS/krisCC.spec
rpmbuild -ba ~/rpmbuild/SPECS/krisCC.spec
```

Repository: https://github.com/krism-eu/krisCC

## Test reale

La CI verifica compilazione, caricamento QML/Kirigami, controlli DNF5 locali, spec RPM, installazione di staging e installazione/smoke dell'RPM. Per il pre-gate KrisOS45, durante Fedora 45 Branched `tests/f45-minimal-runtime.sh <rpm>` usa la base bootc Fedora 45 `:latest`, la sincronizza con i repository correnti e rifiuta qualsiasi sostituzione di un RPM già appartenente alla base sincronizzata; il digest verrà fissato alla stable. Prima di considerare una release definitiva vanno comunque provati sulla macchina reale: `rk plan/add/rm/sync`, autenticazione Polkit, ricerca RPM, apertura/gestione Flatpak tramite Discover, applicazione container esterna, Tools & Fix, servizi/rete, `bootc upgrade --check`, download/apply BootC, creazione/verifica/ripristino backup e selezione one-shot UEFI/GRUB quando gli strumenti sono presenti.
