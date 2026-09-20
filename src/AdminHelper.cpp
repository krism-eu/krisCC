#include <QByteArray>
#include <QCoreApplication>
#include <QList>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>
#include <QUrl>

#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace {

bool validRepositoryId(const QString &value)
{
    static const QRegularExpression pattern(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._:-]{0,127}$"));
    return pattern.match(value).hasMatch();
}

bool validRepositoryUrl(const QString &value)
{
    if (value.isEmpty() || value.size() > 2048
        || value.contains(QRegularExpression(QStringLiteral("[\\s\\x00-\\x1f]"))))
        return false;
    const QUrl url(value);
    return url.isValid()
        && url.scheme() == QStringLiteral("https")
        && !url.host().isEmpty()
        && url.userInfo().isEmpty();
}

bool validBootToken(const QString &value)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9A-Fa-f]{4}$"));
    return pattern.match(value).hasMatch();
}

bool validGrubEntry(const QString &value)
{
    if (value.isEmpty() || value.size() > 256 || value.startsWith(QLatin1Char('-')))
        return false;
    for (const QChar ch : value) {
        if (ch.isNull() || ch.unicode() < 0x20 || ch.unicode() == 0x7f)
            return false;
    }
    return true;
}

[[noreturn]] void execProgram(const QByteArray &program, const QStringList &arguments)
{
    QList<QByteArray> encoded;
    encoded.reserve(arguments.size() + 1);
    encoded.append(program);
    for (const QString &argument : arguments)
        encoded.append(argument.toLocal8Bit());

    QList<char *> argv;
    argv.reserve(encoded.size() + 1);
    for (QByteArray &argument : encoded)
        argv.append(argument.data());
    argv.append(nullptr);

    ::execv(program.constData(), argv.data());
    QTextStream errorStream(stderr);
    errorStream << "kriscc-admin: exec fallita per "
                << QString::fromLocal8Bit(program)
                << ": " << std::strerror(errno) << '\n';
    errorStream.flush();
    _exit(126);
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

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        err << "kriscc-admin: operazione mancante.\n";
        return 64;
    }

    const QString operation = args.at(1);

    if (operation == QStringLiteral("bootc-check") && args.size() == 2)
        execProgram("/usr/bin/bootc", {QStringLiteral("upgrade"), QStringLiteral("--check")});
    if (operation == QStringLiteral("bootc-download") && args.size() == 2)
        execProgram("/usr/bin/bootc", {QStringLiteral("upgrade"), QStringLiteral("--download-only")});
    if (operation == QStringLiteral("bootc-prepare") && args.size() == 2)
        execProgram("/usr/bin/bootc", {QStringLiteral("upgrade")});
    if (operation == QStringLiteral("bootc-apply-downloaded") && args.size() == 2)
        execProgram("/usr/bin/bootc",
                    {QStringLiteral("upgrade"), QStringLiteral("--from-downloaded"),
                     QStringLiteral("--apply")});

    if ((operation == QStringLiteral("repo-enable")
         || operation == QStringLiteral("repo-disable"))
        && args.size() == 3 && validRepositoryId(args.at(2))) {
        execProgram("/usr/bin/dnf5",
                    {QStringLiteral("config-manager"),
                     operation == QStringLiteral("repo-enable")
                         ? QStringLiteral("enable") : QStringLiteral("disable"),
                     args.at(2)});
    }

    if (operation == QStringLiteral("repo-add") && args.size() == 3
        && validRepositoryUrl(args.at(2))) {
        execProgram("/usr/bin/dnf5",
                    {QStringLiteral("config-manager"), QStringLiteral("addrepo"),
                     QStringLiteral("--from-repofile=") + args.at(2)});
    }

    if (operation == QStringLiteral("boot-next-uefi") && args.size() == 3
        && validBootToken(args.at(2))) {
        execProgram("/usr/bin/efibootmgr", {QStringLiteral("-n"), args.at(2).toUpper()});
    }

    if (operation == QStringLiteral("boot-next-grub") && args.size() == 3
        && validGrubEntry(args.at(2))) {
        execProgram("/usr/bin/grub2-reboot", {args.at(2)});
    }

    err << "kriscc-admin: operazione o argomenti non consentiti.\n";
    return 64;
}
