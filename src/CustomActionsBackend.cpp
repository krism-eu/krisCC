#include "CustomActionsBackend.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <unistd.h>

namespace {
constexpr qsizetype kMaxActions = 100;
constexpr qsizetype kMaxName = 80;
constexpr qsizetype kMaxDescription = 240;
constexpr qsizetype kMaxScript = 64 * 1024;
constexpr qsizetype kMaxOutput = 128 * 1024;
constexpr int kActionTimeoutMs = 30 * 60 * 1000;
}

CustomActionsBackend::CustomActionsBackend(QObject *parent)
    : QObject(parent)
{
    reload();
}

QString CustomActionsBackend::storagePath() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(root).filePath(QStringLiteral("custom-actions.json"));
}

void CustomActionsBackend::reload()
{
    if (m_running)
        return;

    m_actions.clear();
    m_errorText.clear();
    m_storageValid = true;

    QFile file(storagePath());
    if (!file.exists()) {
        emit actionsChanged();
        emit stateChanged();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_storageValid = false;
        m_errorText = tr("Impossibile leggere i comandi personali.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_storageValid = false;
        m_errorText = tr("Il file dei comandi personali non è valido e non verrà sovrascritto.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt(-1) != 1
        || !root.value(QStringLiteral("actions")).isArray()) {
        m_storageValid = false;
        m_errorText = tr("Versione del file dei comandi personali non supportata.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("actions")).toArray();
    if (array.size() > kMaxActions) {
        m_storageValid = false;
        m_errorText = tr("Troppi comandi personali nel file di configurazione.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        const QString id = object.value(QStringLiteral("id")).toString();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString description = object.value(QStringLiteral("description")).toString();
        const QString script = object.value(QStringLiteral("script")).toString();
        QString validationError;
        if (id.isEmpty() || !validateAction(name, description, script, &validationError))
            continue;

        QVariantMap action;
        action.insert(QStringLiteral("id"), id);
        action.insert(QStringLiteral("name"), name);
        action.insert(QStringLiteral("description"), description);
        action.insert(QStringLiteral("script"), script);
        action.insert(QStringLiteral("confirm"), object.value(QStringLiteral("confirm")).toBool(false));
        m_actions.append(action);
    }

    emit actionsChanged();
    emit stateChanged();
}

bool CustomActionsBackend::validateAction(const QString &name, const QString &description,
                                          const QString &script, QString *error) const
{
    const QString cleanName = name.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > kMaxName) {
        if (error)
            *error = tr("Il nome deve contenere da 1 a %1 caratteri.").arg(kMaxName);
        return false;
    }
    if (description.size() > kMaxDescription) {
        if (error)
            *error = tr("La descrizione è troppo lunga.");
        return false;
    }
    if (script.trimmed().isEmpty() || script.size() > kMaxScript || script.contains(QChar::Null)) {
        if (error)
            *error = tr("Lo script è vuoto o troppo grande.");
        return false;
    }
    return true;
}

int CustomActionsBackend::indexForId(const QString &id) const
{
    for (qsizetype i = 0; i < m_actions.size(); ++i) {
        if (m_actions.at(i).toMap().value(QStringLiteral("id")).toString() == id)
            return int(i);
    }
    return -1;
}

bool CustomActionsBackend::persist()
{
    if (!m_storageValid) {
        m_errorText = tr("Il file esistente non è valido: correggilo prima di salvare nuove azioni.");
        emit stateChanged();
        return false;
    }

    const QString path = storagePath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        m_errorText = tr("Impossibile creare la cartella di configurazione krisCC.");
        emit stateChanged();
        return false;
    }

    QJsonArray array;
    for (const QVariant &value : m_actions) {
        const QVariantMap action = value.toMap();
        QJsonObject object;
        object.insert(QStringLiteral("id"), action.value(QStringLiteral("id")).toString());
        object.insert(QStringLiteral("name"), action.value(QStringLiteral("name")).toString());
        object.insert(QStringLiteral("description"), action.value(QStringLiteral("description")).toString());
        object.insert(QStringLiteral("script"), action.value(QStringLiteral("script")).toString());
        object.insert(QStringLiteral("confirm"), action.value(QStringLiteral("confirm")).toBool());
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("actions"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit()) {
        m_errorText = tr("Impossibile salvare i comandi personali.");
        emit stateChanged();
        return false;
    }

    m_errorText.clear();
    emit stateChanged();
    return true;
}

bool CustomActionsBackend::saveAction(const QString &id, const QString &name,
                                      const QString &description, const QString &script,
                                      bool confirmBeforeRun)
{
    if (m_running || !m_storageValid)
        return false;

    QString validationError;
    if (!validateAction(name, description, script, &validationError)) {
        m_errorText = validationError;
        emit stateChanged();
        return false;
    }

    QVariantMap action;
    QString actionId = id.trimmed();
    int index = actionId.isEmpty() ? -1 : indexForId(actionId);
    if (index < 0) {
        if (m_actions.size() >= kMaxActions) {
            m_errorText = tr("Limite di %1 comandi personali raggiunto.").arg(kMaxActions);
            emit stateChanged();
            return false;
        }
        actionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    action.insert(QStringLiteral("id"), actionId);
    action.insert(QStringLiteral("name"), name.trimmed());
    action.insert(QStringLiteral("description"), description.trimmed());
    action.insert(QStringLiteral("script"), script);
    action.insert(QStringLiteral("confirm"), confirmBeforeRun);

    const QVariantList previous = m_actions;
    if (index >= 0)
        m_actions[index] = action;
    else
        m_actions.append(action);

    if (!persist()) {
        m_actions = previous;
        return false;
    }

    emit actionsChanged();
    return true;
}

bool CustomActionsBackend::removeAction(const QString &id)
{
    if (m_running || !m_storageValid)
        return false;
    const int index = indexForId(id);
    if (index < 0)
        return false;

    const QVariantList previous = m_actions;
    m_actions.removeAt(index);
    if (!persist()) {
        m_actions = previous;
        return false;
    }
    emit actionsChanged();
    return true;
}

bool CustomActionsBackend::runAction(const QString &id)
{
    if (m_running || ::geteuid() == 0) {
        m_errorText = ::geteuid() == 0
            ? tr("I comandi personali sono disabilitati quando krisCC è eseguito come root.")
            : tr("Un comando personale è già in esecuzione.");
        emit stateChanged();
        return false;
    }

    const int index = indexForId(id);
    if (index < 0)
        return false;
    const QVariantMap action = m_actions.at(index).toMap();
    const QString shell = QStandardPaths::findExecutable(QStringLiteral("bash"));
    if (shell.isEmpty()) {
        m_errorText = tr("bash non è disponibile.");
        emit stateChanged();
        return false;
    }

    auto *process = new QProcess(this);
    m_process = process;
    m_running = true;
    m_cancelRequested = false;
    m_timedOut = false;
    m_runningId = id;
    m_output.clear();
    m_errorText.clear();
    m_resultState = QStringLiteral("running");
    emit stateChanged();

    process->setWorkingDirectory(QDir::homePath());
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (process == m_process)
            appendOutput(process->readAllStandardOutput());
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        if (process != m_process)
            return;
        appendOutput(process->readAllStandardOutput());
        process->deleteLater();
        m_process = nullptr;
        if (m_timedOut)
            finish(QStringLiteral("timeout"), tr("Tempo massimo di 30 minuti superato."));
        else if (m_cancelRequested)
            finish(QStringLiteral("cancelled"), tr("Comando annullato."));
        else if (status == QProcess::NormalExit && exitCode == 0)
            finish(QStringLiteral("success"));
        else
            finish(QStringLiteral("error"),
                   tr("Comando terminato con codice %1.").arg(exitCode));
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (process != m_process || error != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        process->deleteLater();
        m_process = nullptr;
        finish(QStringLiteral("error"), tr("Impossibile avviare lo script: %1").arg(reason));
    });

    process->start(shell, {QStringLiteral("--noprofile"), QStringLiteral("--norc"),
                           QStringLiteral("-c"), action.value(QStringLiteral("script")).toString()});

    QTimer::singleShot(kActionTimeoutMs, process, [this, process] {
        if (process != m_process || process->state() == QProcess::NotRunning)
            return;
        m_timedOut = true;
        process->terminate();
        QTimer::singleShot(2000, process, [process] {
            if (process->state() != QProcess::NotRunning)
                process->kill();
        });
    });
    return true;
}

void CustomActionsBackend::appendOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    m_output += QString::fromUtf8(data);
    if (m_output.size() > kMaxOutput)
        m_output = tr("[output precedente omesso]\n") + m_output.right(kMaxOutput);
    emit stateChanged();
}

void CustomActionsBackend::finish(const QString &state, const QString &message)
{
    m_running = false;
    m_runningId.clear();
    m_resultState = state;
    if (!message.isEmpty()) {
        if (!m_output.isEmpty() && !m_output.endsWith(QLatin1Char('\n')))
            m_output += QLatin1Char('\n');
        m_output += message;
    }
    emit stateChanged();
}

void CustomActionsBackend::cancel()
{
    if (!m_process || !m_running)
        return;
    m_cancelRequested = true;
    m_process->terminate();
    const QPointer<QProcess> process = m_process;
    QTimer::singleShot(2000, this, [process] {
        if (process && process->state() != QProcess::NotRunning)
            process->kill();
    });
}
