#include "SoftwareBackend.h"

#include "PolkitHelper.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>

SoftwareBackend::SoftwareBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent)
    , m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &SoftwareBackend::operationStateChanged);
        connect(m_polkit, &PolkitHelper::line, this, [this](const QString &line) {
            if (!m_operationOwned)
                return;
            m_operationLines.append(line);
            while (m_operationLines.size() > 12)
                m_operationLines.removeFirst();
            emit operationStateChanged();
        });
        connect(m_polkit, &PolkitHelper::finished, this,
                [this](bool success, const QString &output) {
            if (!m_operationOwned)
                return;
            m_operationOwned = false;
            m_operationRunning = false;
            m_operationState = success ? QStringLiteral("success") : QStringLiteral("error");
            if (m_operationLines.isEmpty() && !output.trimmed().isEmpty())
                m_operationLines.append(output.trimmed());
            emit operationStateChanged();
            emit operationFinished(success, output);
            refreshRepositories();
        });
    }
}

bool SoftwareBackend::canModifyRepositories() const
{
    return !m_busy && m_polkit && !m_polkit->running() && !m_operationRunning;
}

bool SoftwareBackend::validRepositoryId(const QString &repoId) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"));
    return pattern.match(repoId.trimmed()).hasMatch();
}

bool SoftwareBackend::validRepositoryUrl(const QString &value) const
{
    const QString urlText = value.trimmed();
    if (urlText.isEmpty() || urlText.size() > 2048
        || urlText.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        return false;
    const QUrl url(urlText);
    return url.isValid()
        && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool SoftwareBackend::startPrivileged(const QStringList &args)
{
    if (!canModifyRepositories())
        return false;
    m_operationOwned = true;
    m_operationRunning = true;
    m_operationState = QStringLiteral("running");
    m_operationLines.clear();
    setError({});
    emit operationStateChanged();
    m_polkit->execute(QStringLiteral("/usr/libexec/kriscc/admin"), args);
    return true;
}

bool SoftwareBackend::enableRepository(const QString &repoId)
{
    const QString id = repoId.trimmed();
    if (!validRepositoryId(id)) {
        setError(tr("Identificatore repository non valido."));
        return false;
    }
    return startPrivileged({QStringLiteral("repo-enable"), id});
}

bool SoftwareBackend::disableRepository(const QString &repoId)
{
    const QString id = repoId.trimmed();
    if (!validRepositoryId(id)) {
        setError(tr("Identificatore repository non valido."));
        return false;
    }
    return startPrivileged({QStringLiteral("repo-disable"), id});
}

bool SoftwareBackend::addRepository(const QString &value)
{
    const QString url = value.trimmed();
    if (!validRepositoryUrl(url)) {
        setError(tr("Repository non aggiunto: usa un URL HTTPS valido."));
        return false;
    }
    return startPrivileged({QStringLiteral("repo-add"), url});
}

void SoftwareBackend::refreshRepositories()
{
    if (m_busy)
        return;

    const QFileInfo dnf5(QStringLiteral("/usr/bin/dnf5"));
    if (!dnf5.exists() || !dnf5.isExecutable()) {
        setError(tr("dnf5 non disponibile."));
        return;
    }

    setBusy(true);
    setError({});

    auto *process = new QProcess(this);
    const QPointer<QProcess> guard(process);
    m_process = process;
    process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, guard](int exitCode, QProcess::ExitStatus status) {
        if (!guard || guard != m_process)
            return;

        const bool timedOut = guard->property("krisccTimedOut").toBool();
        const QByteArray output = guard->readAllStandardOutput();
        const QString stderrText = QString::fromUtf8(guard->readAllStandardError()).trimmed();
        m_process = nullptr;
        guard->deleteLater();
        setBusy(false);

        if (timedOut || status != QProcess::NormalExit || exitCode != 0) {
            setError(timedOut ? tr("Tempo massimo superato durante la lettura dei repository DNF5.")
                              : (stderrText.isEmpty() ? tr("Impossibile leggere i repository DNF5.") : stderrText));
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(output, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            setError(tr("Output repository DNF5 non valido: %1").arg(parseError.errorString()));
            return;
        }

        QVariantList repos;
        bool contractInvalid = false;
        for (const QJsonValue &value : document.array()) {
            if (!value.isObject()) {
                contractInvalid = true;
                break;
            }

            const QJsonObject object = value.toObject();
            if (!object.value(QStringLiteral("id")).isString()
                || !object.value(QStringLiteral("name")).isString()
                || !object.value(QStringLiteral("is_enabled")).isBool()) {
                contractInvalid = true;
                break;
            }

            const QString id = object.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty()) {
                contractInvalid = true;
                break;
            }

            QVariantMap repo;
            repo.insert(QStringLiteral("id"), id);
            repo.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
            repo.insert(QStringLiteral("enabled"), object.value(QStringLiteral("is_enabled")).toBool());
            repos.append(repo);
        }

        if (contractInvalid) {
            setError(tr("Formato JSON repository DNF5 non riconosciuto."));
            return;
        }

        std::sort(repos.begin(), repos.end(), [](const QVariant &left, const QVariant &right) {
            const QVariantMap a = left.toMap();
            const QVariantMap b = right.toMap();
            return a.value(QStringLiteral("id")).toString().localeAwareCompare(
                       b.value(QStringLiteral("id")).toString()) < 0;
        });

        m_repositories = repos;
        emit repositoriesChanged();
    });

    connect(process, &QProcess::errorOccurred, this,
            [this, guard](QProcess::ProcessError error) {
        if (!guard || guard != m_process || error != QProcess::FailedToStart)
            return;
        const QString reason = guard->errorString();
        m_process = nullptr;
        guard->deleteLater();
        setBusy(false);
        setError(tr("Impossibile avviare dnf5: %1").arg(reason));
    });

    process->start(QStringLiteral("/usr/bin/dnf5"),
                   {QStringLiteral("repo"), QStringLiteral("list"),
                    QStringLiteral("--all"), QStringLiteral("--json")});
    QTimer::singleShot(60 * 1000, process, [this, guard] {
        if (!guard || guard != m_process || guard->state() == QProcess::NotRunning)
            return;
        guard->setProperty("krisccTimedOut", true);
        guard->terminate();
        QTimer::singleShot(2000, guard, [guard] {
            if (guard && guard->state() != QProcess::NotRunning)
                guard->kill();
        });
    });
}

void SoftwareBackend::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
    emit operationStateChanged();
}

void SoftwareBackend::setError(const QString &error)
{
    if (m_errorText == error)
        return;
    m_errorText = error;
    emit errorTextChanged();
}
