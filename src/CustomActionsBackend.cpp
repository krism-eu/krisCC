#include "CustomActionsBackend.h"
#include "BashPromptConfig.h"
#include "ProcessRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QStringConverter>
#include <QUuid>

#include <unistd.h>

#include <algorithm>

namespace {
constexpr qsizetype kMaxActions = 100;
constexpr qsizetype kMaxQuickActions = 8;
constexpr qsizetype kMaxName = 80;
constexpr qsizetype kMaxDescription = 240;
constexpr qsizetype kMaxScript = 64 * 1024;
constexpr qsizetype kMaxCombinedTextBytes = 16 * 1024 * 1024;
constexpr int kActionTimeoutMs = 30 * 60 * 1000;
const QString kShell = QStringLiteral("/usr/bin/bash");
const QString kDefaultActionIcon = QStringLiteral("utilities-terminal");

const QStringList &allowedActionIcons()
{
    static const QStringList icons = {
        QStringLiteral("utilities-terminal"),
        QStringLiteral("system-run"),
        QStringLiteral("system-search"),
        QStringLiteral("applications-system"),
        QStringLiteral("preferences-system"),
        QStringLiteral("drive-harddisk"),
        QStringLiteral("folder"),
        QStringLiteral("document-new"),
        QStringLiteral("document-save"),
        QStringLiteral("network-wired"),
        QStringLiteral("dialog-information"),
        QStringLiteral("tools-wizard")
    };
    return icons;
}

QString normalizedActionIcon(const QString &iconName)
{
    return allowedActionIcons().contains(iconName) ? iconName : kDefaultActionIcon;
}

bool isUtf8Text(const QByteArray &data)
{
    if (data.contains('\0'))
        return false;

    QStringDecoder decoder(QStringDecoder::Utf8);
    decoder.decode(data);
    return !decoder.hasError();
}
}

CustomActionsBackend::CustomActionsBackend(QObject *parent)
    : QObject(parent)
{
    reload();
}

CustomActionsBackend::~CustomActionsBackend() = default;

QString CustomActionsBackend::storagePath() const
{
    const QString root = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return QDir(root).filePath(QStringLiteral("custom-actions.json"));
}

bool CustomActionsBackend::validId(const QString &id) const
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_-]{0,63}$"));
    return pattern.match(id).hasMatch();
}

void CustomActionsBackend::reload()
{
    if (m_running)
        return;

    m_errorText.clear();
    m_storageValid = true;

    const QString path = storagePath();
    const QFileInfo fileInfo(path);
    if (fileInfo.isSymLink()) {
        m_actions.clear();
        m_storageValid = false;
        m_errorText = tr("Il file dei comandi personali è un collegamento simbolico e non verrà usato.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    QFile file(path);
    if (!file.exists()) {
        m_actions.clear();
        emit actionsChanged();
        emit stateChanged();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        m_actions.clear();
        m_storageValid = false;
        m_errorText = tr("Impossibile leggere i comandi personali.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        m_actions.clear();
        m_storageValid = false;
        m_errorText = tr("Il file dei comandi personali non è valido e non verrà sovrascritto.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schema")).toInt(-1) != 1
        || !root.value(QStringLiteral("actions")).isArray()) {
        m_actions.clear();
        m_storageValid = false;
        m_errorText = tr("Versione del file dei comandi personali non supportata.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    const QJsonArray array = root.value(QStringLiteral("actions")).toArray();
    if (array.size() > kMaxActions) {
        m_actions.clear();
        m_storageValid = false;
        m_errorText = tr("Troppi comandi personali nel file di configurazione.");
        emit actionsChanged();
        emit stateChanged();
        return;
    }

    QVariantList loaded;
    QSet<QString> ids;
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            m_storageValid = false;
            break;
        }

        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("id")).isString()
            || !object.value(QStringLiteral("name")).isString()
            || !object.value(QStringLiteral("description")).isString()
            || !object.value(QStringLiteral("script")).isString()
            || !object.value(QStringLiteral("confirm")).isBool()
            || (object.contains(QStringLiteral("quick"))
                && !object.value(QStringLiteral("quick")).isBool())
            || (object.contains(QStringLiteral("icon"))
                && !object.value(QStringLiteral("icon")).isString())) {
            m_storageValid = false;
            break;
        }

        const QString id = object.value(QStringLiteral("id")).toString();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QString description = object.value(QStringLiteral("description")).toString();
        const QString script = object.value(QStringLiteral("script")).toString();
        QString validationError;
        if (!validId(id) || ids.contains(id)
            || !validateAction(name, description, script, &validationError)) {
            m_storageValid = false;
            break;
        }

        ids.insert(id);
        QVariantMap action;
        action.insert(QStringLiteral("id"), id);
        action.insert(QStringLiteral("name"), name);
        action.insert(QStringLiteral("description"), description);
        action.insert(QStringLiteral("script"), script);
        action.insert(QStringLiteral("confirm"), object.value(QStringLiteral("confirm")).toBool());
        action.insert(QStringLiteral("quick"), object.value(QStringLiteral("quick")).toBool(false));
        action.insert(QStringLiteral("icon"),
                      normalizedActionIcon(object.value(QStringLiteral("icon")).toString()));
        loaded.append(action);
    }

    if (!m_storageValid) {
        m_actions.clear();
        m_errorText = tr("Il file dei comandi personali contiene dati non validi o ID duplicati e non verrà sovrascritto.");
    } else {
        m_actions = loaded;
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
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
        object.insert(QStringLiteral("quick"), action.value(QStringLiteral("quick")).toBool());
        object.insert(QStringLiteral("icon"),
                      normalizedActionIcon(action.value(QStringLiteral("icon")).toString()));
        array.append(object);
    }

    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(QStringLiteral("actions"), array);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
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
    QString iconName = kDefaultActionIcon;
    const int existingIndex = indexForId(id.trimmed());
    if (existingIndex >= 0) {
        iconName = normalizedActionIcon(
            m_actions.at(existingIndex).toMap().value(QStringLiteral("icon")).toString());
    }
    return saveAction(id, name, description, script, confirmBeforeRun, iconName);
}

bool CustomActionsBackend::saveAction(const QString &id, const QString &name,
                                      const QString &description, const QString &script,
                                      bool confirmBeforeRun, const QString &iconName)
{
    if (m_running || !m_storageValid)
        return false;

    QString validationError;
    if (!validateAction(name, description, script, &validationError)) {
        m_errorText = validationError;
        emit stateChanged();
        return false;
    }

    const QString requestedId = id.trimmed();
    int index = -1;
    QString actionId;
    if (requestedId.isEmpty()) {
        if (m_actions.size() >= kMaxActions) {
            m_errorText = tr("Limite di %1 comandi personali raggiunto.").arg(kMaxActions);
            emit stateChanged();
            return false;
        }
        actionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else {
        if (!validId(requestedId) || (index = indexForId(requestedId)) < 0) {
            m_errorText = tr("Comando personale da modificare non trovato.");
            emit stateChanged();
            return false;
        }
        actionId = requestedId;
    }

    const bool wasQuick = index >= 0
        && m_actions.at(index).toMap().value(QStringLiteral("quick")).toBool();

    QVariantMap action;
    action.insert(QStringLiteral("id"), actionId);
    action.insert(QStringLiteral("name"), name.trimmed());
    action.insert(QStringLiteral("description"), description.trimmed());
    action.insert(QStringLiteral("script"), script);
    action.insert(QStringLiteral("confirm"), confirmBeforeRun);
    action.insert(QStringLiteral("quick"), wasQuick);
    action.insert(QStringLiteral("icon"), normalizedActionIcon(iconName));

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

QStringList CustomActionsBackend::actionIcons() const
{
    return allowedActionIcons();
}

QVariantList CustomActionsBackend::quickActions() const
{
    QVariantList result;
    for (const QVariant &value : m_actions) {
        const QVariantMap action = value.toMap();
        if (!action.value(QStringLiteral("quick")).toBool())
            continue;
        result.append(action);
        if (result.size() >= kMaxQuickActions)
            break;
    }
    return result;
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

bool CustomActionsBackend::setQuickAction(const QString &id, bool quick)
{
    if (m_running || !m_storageValid)
        return false;
    const int index = indexForId(id);
    if (index < 0)
        return false;

    if (quick) {
        qsizetype count = 0;
        for (const QVariant &value : m_actions)
            count += value.toMap().value(QStringLiteral("quick")).toBool() ? 1 : 0;
        if (count >= kMaxQuickActions
            && !m_actions.at(index).toMap().value(QStringLiteral("quick")).toBool()) {
            m_errorText = tr("Puoi assegnare al massimo %1 azioni rapide.").arg(kMaxQuickActions);
            emit stateChanged();
            return false;
        }
    }

    const QVariantList previous = m_actions;
    QVariantMap action = m_actions.at(index).toMap();
    action.insert(QStringLiteral("quick"), quick);
    m_actions[index] = action;
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
    if (!QFileInfo(kShell).isExecutable()) {
        m_errorText = tr("/usr/bin/bash non è disponibile.");
        emit stateChanged();
        return false;
    }

    const QVariantMap action = m_actions.at(index).toMap();
    auto *runner = new ProcessRunner(this);
    m_runner = runner;
    m_running = true;
    m_runningId = id;
    m_output.clear();
    m_errorText.clear();
    m_resultState = QStringLiteral("running");
    emit stateChanged();

    connect(runner, &ProcessRunner::outputReady, this, [this, runner](const QByteArray &data) {
        if (runner == m_runner)
            appendOutput(data);
    });
    connect(runner, &ProcessRunner::finished, this,
            [this, runner](ProcessRunner::Outcome outcome, int exitCode,
                           const QByteArray &, const QByteArray &, const QString &error) {
        if (runner != m_runner)
            return;
        m_runner = nullptr;
        runner->deleteLater();
        switch (outcome) {
        case ProcessRunner::Success:
            finish(QStringLiteral("success"));
            break;
        case ProcessRunner::TimedOut:
            finish(QStringLiteral("timeout"), tr("Tempo massimo di 30 minuti superato."));
            break;
        case ProcessRunner::Cancelled:
            finish(QStringLiteral("cancelled"), tr("Comando annullato."));
            break;
        case ProcessRunner::FailedToStart:
            finish(QStringLiteral("error"), tr("Impossibile avviare lo script: %1").arg(error));
            break;
        case ProcessRunner::ExitError:
            finish(QStringLiteral("error"), tr("Comando terminato con codice %1.").arg(exitCode));
            break;
        }
    });

    ProcessRunner::Options options;
    options.program = kShell;
    options.arguments = {QStringLiteral("--noprofile"), QStringLiteral("--norc"),
                         QStringLiteral("-c"), action.value(QStringLiteral("script")).toString()};
    options.workingDirectory = QDir::homePath();
    options.timeoutMs = kActionTimeoutMs;
    options.maxOutputBytes = 128 * 1024;
    options.mergedChannels = true;
    options.processGroup = true;
    if (!runner->start(options)) {
        m_runner = nullptr;
        runner->deleteLater();
        finish(QStringLiteral("error"), tr("Impossibile inizializzare lo script."));
        return false;
    }
    return true;
}

QString CustomActionsBackend::combineTextFiles(const QUrl &folderUrl)
{
    auto fail = [this](const QString &message) {
        m_errorText = message;
        emit stateChanged();
        return QString();
    };

    if (m_running)
        return fail(tr("Attendi la fine del comando personale in esecuzione."));

    if (!folderUrl.isLocalFile())
        return fail(tr("Seleziona una cartella locale."));

    const QFileInfo requested(folderUrl.toLocalFile());
    if (!requested.exists() || !requested.isDir() || !requested.isReadable()
        || requested.isSymLink()) {
        return fail(tr("La cartella selezionata non è valida, leggibile o è un collegamento simbolico."));
    }

    const QString canonicalPath = requested.canonicalFilePath();
    if (canonicalPath.isEmpty())
        return fail(tr("Impossibile risolvere il percorso della cartella selezionata."));

    const QFileInfo folderInfo(canonicalPath);
    const QString folderName = folderInfo.fileName();
    if (folderName.isEmpty())
        return fail(tr("La radice del filesystem non può essere usata per questa operazione."));

    const QString outputPath = QDir(folderInfo.absolutePath())
                                   .filePath(folderName + QStringLiteral("-contenuto.txt"));
    const QFileInfo outputInfo(outputPath);
    if (outputInfo.exists() || outputInfo.isSymLink())
        return fail(tr("Il file di destinazione esiste già: %1").arg(outputPath));

    QDir sourceDir(canonicalPath);
    QFileInfoList entries = sourceDir.entryInfoList(
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot, QDir::NoSort);

    std::sort(entries.begin(), entries.end(),
              [](const QFileInfo &left, const QFileInfo &right) {
        return QString::compare(left.fileName(), right.fileName(), Qt::CaseSensitive) < 0;
    });

    QByteArray combined;
    qsizetype included = 0;

    for (const QFileInfo &entry : entries) {
        if (!entry.isFile() || !entry.isReadable() || entry.isSymLink())
            continue;

        if (entry.size() > kMaxCombinedTextBytes)
            return fail(tr("Il file %1 supera il limite massimo consentito.").arg(entry.fileName()));

        QFile input(entry.absoluteFilePath());
        if (!input.open(QIODevice::ReadOnly))
            continue;

        const QByteArray data = input.readAll();
        if (data.size() > kMaxCombinedTextBytes)
            return fail(tr("Il file %1 supera il limite massimo consentito.").arg(entry.fileName()));

        if (!isUtf8Text(data))
            continue;

        const QByteArray header =
            QByteArrayLiteral("===== ") + entry.fileName().toUtf8() + QByteArrayLiteral(" =====\n");

        qsizetype extra = header.size() + data.size() + 1;
        if (!data.endsWith('\n'))
            ++extra;

        if (combined.size() + extra > kMaxCombinedTextBytes) {
            return fail(tr("I file di testo superano complessivamente il limite di 16 MiB."));
        }

        combined += header;
        combined += data;
        if (!data.endsWith('\n'))
            combined += '\n';
        combined += '\n';
        ++included;
    }

    if (included == 0)
        return fail(tr("Nessun file di testo UTF-8 leggibile trovato nella cartella."));

    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return fail(tr("Impossibile creare il file di destinazione senza sovrascrivere dati esistenti."));

    const qint64 written = output.write(combined);
    const bool flushed = output.flush();
    output.close();

    if (written != combined.size() || !flushed) {
        QFile::remove(outputPath);
        return fail(tr("Scrittura del file di destinazione non riuscita."));
    }

    m_errorText.clear();
    emit stateChanged();
    return outputPath;
}

QString CustomActionsBackend::bashPromptStatus() const
{
    const QString path = QDir::home().filePath(QStringLiteral(".bashrc"));
    return BashPromptConfig::statusId(BashPromptConfig::inspect(path));
}

bool CustomActionsBackend::applyBashPromptPreset(const QString &presetId)
{
    if (m_running) {
        m_errorText = tr("Attendi la fine del comando personale in esecuzione.");
        emit stateChanged();
        return false;
    }

    QString error;
    const QString path = QDir::home().filePath(QStringLiteral(".bashrc"));
    if (!BashPromptConfig::applyPreset(path, presetId, &error)) {
        m_errorText = error;
        emit stateChanged();
        return false;
    }

    m_errorText.clear();
    emit stateChanged();
    return true;
}

bool CustomActionsBackend::resetBashPrompt()
{
    if (m_running) {
        m_errorText = tr("Attendi la fine del comando personale in esecuzione.");
        emit stateChanged();
        return false;
    }

    QString error;
    const QString path = QDir::home().filePath(QStringLiteral(".bashrc"));
    if (!BashPromptConfig::removeManagedBlock(path, &error)) {
        m_errorText = error;
        emit stateChanged();
        return false;
    }

    m_errorText.clear();
    emit stateChanged();
    return true;
}


bool CustomActionsBackend::setTemporaryEnergyProfile(const QString &profileId)
{
    static const QSet<QString> allowed = {
        QStringLiteral("standard"),
        QStringLiteral("60"),
        QStringLiteral("180")
    };

    if (!allowed.contains(profileId)) {
        m_errorText = tr("Profilo energia temporaneo non valido.");
        emit stateChanged();
        return false;
    }

    QString helper = qEnvironmentVariable("KRISCC_ENERGY_HELPER");
    if (helper.isEmpty()) {
        const QString sibling =
            QDir(QCoreApplication::applicationDirPath())
                .filePath(QStringLiteral("energy-profile"));
        if (QFileInfo(sibling).isExecutable())
            helper = sibling;
        else
            helper = QStringLiteral("/usr/libexec/kriscc/energy-profile");
    }

    if (!QFileInfo(helper).isExecutable()) {
        m_errorText = tr("Helper dei profili energia non disponibile.");
        emit stateChanged();
        return false;
    }

    QProcess process;
    process.setProgram(helper);
    process.setArguments({QStringLiteral("--apply"), profileId});
    process.start();

    if (!process.waitForStarted(3000)) {
        m_errorText = tr("Impossibile avviare il profilo energia temporaneo.");
        emit stateChanged();
        return false;
    }

    if (!process.waitForFinished(10000)) {
        process.kill();
        process.waitForFinished(1000);
        m_errorText = tr("Timeout durante l'applicazione del profilo energia.");
        emit stateChanged();
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        m_errorText = tr("Impossibile applicare il profilo energia temporaneo.");
        emit stateChanged();
        return false;
    }

    m_errorText.clear();
    emit stateChanged();
    return true;
}

void CustomActionsBackend::appendOutput(const QByteArray &data)
{
    if (data.isEmpty())
        return;
    m_output += QString::fromUtf8(data);
    constexpr qsizetype maxOutput = 128 * 1024;
    if (m_output.size() > maxOutput)
        m_output = tr("[output precedente omesso]\n") + m_output.right(maxOutput);
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
    if (m_runner && m_running)
        m_runner->cancel();
}
