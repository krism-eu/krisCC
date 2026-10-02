#include <QCommandLineParser>
#include <QGuiApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QProcess>
#include <QThread>

#include <KIdleTime>

namespace {
const QString kUnit = QStringLiteral("kriscc-energy-profile.service");
const QString kSystemctl = QStringLiteral("/usr/bin/systemctl");
const QString kSystemdRun = QStringLiteral("/usr/bin/systemd-run");
const QString kKscreenDoctor = QStringLiteral("/usr/bin/kscreen-doctor");

bool validProfile(const QString &profile)
{
    return profile == QStringLiteral("standard")
        || profile == QStringLiteral("60")
        || profile == QStringLiteral("180");
}

int applyProfile(const QString &profile)
{
    if (!validProfile(profile))
        return 2;

    QProcess::execute(kSystemctl,
                      {QStringLiteral("--user"), QStringLiteral("stop"), kUnit});

    if (profile == QStringLiteral("standard"))
        return 0;

    const int displaySeconds = profile == QStringLiteral("60") ? 5 * 60 : 3 * 60;
    const int suspendSeconds = profile == QStringLiteral("60") ? 60 * 60 : 180 * 60;

    const QString self = QCoreApplication::applicationFilePath();
    const QStringList args = {
        QStringLiteral("--user"),
        QStringLiteral("--quiet"),
        QStringLiteral("--unit=kriscc-energy-profile"),
        QStringLiteral("--collect"),
        QStringLiteral("--property=Restart=no"),
        self,
        QStringLiteral("--run"),
        QStringLiteral("--display-seconds=%1").arg(displaySeconds),
        QStringLiteral("--suspend-seconds=%1").arg(suspendSeconds)
    };

    for (int attempt = 0; attempt < 5; ++attempt) {
        const int rc = QProcess::execute(kSystemdRun, args);
        if (rc == 0)
            return 0;
        QThread::msleep(150);
    }

    return 3;
}

int runProfile(QGuiApplication &app, int displaySeconds, int suspendSeconds)
{
    if (displaySeconds <= 0 || suspendSeconds <= displaySeconds)
        return 2;

    QDBusInterface policy(
        QStringLiteral("org.kde.Solid.PowerManagement"),
        QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent"),
        QStringLiteral("org.kde.Solid.PowerManagement.PolicyAgent"),
        QDBusConnection::sessionBus());

    if (!policy.isValid())
        return 4;

    const QDBusReply<uint> inhibition = policy.call(
        QStringLiteral("AddInhibition"),
        uint(1),
        QStringLiteral("krisCC"),
        QStringLiteral("Profilo energia temporaneo krisCC"));

    if (!inhibition.isValid())
        return 5;

    const uint inhibitionCookie = inhibition.value();

    KIdleTime *idle = KIdleTime::instance();
    const int displayId = idle->addIdleTimeout(displaySeconds * 1000);
    const int suspendId = idle->addIdleTimeout(suspendSeconds * 1000);

    if (displayId <= 0 || suspendId <= 0) {
        policy.call(QStringLiteral("ReleaseInhibition"), inhibitionCookie);
        return 6;
    }

    QObject::connect(idle, &KIdleTime::timeoutReached, &app,
                     [idle, displayId, suspendId](int identifier, int) {
        if (identifier == displayId) {
            QProcess::startDetached(
                kKscreenDoctor,
                {QStringLiteral("--dpms"), QStringLiteral("off")});
            idle->catchNextResumeEvent();
            return;
        }

        if (identifier == suspendId) {
            QDBusInterface login1(
                QStringLiteral("org.freedesktop.login1"),
                QStringLiteral("/org/freedesktop/login1"),
                QStringLiteral("org.freedesktop.login1.Manager"),
                QDBusConnection::systemBus());
            if (login1.isValid())
                login1.asyncCall(QStringLiteral("Suspend"), false);
            idle->catchNextResumeEvent();
        }
    });

    QObject::connect(idle, &KIdleTime::resumingFromIdle, &app, [] {
        QProcess::startDetached(
            kKscreenDoctor,
            {QStringLiteral("--dpms"), QStringLiteral("on")});
    });

    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app,
                     [inhibitionCookie] {
        QDBusInterface policy(
            QStringLiteral("org.kde.Solid.PowerManagement"),
            QStringLiteral("/org/kde/Solid/PowerManagement/PolicyAgent"),
            QStringLiteral("org.kde.Solid.PowerManagement.PolicyAgent"),
            QDBusConnection::sessionBus());
        if (policy.isValid())
            policy.call(QStringLiteral("ReleaseInhibition"), inhibitionCookie);
    });

    return app.exec();
}
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QGuiApplication::setApplicationName(QStringLiteral("kriscc-energy-profile"));

    QCommandLineParser parser;
    parser.addHelpOption();

    QCommandLineOption applyOption(
        QStringLiteral("apply"),
        QStringLiteral("Apply standard, 60 or 180."),
        QStringLiteral("profile"));
    QCommandLineOption runOption(QStringLiteral("run"),
                                 QStringLiteral("Run the transient idle monitor."));
    QCommandLineOption displayOption(
        QStringLiteral("display-seconds"),
        QStringLiteral("Display-off idle timeout."),
        QStringLiteral("seconds"));
    QCommandLineOption suspendOption(
        QStringLiteral("suspend-seconds"),
        QStringLiteral("Suspend idle timeout."),
        QStringLiteral("seconds"));

    parser.addOption(applyOption);
    parser.addOption(runOption);
    parser.addOption(displayOption);
    parser.addOption(suspendOption);
    parser.process(app);

    if (parser.isSet(applyOption))
        return applyProfile(parser.value(applyOption));

    if (parser.isSet(runOption)) {
        bool displayOk = false;
        bool suspendOk = false;
        const int displaySeconds = parser.value(displayOption).toInt(&displayOk);
        const int suspendSeconds = parser.value(suspendOption).toInt(&suspendOk);
        if (!displayOk || !suspendOk)
            return 2;
        return runProfile(app, displaySeconds, suspendSeconds);
    }

    parser.showHelp(2);
}
