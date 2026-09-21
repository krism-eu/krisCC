#include "SoftwareBackend.h"

#include "PolkitHelper.h"
#include "ProcessRunner.h"
#include "Validators.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

SoftwareBackend::SoftwareBackend(PolkitHelper *polkit, QObject *parent)
    : QObject(parent), m_polkit(polkit)
{
    if (m_polkit) {
        connect(m_polkit, &PolkitHelper::runningChanged, this, &SoftwareBackend::operationStateChanged);
        connect(m_polkit, &PolkitHelper::line, this, [this](const QString &line) {
            if (!m_operationOwned) return;
            m_operationLines.append(line);
            while (m_operationLines.size() > 12) m_operationLines.removeFirst();
            emit operationStateChanged();
        });
        connect(m_polkit, &PolkitHelper::finished, this, [this](bool success, const QString &output) {
            if (!m_operationOwned) return;
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
    return Validators::repositoryId(repoId.trimmed());
}

bool SoftwareBackend::validRepositoryUrl(const QString &value) const
{
    return Validators::repositoryUrl(value.trimmed());
}

bool SoftwareBackend::startPrivileged(const QStringList &args)
{
    if (!canModifyRepositories()) return false;
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
    if (!validRepositoryId(id)) { setError(tr("Identificatore repository non valido.")); return false; }
    return startPrivileged({QStringLiteral("repo-enable"), id});
}

bool SoftwareBackend::disableRepository(const QString &repoId)
{
    const QString id = repoId.trimmed();
    if (!validRepositoryId(id)) { setError(tr("Identificatore repository non valido.")); return false; }
    return startPrivileged({QStringLiteral("repo-disable"), id});
}

bool SoftwareBackend::addRepository(const QString &value)
{
    const QString url = value.trimmed();
    if (!validRepositoryUrl(url)) { setError(tr("Repository non aggiunto: usa un URL HTTPS valido.")); return false; }
    return startPrivileged({QStringLiteral("repo-add"), url});
}

void SoftwareBackend::refreshRepositories()
{
    if (m_busy) return;
    const QFileInfo dnf5(QStringLiteral("/usr/bin/dnf5"));
    if (!dnf5.exists() || !dnf5.isExecutable()) { setError(tr("dnf5 non disponibile.")); return; }

    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    setBusy(true);
    setError({});

    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int,
                           const QByteArray &out, const QByteArray &err, const QString &error) {
        if (runner != m_runner) { runner->deleteLater(); return; }
        m_runner = nullptr;
        runner->deleteLater();
        setBusy(false);

        if (outcome != ProcessRunner::Success) {
            if (outcome == ProcessRunner::TimedOut)
                setError(tr("Tempo massimo superato durante la lettura dei repository DNF5."));
            else if (outcome == ProcessRunner::FailedToStart)
                setError(tr("Impossibile avviare dnf5: %1").arg(error));
            else {
                const QString details = QString::fromUtf8(err.isEmpty() ? out : err).trimmed();
                setError(details.isEmpty() ? tr("Impossibile leggere i repository DNF5.") : details);
            }
            return;
        }

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(out, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
            setError(tr("Output repository DNF5 non valido: %1").arg(parseError.errorString()));
            return;
        }

        QVariantList repos;
        for (const QJsonValue &value : document.array()) {
            if (!value.isObject()) { setError(tr("Formato JSON repository DNF5 non riconosciuto.")); return; }
            const QJsonObject object = value.toObject();
            if (!object.value(QStringLiteral("id")).isString()
                || !object.value(QStringLiteral("name")).isString()
                || !object.value(QStringLiteral("is_enabled")).isBool()) {
                setError(tr("Formato JSON repository DNF5 non riconosciuto."));
                return;
            }
            const QString id = object.value(QStringLiteral("id")).toString().trimmed();
            if (id.isEmpty()) { setError(tr("Formato JSON repository DNF5 non riconosciuto.")); return; }
            QVariantMap repo;
            repo.insert(QStringLiteral("id"), id);
            repo.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
            repo.insert(QStringLiteral("enabled"), object.value(QStringLiteral("is_enabled")).toBool());
            repos.append(repo);
        }

        std::sort(repos.begin(), repos.end(), [](const QVariant &left, const QVariant &right) {
            return left.toMap().value(QStringLiteral("id")).toString().localeAwareCompare(
                       right.toMap().value(QStringLiteral("id")).toString()) < 0;
        });
        m_repositories = repos;
        emit repositoriesChanged();
    });

    ProcessRunner::Options options;
    options.program = QStringLiteral("/usr/bin/dnf5");
    options.arguments = {QStringLiteral("repo"), QStringLiteral("list"),
                         QStringLiteral("--all"), QStringLiteral("--json")};
    options.timeoutMs = 60 * 1000;
    options.maxOutputBytes = 2 * 1024 * 1024;
    options.mergedChannels = false;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        setBusy(false);
        setError(tr("Impossibile inizializzare dnf5."));
    }
}

void SoftwareBackend::setBusy(bool busy)
{
    if (m_busy == busy) return;
    m_busy = busy;
    emit busyChanged();
    emit operationStateChanged();
}

void SoftwareBackend::setError(const QString &error)
{
    if (m_errorText == error) return;
    m_errorText = error;
    emit errorTextChanged();
}
