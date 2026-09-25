#include "editor/PromptEditor.h"
#include "launcher/Ipc.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

class IpcTests : public QObject {
    Q_OBJECT
private slots:
    void twoWaitingCallersAndCollision() {
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
        QString error;
        Store store;
        QVERIFY2(store.open(profile.path(), &error), qPrintable(error));
        MainWindow window(&store, false);
        window.show();
        Ipc::Server server(Ipc::endpointName(profile.path()), &window);
        QVERIFY2(server.start(&error), qPrintable(error));
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("PROMPTPAD_PROFILE_DIR"), profile.path());
        const QString a = profile.filePath(QString::fromUtf8("first draft 🧭.md"));
        const QString b = profile.filePath(QStringLiteral("second draft.md"));
        QProcess first, second, collision;
        for (auto *process : {&first, &second, &collision}) process->setProcessEnvironment(environment);
        first.start(QStringLiteral(PROMPTPAD_EXE), {QStringLiteral("--wait"), a});
        second.start(QStringLiteral(PROMPTPAD_EXE), {QStringLiteral("--wait"), b});
        QVERIFY(first.waitForStarted());
        QVERIFY(second.waitForStarted());
        QTabWidget *tabs = nullptr;
        for (auto *candidate : window.findChildren<QTabWidget *>()) if (candidate->count() == 2 && candidate->tabToolTip(0).contains(profile.path())) tabs = candidate;
        QTRY_VERIFY_WITH_TIMEOUT(tabs != nullptr || ([&] {
            for (auto *candidate : window.findChildren<QTabWidget *>()) if (candidate->count() == 2 && candidate->tabToolTip(0).contains(profile.path())) tabs = candidate;
            return tabs != nullptr;
        }()), 5000);
        collision.start(QStringLiteral(PROMPTPAD_EXE), {QStringLiteral("--wait"), a});
        QVERIFY(collision.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(collision.state() == QProcess::NotRunning, 5000);
        QCOMPARE(collision.exitCode(), 2);
        int indexA = -1;
        for (int i = 0; i < tabs->count(); ++i) if (tabs->tabToolTip(i) == a) indexA = i;
        QVERIFY2(indexA >= 0, "First waiting document tab was not found");
        tabs->setCurrentIndex(indexA);
        auto *editorA = tabs->currentWidget()->findChild<PromptEditor *>();
        QVERIFY(editorA);
        editorA->replaceSelection(QString::fromUtf8("First 🧭\r\n").toUtf8());
        QAction *saveReturn = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Save and return")) saveReturn = action;
        QVERIFY(saveReturn);
        QVERIFY(saveReturn->isVisible());
        QSignalSpy finishedSpy(&window, &MainWindow::waitFinished);
        saveReturn->trigger();
        QCOMPARE(finishedSpy.size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(first.state() == QProcess::NotRunning, 5000);
        QCOMPARE(first.exitCode(), 0);
        QCOMPARE(second.state(), QProcess::Running);
        QFile fileA(a);
        QVERIFY(fileA.open(QIODevice::ReadOnly));
        QCOMPARE(fileA.readAll(), QString::fromUtf8("First 🧭\r\n").toUtf8());
        for (int i = 0; i < tabs->count(); ++i) if (tabs->tabToolTip(i) == b) tabs->setCurrentIndex(i);
        auto *editorB = tabs->currentWidget()->findChild<PromptEditor *>();
        QVERIFY(editorB);
        editorB->replaceSelection("Second\n");
        saveReturn->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(second.state() == QProcess::NotRunning, 5000);
        QCOMPARE(second.exitCode(), 0);
        QFile fileB(b);
        QVERIFY(fileB.open(QIODevice::ReadOnly));
        QCOMPARE(fileB.readAll(), QByteArray("Second\n"));
        window.close();
    }
};

QTEST_MAIN(IpcTests)
#include "ipc_tests.moc"
