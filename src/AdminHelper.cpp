#include "AdminPolicy.h"
#include "PrivilegedProcessRunner.h"

#include <QCoreApplication>
#include <QTextStream>

#include <unistd.h>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);

    if (::geteuid() != 0) {
        err << "kriscc-admin: l'helper deve essere eseguito come root tramite Polkit.\n";
        return 77;
    }

    AdminCommand command;
    if (!buildAdminCommand(QCoreApplication::arguments(), &command)) {
        err << "kriscc-admin: operazione o argomenti non consentiti.\n";
        return 64;
    }

    return runPrivilegedCommand(command.program, command.arguments, command.timeoutMs);
}
