# krisCC

krisCC è un **Control Center personale Kirigami per KrisOS / Fedora bootc**. Non vuole sostituire Plasma System Settings: rete, utenti, firewall, display, audio e preferenze desktop restano agli strumenti KDE già presenti. L'interfaccia resta volutamente nello stile Breeze/Plasma, usando componenti Kirigami e icone KDE senza introdurre un tema proprietario.

## Architettura

Le regole di stabilità, compatibilità e integrazione sono definite in [ARCHITECTURE.md](ARCHITECTURE.md). Il documento è normativo per le nuove funzionalità: krisCC deve restare piccolo, capability-driven e con logica di sistema confinata nei backend.

## Cosa gestisce

- **Panoramica**: stato essenziale di sistema, aggiornamenti, storage, CPU, RAM usata senza swap, temperatura CPU e Quick System Info.
- **Software RPM**: ricerca, installati, aggiornabili, provenienza Base/Persistente/Layer non richiesti e piano della transazione tramite la stessa policy `rk` usata per installare.
- **Flatpak**: ricerca strutturata, installati, aggiornamenti, update singolo o completo del profilo utente, remote e integrazione Flathub senza dipendere da Discover.
- **Container / Podman**: elenco container e immagini locali, stato, nome/tag, dimensione, informazioni, log, start/stop/restart, rinomina, rimozione dei container e rimozione esplicita delle immagini senza force.
- **Sistema**: centro aggiornamenti BootC/Flatpak/rk, salute e sicurezza read-only, storage, voci UEFI e GRUB/BLS, selezione one-shot del prossimo avvio e strumenti KDE essenziali.
- **Comandi**: diagnostica read-only pronta per systemd, journal, rete, spazio, inode, mount e avvio, più **Miei comandi** per salvare comandi o script Bash multilinea personali in `~/.config/krisCC/custom-actions.json`. Il file è privato (`0600`), versionato e fail-closed; le azioni girano soltanto con i privilegi dell'utente corrente, con stdin chiuso, e Annulla/timeout termina l'intero gruppo di processi dello script.
- **Backup e recovery**: creazione, anteprima precisa di inclusioni/esclusioni, elenco, verifica e ripristino degli snapshot `tar.gz`, più stato RK strutturato, sync e forget di recovery. Il backup home esclude runtime/app Flatpak e storage Podman ricostruibili, mantenendo i dati Flatpak in `~/.var/app`.
- **Cronologia**: registro locale privato e limitato delle operazioni mutanti eseguite da krisCC. Non vengono salvati output completi né argomenti sensibili delle operazioni amministrative.

## Sicurezza

Le mutazioni KrisOS passano da pochi confini espliciti. `rk sync/add/rm/forget` resta il contratto privilegiato proprietario di KrisOS e il gate finale della policy RPM. Le invocazioni provenienti da krisCC, insieme a BootC, repository e selezione one-shot del prossimo boot, passano da `/usr/libexec/kriscc/admin`: Polkit autorizza l'helper e l'helper root valida l'operazione semantica e **tutti** gli argomenti prima di eseguire un binario a percorso fisso, senza shell. La policy usa `auth_admin` senza retention (`auth_admin_keep` è vietato).

La pulizia dei cestini non è privilegiata: l'helper `maintenance` gira come utente e rifiuta esplicitamente l'esecuzione come root. La gestione repository accetta soltanto URL HTTPS validati e ID validi. L'anteprima e ogni installazione/rimozione RPM persistente continuano a passare da `rk plan/add/rm`; rk resta il gate finale della policy KrisOS. Flatpak, Podman e comandi personali restano rootless nel profilo utente.

## Compatibilità KrisOS

krisCC usa in via primaria il layout corrente di KrisOS:

- `/var/lib/krisos/packages.list`
- `/usr/share/krisos/owned-packages.txt`
- `/usr/bin/rk`

Il pacchetto RPM e l'eseguibile hanno una sola identità tecnica: **`krisCC`**. Non sono previsti alias, binari o compatibilità RPM con i vecchi nomi sperimentali.

krisCC è parte della base immutabile di KrisOS: le release normali del control center arrivano insieme a una nuova immagine KrisOS, non tramite `rk` o un aggiornamento RPM separato sul sistema installato.

## Release e rami

Le versioni visibili usano solo **X.Y.Z**. La linea corrente è **dev/0.8**; le correzioni avanzano il patch (`0.8.1`, `0.8.2`, …). Il campo RPM `Release: 1` resta solo metadata Fedora e non compare nella versione mostrata dall'app o nel tag GitHub.

- `main` è la linea ufficiale corrente; dalla 0.7 contiene l'architettura strutturata di krisCC.
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

Lo spec RPM è `packaging/krisCC.spec` e produce **`krisCC-0.8.0-*.rpm`**. La CI Fedora 44 costruisce l'RPM, lo installa in un ambiente pulito, riesegue lo smoke test e produce `SHA256SUMS` dell'artefatto RPM.

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

La CI verifica compilazione, caricamento QML/Kirigami, controlli DNF5 locali, spec RPM, installazione di staging e installazione/smoke dell'RPM. Prima di considerare una release definitiva vanno comunque provati sulla macchina reale: `rk plan/add/rm/sync`, autenticazione Polkit, ricerca RPM, aggiornamenti Flatpak, Podman, `bootc upgrade --check`, download/apply BootC, creazione/verifica/ripristino backup e selezione one-shot UEFI/GRUB quando gli strumenti sono presenti.