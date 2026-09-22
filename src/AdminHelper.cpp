#include "AdminPolicy.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTextStream>

#include <signal.h>
#include <unistd.h>

namespace {

int runProgram(const AdminPolicy::Command &command)
{
    QTextStream err(stderr);
    QProcess process;
    process.setProgram(command.program);
    process.setArguments(command.arguments);
    process.setProcessChannelMode(QProcess::ForwardedChannels);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setChildProcessModifier([] {
        (void)::setsid();
    });
    process.start();

    if (!process.waitForStarted(5000)) {
        err << "kriscc-admin: avvio fallito per " << command.program
            << ": " << process.errorString() << '\n';
        return 125;
    }

    if (process.waitForFinished(command.timeoutMs)) {
        if (process.exitStatus() != QProcess::NormalExit) {
            err << "kriscc-admin: processo terminato in modo anomalo: "
                << command.program << '\n';
            return 125;
        }
        return process.exitCode();
    }

    const qint64 pid = process.processId();
    err << "kriscc-admin: timeout per " << command.program << '\n';
    err.flush();

    if (pid > 0)
        (void)::kill(-pid, SIGTERM);
    else
        process.terminate();

    if (!process.waitForFinished(3000)) {
        if (pid > 0)
            (void)::kill(-pid, SIGKILL);
        else
            process.kill();
        process.waitForFinished(3000);
    }
    return 124;
}

}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);

    if (::geteuid() != 0) {
        err << "kriscc-admin: l'helper deve essere eseguito come root tramite Polkit.\n";
        return 77;
    }

    const QStringList request = QCoreApplication::arguments().mid(1);
    const auto command = AdminPolicy::resolve(request);
    if (!command) {
        err << "kriscc-admin: operazione o argomenti non consentiti.\n";
        return 64;
    }
    return runProgram(*command);
}
