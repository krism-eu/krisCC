#include "PolkitHelper.h"

#include "OperationLog.h"
#include "AdminPolicy.h"

#include <QFileInfo>

namespace {
constexpr qsizetype kMaxOutput = 256 * 1024;
constexpr qsizetype kMaxLineBuffer = 64 * 1024;
}

PolkitHelper::PolkitHelper(QObject *parent)
    : QObject(parent)
{
}

void PolkitHelper::execute(const QString &program, const QStringList &args)
{
    if (m_running) {
        emit finished(false, tr("Un'altra operazione privilegiata è già in corso."));
        return;
    }

    if (!isPrivilegedInvocationAllowed(program, args)) {
        emit finished(false, tr("Operazione privilegiata non consentita."));
        return;
    }

    m_running = true;
    m_allOutput.clear();
    m_lineBuffer.clear();
    m_program = program;
    m_args = args;
    emit runningChanged();

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &PolkitHelper::onReadyRead);
    connect(m_process, &QProcess::errorOccurred, this, &PolkitHelper::onProcessError);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PolkitHelper::onProcessFinished);

    QStringList fullArgs;
    fullArgs << program << args;
    m_process->start(QStringLiteral("/usr/bin/pkexec"), fullArgs);
}

void PolkitHelper::onReadyRead()
{
    if (m_process)
        consumeOutput(m_process->readAllStandardOutput());
}

void PolkitHelper::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;

    consumeOutput(m_process->readAllStandardOutput(), true);
    const bool success = status == QProcess::NormalExit && exitCode == 0;
    QString output = m_allOutput.trimmed();

    if (!success) {
        if (status == QProcess::NormalExit && exitCode == 124)
            output = tr("Tempo massimo superato: l'helper amministrativo ha interrotto l'operazione.");
        else if (status == QProcess::NormalExit && exitCode == 125)
            output = tr("L'helper amministrativo non è riuscito ad avviare o completare il comando.");
        else if (status == QProcess::NormalExit && exitCode == 126)
            output = tr("Autenticazione annullata dall'utente.");
        else if (status == QProcess::NormalExit && exitCode == 127)
            output = tr("Autenticazione amministrativa non disponibile o non autorizzata.");
        else if (output.isEmpty())
            output = tr("Operazione terminata con codice %1.").arg(exitCode);
    }

    OperationLog::append(QStringLiteral("Amministrazione"), operationLabel(),
                         success ? QStringLiteral("success")
                                 : (exitCode == 124 ? QStringLiteral("timeout")
                                                    : QStringLiteral("error")));

    m_process->deleteLater();
    m_process = nullptr;
    m_running = false;
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(success, output);
}

void PolkitHelper::onProcessError(QProcess::ProcessError error)
{
    if (!m_process || error != QProcess::FailedToStart)
        return;
    finishWithError(tr("Impossibile avviare pkexec: %1").arg(m_process->errorString()));
}

bool PolkitHelper::isPrivilegedInvocationAllowed(const QString &program,
                                                  const QStringList &args) const
{
    return program == QStringLiteral("/usr/libexec/kriscc/admin")
        && AdminPolicy::resolve(args).has_value();
}

QString PolkitHelper::operationLabel() const
{
    if (m_program == QStringLiteral("/usr/libexec/kriscc/admin"))
        return m_args.isEmpty() ? QStringLiteral("admin") : m_args.at(0);
    return QFileInfo(m_program).fileName();
}

void PolkitHelper::consumeOutput(const QByteArray &data, bool flushPartial)
{
    if (!data.isEmpty()) {
        m_allOutput += QString::fromUtf8(data);
        if (m_allOutput.size() > kMaxOutput)
            m_allOutput = tr("[output precedente omesso]\n") + m_allOutput.right(kMaxOutput);

        m_lineBuffer += data;
        if (m_lineBuffer.size() > kMaxLineBuffer)
            m_lineBuffer = QByteArray("[riga troppo lunga: inizio omesso]\n")
                         + m_lineBuffer.right(kMaxLineBuffer);
    }

    qsizetype newline = -1;
    while ((newline = m_lineBuffer.indexOf('\n')) >= 0) {
        QByteArray lineData = m_lineBuffer.left(newline);
        m_lineBuffer.remove(0, newline + 1);
        if (!lineData.isEmpty() && lineData.endsWith('\r'))
            lineData.chop(1);
        if (!lineData.isEmpty())
            emit line(QString::fromUtf8(lineData));
    }

    if (flushPartial && !m_lineBuffer.isEmpty()) {
        emit line(QString::fromUtf8(m_lineBuffer));
        m_lineBuffer.clear();
    }
}

void PolkitHelper::finishWithError(const QString &message)
{
    if (!operationLabel().trimmed().isEmpty())
        OperationLog::append(QStringLiteral("Amministrazione"), operationLabel(),
                             QStringLiteral("error"));

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_running = false;
    m_lineBuffer.clear();
    m_allOutput.clear();
    m_program.clear();
    m_args.clear();
    emit runningChanged();
    emit finished(false, message);
}