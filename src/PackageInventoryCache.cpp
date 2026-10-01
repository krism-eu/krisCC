#include "PackageInventoryCache.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

namespace {
constexpr qsizetype kManifestLineLimit = 4096;
}

PackageInventoryCache::PackageInventoryCache(QObject *parent)
    : PackageInventoryCache(Config{}, parent)
{
}

PackageInventoryCache::PackageInventoryCache(const Config &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
{
}

PackageInventoryCache::~PackageInventoryCache()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

PackageInventoryCache *PackageInventoryCache::shared()
{
    static PackageInventoryCache cache;
    return &cache;
}

bool PackageInventoryCache::expired() const
{
    if (!m_ready || m_lastSuccessMs <= 0)
        return true;
    return QDateTime::currentMSecsSinceEpoch() - m_lastSuccessMs >= m_config.ttlMs;
}

void PackageInventoryCache::invalidate()
{
    ++m_epoch;
    m_ready = false;
    m_lastSuccessMs = 0;
    if (m_loading)
        m_forceAgain = true;
    emit inventoryChanged();
}

bool PackageInventoryCache::readManifest(const QString &path, bool missingIsEmpty,
                                         QSet<QString> *target, QString *error) const
{
    if (!target)
        return false;
    target->clear();

    const QFileInfo info(path);
    if (!info.exists()) {
        if (missingIsEmpty)
            return true;
        if (error)
            *error = tr("Manifest non disponibile: %1").arg(path);
        return false;
    }
    if (!info.isFile() || info.isSymLink()) {
        if (error)
            *error = tr("Manifest non sicuro o non regolare: %1").arg(path);
        return false;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = tr("Impossibile leggere il manifest %1: %2").arg(path, file.errorString());
        return false;
    }

    QSet<QString> candidate;
    while (!file.atEnd()) {
        const QByteArray raw = file.readLine(kManifestLineLimit + 2);
        if (raw.size() > kManifestLineLimit && !raw.endsWith('\n')) {
            if (error)
                *error = tr("Riga troppo lunga nel manifest: %1").arg(path);
            return false;
        }
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')))
            candidate.insert(line);
    }
    if (file.error() != QFileDevice::NoError) {
        if (error)
            *error = tr("Lettura incompleta del manifest %1: %2").arg(path, file.errorString());
        return false;
    }

    *target = candidate;
    return true;
}

bool PackageInventoryCache::refreshMetadata(QSet<QString> *owned, QSet<QString> *persistent,
                                            QString *error) const
{
    QSet<QString> nextOwned;
    QSet<QString> nextPersistent;
    QString detail;
    if (!readManifest(m_config.ownedManifest, false, &nextOwned, &detail)) {
        if (error)
            *error = detail;
        return false;
    }
    if (!readManifest(m_config.persistentManifest, true, &nextPersistent, &detail)) {
        if (error)
            *error = detail;
        return false;
    }
    if (owned)
        *owned = nextOwned;
    if (persistent)
        *persistent = nextPersistent;
    return true;
}

void PackageInventoryCache::ensureFresh(bool force)
{
    if (m_loading) {
        if (force) {
            ++m_epoch;
            m_forceAgain = true;
        }
        return;
    }

    if (!force && !expired()) {
        QTimer::singleShot(0, this, [this] { emit refreshFinished(true, QString()); });
        return;
    }

    QSet<QString> owned;
    QSet<QString> persistent;
    QString metadataError;
    if (!refreshMetadata(&owned, &persistent, &metadataError)) {
        m_ready = false;
        m_error = metadataError;
        emit inventoryChanged();
        QTimer::singleShot(0, this, [this, metadataError] {
            emit refreshFinished(false, metadataError);
        });
        return;
    }

    startInstalledRefresh(std::move(owned), std::move(persistent), m_epoch);
}

void PackageInventoryCache::startInstalledRefresh(QSet<QString> owned,
                                                  QSet<QString> persistent,
                                                  quint64 requestEpoch)
{
    m_loading = true;
    m_error.clear();
    ++m_loadCount;

    auto *raw = new QProcess(this);
    const QPointer<QProcess> process(raw);
    m_process = raw;
    raw->setProcessChannelMode(QProcess::SeparateChannels);
    raw->setStandardInputFile(QProcess::nullDevice());

    connect(raw, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, owned = std::move(owned), persistent = std::move(persistent), requestEpoch]
            (int exitCode, QProcess::ExitStatus status) mutable {
        if (!process || process != m_process)
            return;
        const bool timedOut = process->property("krisccTimedOut").toBool();
        const QByteArray output = process->readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(process->readAllStandardError()).trimmed();
        m_process = nullptr;
        process->deleteLater();
        m_loading = false;

        if (m_forceAgain || requestEpoch != m_epoch) {
            m_forceAgain = false;
            ensureFresh(true);
            return;
        }

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            const QString detail = timedOut
                ? tr("Tempo massimo superato durante la lettura dell'inventario RPM.")
                : (stderrText.isEmpty() ? tr("Impossibile leggere l'inventario RPM.") : stderrText);
            finishRefresh(false, detail);
            return;
        }

        QSet<QString> installed;
        for (const QByteArray &rawLine : output.split('\n')) {
            const QString name = QString::fromUtf8(rawLine).trimmed();
            if (!name.isEmpty())
                installed.insert(name);
        }

        m_installed = std::move(installed);
        m_owned = std::move(owned);
        m_persistent = std::move(persistent);
        m_ready = true;
        m_lastSuccessMs = QDateTime::currentMSecsSinceEpoch();
        finishRefresh(true);
    });

    connect(raw, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (!process || process != m_process || error != QProcess::FailedToStart)
            return;
        m_process = nullptr;
        process->deleteLater();
        m_loading = false;
        if (m_forceAgain) {
            m_forceAgain = false;
            ensureFresh(true);
            return;
        }
        finishRefresh(false, tr("Impossibile avviare rpm per leggere l'inventario installato."));
    });

    raw->start(m_config.rpmProgram,
               {QStringLiteral("-qa"), QStringLiteral("--qf"), QStringLiteral("%{NAME}\\n")});
    QTimer::singleShot(m_config.timeoutMs, raw, [this, process] {
        if (!process || process != m_process || process->state() == QProcess::NotRunning)
            return;
        process->setProperty("krisccTimedOut", true);
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process && process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
}

void PackageInventoryCache::finishRefresh(bool success, const QString &error)
{
    if (!success) {
        m_ready = false;
        m_error = error;
    } else {
        m_error.clear();
    }
    emit inventoryChanged();
    emit refreshFinished(success, m_error);
}
