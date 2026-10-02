#include <QtTest>

#include "PolkitHelper.h"

class PolkitHelperMessageTest final : public QObject
{
    Q_OBJECT

private slots:
    void dnfLockGetsFriendlyMessage()
    {
        PolkitHelper helper;

        const QString raw =
            QStringLiteral(
                "rk: Another package transaction holds the DNF system lock");

        QCOMPARE(
            helper.userFacingOutput(raw),
            QStringLiteral(
                "Un'altra operazione sui pacchetti è in corso. "
                "Attendi che termini e riprova."));
    }

    void unrelatedErrorsArePreserved()
    {
        PolkitHelper helper;
        const QString raw =
            QStringLiteral("Errore amministrativo di prova.");

        QCOMPARE(helper.userFacingOutput(raw), raw);
    }

    void otherLinesArePreserved()
    {
        PolkitHelper helper;

        const QString raw =
            QStringLiteral(
                "prima\n"
                "rk: Another package transaction holds the DNF system lock\n"
                "dopo");

        QCOMPARE(
            helper.userFacingOutput(raw),
            QStringLiteral(
                "prima\n"
                "Un'altra operazione sui pacchetti è in corso. "
                "Attendi che termini e riprova.\n"
                "dopo"));
    }
};

QTEST_APPLESS_MAIN(PolkitHelperMessageTest)
#include "test_polkit_helper.moc"
