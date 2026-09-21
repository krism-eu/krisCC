#include "PrivilegedProcessRunner.h"

#include <QElapsedTimer>
#include <QProcess>
#include <QTextStream>

#include <cstdio>
#include <signal.h>
#include <unistd.h>

namespace {

void forwardOutput(QProcess &process)
{
    const QByteArray data = process.readAllStandardOutput();
    if (data.isEmpty())
        return;
    std::fwrite(data.constData(), 1, size_t(data.size()), stdout);
    std::fflush(stdout);
}

}

int runPrivilegedCommand(const QString &program, const QStringList &arguments, int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setChildProcessModifier([] {
        (void)::setsid();
    });

    process.start(program, arguments);
    if (!process.waitForStarted(5000)) {
        QTextStream(stderr) << "kriscc-admin: avvio fallito per "
                            << program << ": " << process.errorString() << '\n';
        return 69;
    }

    QElapsedTimer timer;
    timer.start();

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(200);
        forwardOutput(process);

        if (timeoutMs <= 0 || timer.elapsed() < timeoutMs)
            continue;

        QTextStream(stderr) << "kriscc-admin: timeout dell'operazione privilegiata.\n";
        const qint64 pid = process.processId();
        if (pid > 0)
            (void)::kill(-pid, SIGTERM);
        if (!process.waitForFinished(3000)) {
            if (pid > 0)
                (void)::kill(-pid, SIGKILL);
            process.waitForFinished(3000);
        }
        forwardOutput(process);
        return 124;
    }

    forwardOutput(process);
    if (process.exitStatus() != QProcess::NormalExit)
        return 70;

    const int code = process.exitCode();
    if (code == 126 || code == 127) {
        QTextStream(stderr) << "kriscc-admin: il comando figlio è terminato con codice riservato "
                            << code << ".\n";
        return 70;
    }
    return code;
}
