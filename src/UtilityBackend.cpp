#include "UtilityBackend.h"

#include "OperationLog.h"
#include "ProcessRunner.h"
#include "Validators.h"

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace {
constexpr int kShortQueryTimeoutMs = 30 * 1000;
constexpr int kRepositoryQueryTimeoutMs = 2 * 60 * 1000;
}

UtilityBackend::UtilityBackend(QObject *parent)
    : QObject(parent)
{
}

bool UtilityBackend::validPackageName(const QString &name) const
{
    return Validators::packageName(name);
}


void UtilityBackend::setImmediateError(const QString &title, const QString &operationId, const QString &message)
{
    m_title = title;
    m_operationId = operationId;
    m_output = message;
    m_resultState = QStringLiteral("error");
    emit stateChanged();
}

bool UtilityBackend::start(const QString &program, const QStringList &args, const QString &title,
                           const QString &operationId, int timeoutMs, bool structuredOutput)
{
    if (m_busy) {
        qDebug().noquote() << "UtilityBackend: refusing" << operationId
                           << "while operation" << m_operationId << "is still running";
        return false;
    }

    const QString executable = program.startsWith(QLatin1Char('/'))
        ? (QFileInfo(program).isExecutable() ? program : QString())
        : QStandardPaths::findExecutable(program);
    if (executable.isEmpty()) {
        setImmediateError(title, operationId, tr("Comando non disponibile: %1").arg(program));
        return false;
    }

    m_busy = true;
    m_title = title;
    m_operationId = operationId;
    m_output.clear();
    m_rows.clear();
    m_resultState = QStringLiteral("running");
    emit stateChanged();

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    connect(runner, &ProcessRunner::finished, this,
            [this, runner, structuredOutput](ProcessRunner::Outcome outcome, int exitCode,
                           const QByteArray &stdoutData, const QByteArray &stderrData,
                           const QString &error) {
        if (runner != m_runner)
            return;
        QByteArray combined = stdoutData;
        if (!structuredOutput || outcome != ProcessRunner::Success) {
            if (!combined.isEmpty() && !stderrData.isEmpty() && !combined.endsWith('\n'))
                combined.append('\n');
            combined.append(stderrData);
        }
        const QString text = QString::fromUtf8(combined);
        switch (outcome) {
        case ProcessRunner::Success:
            finish(text, QStringLiteral("success"));
            break;
        case ProcessRunner::Cancelled:
            finish(text.isEmpty() ? tr("Operazione annullata.") : text,
                   QStringLiteral("cancelled"));
            break;
        case ProcessRunner::TimedOut:
            finish(text.isEmpty() ? tr("Tempo massimo superato; il comando è stato interrotto.") : text,
                   QStringLiteral("timeout"));
            break;
        case ProcessRunner::FailedToStart:
            finish(tr("Impossibile avviare il comando: %1").arg(error),
                   QStringLiteral("error"));
            break;
        case ProcessRunner::ExitError:
            finish(text.isEmpty() ? tr("Comando terminato con codice %1.").arg(exitCode) : text,
                   QStringLiteral("error"));
            break;
        }
    });

    ProcessRunner::Options options;
    options.program = executable;
    options.arguments = args;
    options.timeoutMs = timeoutMs;
    options.maxOutputBytes = 512 * 1024;
    options.mergedChannels = !structuredOutput;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        finish(tr("Impossibile inizializzare il comando."), QStringLiteral("error"));
        return false;
    }
    return true;
}

void UtilityBackend::finish(const QString &message, const QString &state)
{
    const QString completedOperation = m_operationId;
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    m_busy = false;
    m_output = message.trimmed();
    m_rows.clear();
    m_resultState = state;

    emit stateChanged();
}

bool UtilityBackend::cancel()
{
    return m_runner && m_busy && m_runner->cancel();
}

void UtilityBackend::clearResult()
{
    if (m_busy)
        return;
    m_title.clear();
    m_output.clear();
    m_rows.clear();
    m_operationId.clear();
    m_resultState = QStringLiteral("idle");
    emit stateChanged();
}

bool UtilityBackend::runBookmark(const QString &id)
{
    if (id == QStringLiteral("pipewire-restart"))
        return start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("pipewire"), QStringLiteral("pipewire-pulse"), QStringLiteral("wireplumber")}, tr("Riavvio Audio PipeWire"), QStringLiteral("bookmark.pipewire-restart"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("journal-vacuum"))
        return start(QStringLiteral("journalctl"), {QStringLiteral("--vacuum-size=100M")}, tr("Pulizia Journal Systemd"), QStringLiteral("bookmark.journal-vacuum"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("dns-flush"))
        return start(QStringLiteral("resolvectl"), {QStringLiteral("flush-caches")}, tr("Svuota cache DNS"), QStringLiteral("repair.dns-flush"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("cleanup-estimate")) {
        const QString script = QStringLiteral(
            "printf 'Cestini: '; du -sh "$HOME/.local/share/Trash" 2>/dev/null | cut -f1 || echo '0'; "
            "printf 'Journal: '; journalctl --disk-usage 2>/dev/null | sed 's/^.*take up /circa /'; "
            "printf 'Cache DNF: '; du -sh /var/cache/libdnf5 2>/dev/null | cut -f1 || echo 'non misurabile'; "
            "printf 'Flatpak inutilizzati: calcolo esatto durante la pulizia\\n'");
        return start(QStringLiteral("/usr/bin/bash"), {QStringLiteral("-c"), script},
                     tr("Stima spazio recuperabile"), QStringLiteral("cleanup.estimate"), kShortQueryTimeoutMs);
    }
    if (id == QStringLiteral("dnf-clean"))
        return start(QStringLiteral("dnf5"), {QStringLiteral("clean"), QStringLiteral("all")}, tr("Pulizia cache DNF5"), QStringLiteral("cleanup.dnf"), kRepositoryQueryTimeoutMs);
    if (id == QStringLiteral("flatpak-unused"))
        return start(QStringLiteral("flatpak"), {QStringLiteral("uninstall"), QStringLiteral("--user"), QStringLiteral("--unused"), QStringLiteral("--noninteractive"), QStringLiteral("--assumeyes")}, tr("Rimozione runtime Flatpak inutilizzati"), QStringLiteral("cleanup.flatpak"), kRepositoryQueryTimeoutMs);
    if (id == QStringLiteral("vainfo"))
        return start(QStringLiteral("vainfo"), {QStringLiteral("--display"), QStringLiteral("drm"), QStringLiteral("--device"), QStringLiteral("/dev/dri/renderD128")}, tr("Accelerazione video VA-API"), QStringLiteral("diagnostic.vainfo"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("gpu-driver"))
        return start(QStringLiteral("glxinfo"), {QStringLiteral("-B")}, tr("Info Driver GPU"), QStringLiteral("bookmark.gpu-driver"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("vulkan-info"))
        return start(QStringLiteral("vulkaninfo"), {QStringLiteral("--summary")}, tr("Riepilogo Vulkan"), QStringLiteral("bookmark.vulkan-info"), kShortQueryTimeoutMs);

    if (id == QStringLiteral("failed-units"))
        return start(QStringLiteral("systemctl"), {QStringLiteral("--failed"), QStringLiteral("--no-pager"), QStringLiteral("--plain")}, tr("Unità systemd fallite"), QStringLiteral("bookmark.failed-units"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("user-failed-units"))
        return start(QStringLiteral("systemctl"), {QStringLiteral("--user"), QStringLiteral("--failed"), QStringLiteral("--no-pager"), QStringLiteral("--plain")}, tr("Unità utente fallite"), QStringLiteral("bookmark.user-failed-units"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("timers"))
        return start(QStringLiteral("systemctl"), {QStringLiteral("list-timers"), QStringLiteral("--all"), QStringLiteral("--no-pager")}, tr("Timer systemd"), QStringLiteral("bookmark.timers"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("ports"))
        return start(QStringLiteral("ss"), {QStringLiteral("-lntu")}, tr("Porte in ascolto"), QStringLiteral("bookmark.ports"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("sessions"))
        return start(QStringLiteral("loginctl"), {QStringLiteral("list-sessions"), QStringLiteral("--no-legend")}, tr("Sessioni attive"), QStringLiteral("bookmark.sessions"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("mounts"))
        return start(QStringLiteral("findmnt"), {QStringLiteral("-o"), QStringLiteral("TARGET,SOURCE,FSTYPE,OPTIONS")}, tr("Mount attivi"), QStringLiteral("bookmark.mounts"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("selinux"))
        return start(QStringLiteral("getenforce"), {}, tr("SELinux"), QStringLiteral("bookmark.selinux"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("services-active"))
        return start(QStringLiteral("systemctl"), {QStringLiteral("list-units"), QStringLiteral("--type=service"), QStringLiteral("--state=running"), QStringLiteral("--no-pager"), QStringLiteral("--plain")}, tr("Servizi attivi"), QStringLiteral("bookmark.services-active"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("uptime"))
        return start(QStringLiteral("uptime"), {QStringLiteral("-p")}, tr("Tempo di attività"), QStringLiteral("bookmark.uptime"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("boot-time"))
        return start(QStringLiteral("systemd-analyze"), {QStringLiteral("time")}, tr("Tempo di avvio"), QStringLiteral("bookmark.boot-time"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("blame"))
        return start(QStringLiteral("systemd-analyze"), {QStringLiteral("blame")}, tr("Servizi più lenti all'avvio"), QStringLiteral("bookmark.blame"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("disk-space"))
        return start(QStringLiteral("df"), {QStringLiteral("-hT"), QStringLiteral("-x"), QStringLiteral("tmpfs"), QStringLiteral("-x"), QStringLiteral("devtmpfs")}, tr("Spazio filesystem"), QStringLiteral("bookmark.disk-space"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("inodes"))
        return start(QStringLiteral("df"), {QStringLiteral("-hi"), QStringLiteral("-x"), QStringLiteral("tmpfs"), QStringLiteral("-x"), QStringLiteral("devtmpfs")}, tr("Inode filesystem"), QStringLiteral("bookmark.inodes"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("journal-size"))
        return start(QStringLiteral("journalctl"), {QStringLiteral("--disk-usage")}, tr("Spazio journal"), QStringLiteral("bookmark.journal-size"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("partitions") || id == QStringLiteral("block-devices"))
        return start(QStringLiteral("lsblk"), {QStringLiteral("-e"), QStringLiteral("7"), QStringLiteral("-o"), QStringLiteral("NAME,PARTN,SIZE,FSTYPE,FSVER,LABEL,UUID,MOUNTPOINTS")}, tr("Dischi e partizioni"), QStringLiteral("bookmark.partitions"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("network"))
        return start(QStringLiteral("ip"), {QStringLiteral("-brief"), QStringLiteral("address")}, tr("Interfacce di rete"), QStringLiteral("bookmark.network"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("routes"))
        return start(QStringLiteral("ip"), {QStringLiteral("route")}, tr("Route di rete"), QStringLiteral("bookmark.routes"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("dns"))
        return start(QStringLiteral("resolvectl"), {QStringLiteral("status")}, tr("DNS"), QStringLiteral("bookmark.dns"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("journal-errors"))
        return start(QStringLiteral("journalctl"), {QStringLiteral("-b"), QStringLiteral("-p"), QStringLiteral("warning"), QStringLiteral("--no-pager"), QStringLiteral("-n"), QStringLiteral("200")}, tr("Warning ed errori dell'avvio"), QStringLiteral("bookmark.journal-errors"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("kernel-errors"))
        return start(QStringLiteral("journalctl"), {QStringLiteral("-k"), QStringLiteral("-b"), QStringLiteral("-p"), QStringLiteral("warning"), QStringLiteral("--no-pager"), QStringLiteral("-n"), QStringLiteral("200")}, tr("Warning kernel"), QStringLiteral("bookmark.kernel-errors"), kShortQueryTimeoutMs);
    if (id == QStringLiteral("unneeded-rpms"))
        return start(QStringLiteral("/usr/bin/dnf5"), {QStringLiteral("repoquery"), QStringLiteral("--installed"), QStringLiteral("--unneeded")},
                     tr("RPM non necessari"), QStringLiteral("bookmark.unneeded-rpms"), kRepositoryQueryTimeoutMs);
    if (id == QStringLiteral("fstab-order"))
        return start(QStringLiteral("findmnt"), {QStringLiteral("--fstab"), QStringLiteral("--evaluate"), QStringLiteral("-o"), QStringLiteral("TARGET,SOURCE,FSTYPE,OPTIONS")}, tr("Ordine mount configurato"), QStringLiteral("bookmark.fstab-order"), kShortQueryTimeoutMs);

    if (id == QStringLiteral("health")) {
        const QString script = QStringLiteral(
            "printf '=== Unità fallite ===\\n'; systemctl --failed --no-legend --plain || true; "
            "printf '\\n=== Overlay /usr ===\\n'; findmnt -n -o SOURCE,FSTYPE,OPTIONS /usr 2>/dev/null || true; "
            "printf '\\n=== Spazio ===\\n'; df -h / /var /home 2>/dev/null || df -h /; "
            "printf '\\n=== rk ===\\n'; /usr/bin/rk status 2>&1 || true; "
            "printf '\\n=== BootC ===\\n'; /usr/bin/pkexec /usr/libexec/kriscc/bootc-status humanreadable 2>&1 || true");
        return start(QStringLiteral("/usr/bin/bash"), {QStringLiteral("-c"), script},
                     tr("Salute KrisOS"), QStringLiteral("bookmark.health"), kRepositoryQueryTimeoutMs);
    }

    if (id == QStringLiteral("security")) {
        const QString script = QStringLiteral(
            "printf 'SELinux: '; if command -v getenforce >/dev/null; then getenforce; else echo 'n/d'; fi; "
            "printf 'Firewalld: '; systemctl is-active firewalld.service 2>/dev/null || true; "
            "if [ -d /sys/firmware/efi ]; then echo 'Boot mode: UEFI'; else echo 'Boot mode: BIOS'; fi; "
            "if command -v mokutil >/dev/null; then mokutil --sb-state 2>&1; else echo 'Secure Boot: verifica non disponibile (mokutil assente)'; fi");
        return start(QStringLiteral("/usr/bin/bash"), {QStringLiteral("-c"), script},
                     tr("Sicurezza"), QStringLiteral("bookmark.security"), kShortQueryTimeoutMs);
    }

    return false;
}

bool UtilityBackend::previewRpmInstall(const QString &packageName)
{
    if (!validPackageName(packageName)) {
        setImmediateError(tr("Anteprima RPM"), QStringLiteral("rpm.plan"), tr("Nome pacchetto non valido."));
        return false;
    }
    return start(QStringLiteral("/usr/bin/rk"),
                 {QStringLiteral("plan"), packageName},
                 tr("Piano installazione persistente: %1").arg(packageName),
                 QStringLiteral("rpm.plan"), kRepositoryQueryTimeoutMs);
}

