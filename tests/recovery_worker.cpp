#include "editor/PromptEditor.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QSettings>
#include <QTimer>

#include <cstdlib>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc != 2) return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("PromptPadTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PromptPadRecoveryWorker"));
    const QString profile = QString::fromLocal8Bit(argv[1]);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile);
    Store store;
    QString error;
    if (!store.open(profile, &error)) return 3;
    MainWindow window(&store);
    window.show();
    auto *editor = window.findChild<PromptEditor *>();
    if (!editor) return 4;
    editor->replaceSelection(QString::fromUtf8("# Recovery\nα🧭 {{braces}}\n").toUtf8());
    editor->send(SCI_GOTOPOS, 4);
    QTimer::singleShot(1200, &app, [] { std::_Exit(73); });
    return app.exec();
}
