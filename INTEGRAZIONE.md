# Integrazione krisCC in KrisOS / Fedora bootc

krisCC 0.7.7 è un'applicazione standalone Qt 6/Kirigami pensata per uso personale su Fedora bootc. Non duplica Plasma System Settings: integra solo le funzioni specifiche del sistema e gli strumenti di manutenzione che è utile avere in un unico posto.

## Runtime

- Qt 6 Core/Gui/Qml/Quick/DBus
- KF6 Kirigami
- `bootc`, `rpm`, `dnf5`, `dnf5-plugins` (config-manager), `pkexec`, `tar`
- `/usr/bin/rk` come helper del layer persistente KrisOS
- systemd/logind per sessione e restart servizi

Info Center, Discover, Cockpit, Partition Manager, KSystemLog, System Monitor, Konsole, `vainfo`, `efibootmgr`, `grubby` e `grub2-reboot` sono integrazioni opzionali. Flatpak e container non sono gestiti direttamente da krisCC; il Control Center delega ai rispettivi strumenti dedicati.

## Compatibilità dati KrisOS

krisCC usa come layout primario quello attuale di KrisOS:

- `/var/lib/krisos/packages.list`
- `/usr/share/krisos/owned-packages.txt`

`/usr/bin/rk` non viene rinominato: è ancora il nome dell'helper nel repository KrisOS corrente e cambiarlo unilateralmente romperebbe le operazioni persistenti e la policy Polkit.

## Modello software

La base del sistema resta image-based e si aggiorna esclusivamente tramite BootC. KrisOS supporta un solo deployment operativo; krisCC non espone rollback o gestione di deployment alternativi.

krisCC usa DNF5 per catalogo, inventario, aggiornamenti disponibili, pacchetti recenti e stato dei repository abilitati. La gestione esplicita dei repository usa soltanto `dnf5 config-manager`: add da URL HTTPS validato e enable/disable di un ID validato. L'installazione/rimozione del layer persistente passa sempre da `rk`. rk usa i repository DNF che l'amministratore ha lasciato abilitati, forza `pkg_gpgcheck` e verifica le firme della transazione prima di applicarla; la base immutabile e le architetture vietate restano protette.

L'anteprima deve usare `rk plan <pacchetto>` e non un comando DNF5 parallelo: il piano mostrato all'utente deve essere prodotto dallo stesso solver, dalle stesse esclusioni e dalla stessa policy che verranno applicati da `rk add`.

## Contratto RK

krisCC legge lo stato del layer persistente esclusivamente tramite `rk status --json` con schema versione 1. Il formato umano resta per il terminale, ma non viene parsato dalla UI.

## Privilegi

Non aggiungere wrapper shell generici. `rk sync/add/rm/forget` resta il gate privilegiato autonomo di KrisOS. Le mutazioni richieste dalla UI, incluse quelle rk, passano da `/usr/libexec/kriscc/admin`, che supervisiona il comando root con timeout reale e poi invoca `/usr/bin/rk` senza duplicarne la policy: `PolkitHelper` accetta soltanto operazioni semantiche enumerate, Polkit autorizza il percorso dell'helper con `auth_admin` senza retention e l'helper root rivalida operazione e argomenti completi, chiude stdin e avvia solo `rk`, `bootc`, `dnf5`, `efibootmgr` o `grub2-reboot` con argv fissi e timeout root-owned. Nessuna shell root è ammessa. La pulizia cestini è invece intenzionalmente user-level e l'helper rifiuta l'esecuzione come root. Il rollback BootC non è esposto.

## Pipeline immagine

Il repository produce esclusivamente l'RPM `krisCC`. Il flusso di release previsto è:

```text
krisCC source -> CI/test -> RPM + SHA256 -> build KrisOS -> immagine BootC
```

KrisOS deve consumare l'artefatto RPM già testato e identificarlo con un digest/hash verificato. La build dell'OS non deve fare un `git fetch` di krisCC per ricostruire implicitamente un secondo artefatto a partire da un repository esterno.

L'ordine resta importante: **`krisCC` deve essere installato prima che KrisOS generi `/usr/share/krisos/owned-packages.txt` e `owned-nevra.txt`**. In questo modo viene classificato correttamente come pacchetto della base immutabile e `rk` non proverà mai a trattarlo come pacchetto persistente dell'overlay.

La build deve fallire se l'RPM richiesto non è disponibile, se l'hash non coincide o se l'RPM non si installa correttamente. Dopo l'installazione eseguire almeno:

```bash
rpm -q krisCC
rpm -V krisCC
KRISCC_SMOKE_TEST=1 /usr/bin/krisCC --background
```

Non esistono identità RPM di compatibilità da mantenere: nome pacchetto ed eseguibile sono `krisCC` e non vengono pubblicati alias o `Provides/Obsoletes` per vecchi nomi sperimentali.

## Identità tecnica

Il NEVRA e l'eseguibile sono `krisCC`. Il modulo QML, gli action ID Polkit, il desktop ID e l'AppStream ID usano il namespace `org.kriscc`.

## Verifica reale prima del tag

Provare sulla macchina reale:

```bash
sudo bootc status --format json --format-version=1 | head -c 500
dnf5 repo list --all --json
rpm -q krisCC
rpm -V krisCC
```

Poi verificare manualmente `rk plan/add/rm/sync`, ricerca RPM, apertura e gestione Flatpak tramite Discover, gestione container esterna, Tools & Fix e Servizi & Rete, update BootC, backup create/verify/restore, cronologia locale e selezione one-shot UEFI/GRUB quando disponibile.

Repository: https://github.com/krism-eu/krisCC


## Backup home K1.0

Il profilo home esclude cache, cestino, backup precedenti, `~/.local/share/flatpak` e `~/.local/share/containers`. Questi ultimi sono runtime/app Flatpak e storage Podman ricostruibili; eventuali volumi Podman sono quindi fuori dal backup. I dati personali delle applicazioni Flatpak in `~/.var/app` restano inclusi.