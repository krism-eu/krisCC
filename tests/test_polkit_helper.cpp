#include <QtTest>

#include <QSignalSpy>

#include "PolkitHelper.h"

class PolkitHelperMessageTest final : public QObject
{
    Q_OBJECT

    struct Result {
        bool valid = false;
        bool success = false;
        QString output;
    };

    Result simulateFinished(int exitCode, const QString &rawOutput)
    {
        PolkitHelper helper;
        QSignalSpy spy(&helper, &PolkitHelper::finished);
        helper.m_running = true;
        helper.m_program = QStringLiteral("/usr/libexec/kriscc/admin");
        helper.m_args = {QStringLiteral("rk-sync")};
        helper.m_allOutput = rawOutput;
        helper.m_process = new QProcess(&helper);

        helper.onProcessFinished(exitCode, QProcess::NormalExit);
        if (spy.size() != 1)
            return {};

        const QList<QVariant> values = spy.takeFirst();
        return {true, values.at(0).toBool(), values.at(1).toString()};
    }

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

    void childExitCodesAreNotMisclassified()
    {
        for (const int code : {124, 125, 126, 127}) {
            const QString childText = QStringLiteral("child-error-%1").arg(code);
            const Result result = simulateFinished(
                code,
                childText + QStringLiteral("\nKRISCC_ADMIN_STATUS child %1\n").arg(code));
            QVERIFY(result.valid);
            QVERIFY(!result.success);
            QCOMPARE(result.output, childText);
        }
    }

    void childExitWithoutTextStaysAChildError()
    {
        const Result result = simulateFinished(
            127, QStringLiteral("KRISCC_ADMIN_STATUS child 127\n"));
        QVERIFY(result.valid);
        QVERIFY(!result.success);
        QCOMPARE(result.output,
                 QStringLiteral("Comando amministrativo terminato con codice 127."));
    }

    void wrapperStatusesAreDistinct()
    {
        const Result timeout = simulateFinished(
            124, QStringLiteral("KRISCC_ADMIN_STATUS timeout\n"));
        QVERIFY(timeout.valid);
        QCOMPARE(timeout.output,
                 QStringLiteral("Tempo massimo superato: l'helper amministrativo ha interrotto l'operazione."));

        const Result failedStart = simulateFinished(
            125, QStringLiteral("KRISCC_ADMIN_STATUS failed-to-start\n"));
        QVERIFY(failedStart.valid);
        QCOMPARE(failedStart.output,
                 QStringLiteral("L'helper amministrativo non è riuscito ad avviare il comando."));

        const Result descendants = simulateFinished(
            125, QStringLiteral("KRISCC_ADMIN_STATUS descendants-alive\n"));
        QVERIFY(descendants.valid);
        QCOMPARE(descendants.output,
                 QStringLiteral("Il comando amministrativo ha lasciato processi discendenti attivi ed è stato interrotto."));

        const Result crashed = simulateFinished(
            125, QStringLiteral("KRISCC_ADMIN_STATUS crashed\n"));
        QVERIFY(crashed.valid);
        QCOMPARE(crashed.output,
                 QStringLiteral("Il comando amministrativo è terminato in modo anomalo."));
    }

    void pkexecCodesRequireNoAdminMarker()
    {
        const Result cancelled = simulateFinished(126, QString());
        QVERIFY(cancelled.valid);
        QCOMPARE(cancelled.output,
                 QStringLiteral("Autenticazione annullata dall'utente."));

        const Result denied = simulateFinished(127, QString());
        QVERIFY(denied.valid);
        QCOMPARE(denied.output,
                 QStringLiteral("Autorizzazione amministrativa non ottenuta oppure errore di pkexec."));

        const Result unmarked124 = simulateFinished(124, QString());
        QVERIFY(unmarked124.valid);
        QCOMPARE(unmarked124.output,
                 QStringLiteral("Operazione terminata con codice 124."));
    }

    void protocolMarkersAreNotStreamedToUi()
    {
        PolkitHelper helper;
        QSignalSpy lineSpy(&helper, &PolkitHelper::line);
        helper.consumeOutput(
            QByteArray("visible\nKRISCC_ADMIN_STATUS timeout\n"));
        QCOMPARE(lineSpy.size(), 1);
        QCOMPARE(lineSpy.at(0).at(0).toString(), QStringLiteral("visible"));
    }
};

QTEST_APPLESS_MAIN(PolkitHelperMessageTest)
#include "test_polkit_helper.moc"
