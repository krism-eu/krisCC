#include "SystemBackendRuntime.h"

#include "ContractParsers.h"
#include "PolkitHelper.h"

#include <QPointer>
#include <QProcess>
#include <QTimer>

void SystemBackendRuntime::syncBootAggregate()
{
    m_bootEntriesBusy = m_uefiBusy || m_grubBusy || m_runtimeBootReadOwned;
    QStringList errors;
    if (!m_uefiError.isEmpty())
        errors.append(tr("UEFI: %1").arg(m_uefiError));
    if (!m_grubError.isEmpty())
        errors.append(tr("GRUB: %1").arg(m_grubError));
    m_bootEntriesError = errors.join(QLatin1Char('\n'));
    emit bootEntriesChanged();
}

void SystemBackendRuntime::ensureBootRuntimeConnections()
{
    if (m_bootRuntimeConnectionsInitialized)
        return;
    m_bootRuntimeConnectionsInitialized = true;
    if (!m_polkit)
        return;

    connect(m_polkit, &PolkitHelper::finished, this,
            [this](bool success, const QString &output) {
        if (!m_runtimeBootReadOwned)
            return;
        m_runtimeBootReadOwned = false;
        m_uefiBusy = false;
        if (success) {
            applyUefiEntriesOutput(output);
            m_uefiError.clear();
        } else {
            m_uefiEntries.clear();
            m_nextUefiBootLabel.clear();
            m_currentUefiBootCode.clear();
            m_uefiBootOrder.clear();
            m_uefiError = output.isEmpty()
                ? tr("Impossibile leggere le voci con autorizzazione amministrativa.")
                : output;
        }
        syncBootAggregate();
    });
}

void SystemBackendRuntime::refreshUefiEntries()
{
    ensureBootRuntimeConnections();
    if (m_uefiBusy || m_runtimeBootReadOwned)
        return;

    const QString program = resolveExecutable(QStringLiteral("efibootmgr"));
    if (program.isEmpty()) {
        m_uefiEntries.clear();
        m_nextUefiBootLabel.clear();
        m_currentUefiBootCode.clear();
        m_uefiBootOrder.clear();
        m_uefiError = tr("efibootmgr non disponibile.");
        syncBootAggregate();
        return;
    }

    const quint64 generation = ++m_uefiRequestGeneration;
    m_uefiBusy = true;
    m_uefiError.clear();
    syncBootAggregate();

    auto *process = new QProcess(this);
    const QPointer<QProcess> guard(process);
    m_uefiProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guard, generation](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_uefiProcess || generation != m_uefiRequestGeneration)
            return;
        const bool timedOut = guard->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(guard->readAllStandardOutput());
        m_uefiProcess = nullptr;
        guard->deleteLater();
        m_uefiBusy = false;

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            m_uefiEntries.clear();
            m_nextUefiBootLabel.clear();
            m_currentUefiBootCode.clear();
            m_uefiBootOrder.clear();
            m_uefiError = timedOut
                ? tr("Tempo massimo superato leggendo le voci UEFI.")
                : tr("Impossibile leggere le voci senza privilegi. Usa “Leggi con autorizzazione” per riprovare.");
        } else {
            applyUefiEntriesOutput(output);
            m_uefiError.clear();
        }
        syncBootAggregate();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guard, generation](QProcess::ProcessError error) {
        if (!guard || guard != m_uefiProcess || generation != m_uefiRequestGeneration
            || error != QProcess::FailedToStart)
            return;
        m_uefiProcess = nullptr;
        guard->deleteLater();
        m_uefiBusy = false;
        m_uefiEntries.clear();
        m_nextUefiBootLabel.clear();
        m_currentUefiBootCode.clear();
        m_uefiBootOrder.clear();
        m_uefiError = tr("Impossibile avviare efibootmgr.");
        syncBootAggregate();
    });

    process->start(program, {});
    QTimer::singleShot(15000, process, [this, guard, generation] {
        if (!guard || guard != m_uefiProcess || generation != m_uefiRequestGeneration
            || guard->state() == QProcess::NotRunning)
            return;
        guard->setProperty("krisccTimedOut", true);
        guard->terminate();
        QTimer::singleShot(2000, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
}

void SystemBackendRuntime::refreshUefiEntriesPrivileged()
{
    ensureBootRuntimeConnections();
    if (m_uefiBusy || m_runtimeBootReadOwned || !m_polkit || m_polkit->running()
        || !uefiBootAvailable())
        return;

    m_runtimeBootReadOwned = true;
    m_uefiBusy = true;
    m_uefiError.clear();
    syncBootAggregate();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"),
                      {QStringLiteral("boot-read-uefi")});
}

void SystemBackendRuntime::refreshGrubEntries()
{
    ensureBootRuntimeConnections();
    if (m_grubBusy)
        return;

    const QString program = resolveExecutable(QStringLiteral("grubby"));
    if (program.isEmpty()) {
        m_grubEntries.clear();
        m_grubError = tr("grubby non disponibile.");
        syncBootAggregate();
        return;
    }

    const quint64 generation = ++m_grubRequestGeneration;
    m_grubBusy = true;
    m_grubError.clear();
    syncBootAggregate();

    auto *process = new QProcess(this);
    const QPointer<QProcess> guard(process);
    m_grubProcess = process;
    process->setProcessChannelMode(QProcess::MergedChannels);

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guard, generation](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_grubProcess || generation != m_grubRequestGeneration)
            return;
        const bool timedOut = guard->property("krisccTimedOut").toBool();
        const QString output = QString::fromUtf8(guard->readAllStandardOutput());
        m_grubProcess = nullptr;
        guard->deleteLater();
        m_grubBusy = false;

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            m_grubEntries.clear();
            m_grubError = timedOut ? tr("Tempo massimo superato leggendo le voci GRUB/BLS.")
                                   : tr("Impossibile leggere le voci GRUB/BLS.");
        } else {
            const auto parsed = ContractParsers::parseGrubbyEntries(output.toUtf8());
            if (!parsed.ok()) {
                m_grubEntries.clear();
                m_grubError = tr("Output grubby non riconosciuto.");
            } else {
                m_grubEntries = parsed.values;
                m_grubError.clear();
            }
        }
        syncBootAggregate();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guard, generation](QProcess::ProcessError error) {
        if (!guard || guard != m_grubProcess || generation != m_grubRequestGeneration
            || error != QProcess::FailedToStart)
            return;
        m_grubProcess = nullptr;
        guard->deleteLater();
        m_grubBusy = false;
        m_grubEntries.clear();
        m_grubError = tr("Impossibile avviare grubby.");
        syncBootAggregate();
    });

    process->start(program, {QStringLiteral("--info=ALL")});
    QTimer::singleShot(15000, process, [this, guard, generation] {
        if (!guard || guard != m_grubProcess || generation != m_grubRequestGeneration
            || guard->state() == QProcess::NotRunning)
            return;
        guard->setProperty("krisccTimedOut", true);
        guard->terminate();
        QTimer::singleShot(2000, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
}
