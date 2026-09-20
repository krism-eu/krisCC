# krisCC

krisCC è un **Control Center personale Kirigami per KrisOS / Fedora bootc**. Non vuole sostituire Plasma System Settings: rete, utenti, firewall, display, audio e preferenze desktop restano agli strumenti KDE già presenti. L'interfaccia resta volutamente nello stile Breeze/Plasma, usando componenti Kirigami e icone KDE senza introdurre un tema proprietario.

## Architettura

Le regole di stabilità, compatibilità e integrazione sono definite in [ARCHITECTURE.md](ARCHITECTURE.md). Il documento è normativo per le nuove funzionalità: krisCC deve restare piccolo, capability-driven e con logica di sistema confinata nei backend.

## Cosa gestisce

- **Panoramica**: stato essenziale di sistema, aggiornamenti, storage e Quick System Info.
- **Software RPM**: ricerca, installati, aggiornabili, pacchetti recenti, provenienza Base/Persistente/Locale e piano della transazione tramite la stessa policy `rk` usata per installare.
- **Flatpak**: ricerca strutturata, installati, aggiornamenti, update singolo o completo del profilo utente, remote e integrazione Flathub senza dipendere da Discover.
- **Container / Podman**: elenco container e immagini locali, stato, nome/tag, dimensione, informazioni, log, start/stop/restart, rinomina e rimozione esplicita delle immagini senza force.
- **Sistema**: centro aggiornamenti BootC/Flatpak/rk, salute e sicurezza read-only, storage, voci UEFI e GRUB/BLS, selezione one-shot del prossimo avvio e strumenti KDE essenziali.
- **Comandi**: bookmark read-only per attività quotidiane: systemd, journal, rete, spazio, inode, mount, partizioni e processi, con comando visibile/copiabile e filtro locale.
- **Backup e recovery**: creazione, anteprima precisa di inclusioni/esclusioni, elenco, verifica e ripristino degli snapshot `tar.gz`, più stato RK strutturato, sync e forget di recovery. Il backup home esclude runtime/app Flatpak e storage Podman ricostruibili, mantenendo i dati Flatpak in `~/.var/app`.
- **Cronologia**: registro locale delle operazioni mutanti eseguite da krisCC. Non vengono salvati output completi dei comandi.

## Sicurezza

Le modifiche privilegiate passano da `pkexec` con una allowlist C++ stretta. La policy non usa `auth_admin_keep`. Sono ammesse soltanto le combinazioni previste per `rk`, `bootc`, `dnf5 config-manager`, la manutenzione cestini a scope fisso e la selezione one-shot del prossimo boot. La gestione repository accetta solo add da URL HTTPS validato e enable/disable di ID validi; krisCC non esegue shell root generiche.

Le query DNF5 leggono i repository attualmente abilitati nel sistema. L'aggiunta usa esclusivamente URL HTTPS e l'abilitazione/disabilitazione passa dal plugin `dnf5 config-manager`; l'anteprima e ogni installazione/rimozione RPM persistente continuano invece a passare da `rk plan/add/rm`. rk usa i repository abilitati dall'amministratore, forza la verifica delle firme RPM e resta il gate finale della policy KrisOS. Le operazioni Flatpak e Podman restano rootless nel profilo utente.

## Compatibilità KrisOS

krisCC usa in via primaria il layout corrente di KrisOS:

- `/var/lib/krisos/packages.list`
- `/usr/share/krisos/owned-packages.txt`
- `/usr/bin/rk`

Per la fase di migrazione mantiene un fallback in sola lettura verso i vecchi percorsi `/var/lib/raku-kris` e `/usr/share/raku-kris`. Il layout KrisOS ha sempre precedenza.

Il pacchetto RPM e l'eseguibile hanno una sola identità tecnica: **`krisCC`**. Non sono previsti alias, binari o compatibilità RPM con i vecchi nomi sperimentali.

krisCC è parte della base immutabile di KrisOS: le release normali del control center arrivano insieme a una nuova immagine KrisOS, non tramite `rk` o un aggiornamento RPM separato sul sistema installato.

## Release e rami

- `main` è la linea ufficiale corrente; dalla 0.7 contiene l'architettura strutturata di krisCC.
- `stable/0.6` è una fotografia congelata della precedente linea 0.6 e punta a `v0.6.0-2`. Non riceve sviluppo ordinario né backport automatici.
- ogni push su `main` costruisce e verifica l'RPM in CI, quindi pubblica un candidato prerelease immutabile;
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

È disponibile anche `--background` per l'avvio di sessione senza mostrare la finestra principale. L'istanza registra un servizio D-Bus di sessione; un successivo avvio dal menu riattiva la stessa finestra invece di creare un secondo processo:

```bash
./build/krisCC --background
```

## RPM e integrazione nell'immagine

Lo spec RPM è `packaging/krisCC.spec` e produce **`krisCC-0.7.0-*.rpm`**. La CI Fedora 44 costruisce l'RPM, lo installa in un ambiente pulito, riesegue lo smoke test e produce `SHA256SUMS` dell'artefatto RPM.

Il flusso previsto per KrisOS è:

```text
krisCC source -> CI/test -> RPM identificato + SHA256 -> build KrisOS -> snapshot owned packages -> immagine BootC
```

L'RPM deve essere installato **prima** della generazione di `/usr/share/krisos/owned-packages.txt` e `owned-nevra.txt`, così krisCC viene riconosciuto come parte della base immutabile e non come pacchetto dell'overlay. La build KrisOS deve consumare un artefatto krisCC verificabile, non ricostruire implicitamente un altro repository durante la build dell'OS.

Esempio manuale:

```bash
mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
git archive --format=tar.gz --prefix=krisCC-0.7.0/ \
  -o ~/rpmbuild/SOURCES/krisCC-0.7.0.tar.gz HEAD
cp packaging/krisCC.spec ~/rpmbuild/SPECS/krisCC.spec
rpmbuild -ba ~/rpmbuild/SPECS/krisCC.spec
```

Repository: https://github.com/krism-eu/krisCC

## Test reale

La CI verifica compilazione, caricamento QML/Kirigami, controlli DNF5 locali, spec RPM, installazione di staging e installazione/smoke dell'RPM. Prima di considerare una release definitiva vanno comunque provati sulla macchina reale: `rk plan/add/rm/sync`, autenticazione Polkit, ricerca RPM, aggiornamenti Flatpak, Podman, `bootc upgrade --check`, download/apply BootC, creazione/verifica/ripristino backup e selezione one-shot UEFI/GRUB quando gli strumenti sono presenti.
