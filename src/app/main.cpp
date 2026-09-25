#include "AppIdentity.h"
#include "launcher/Ipc.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <cstdio>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

int main(int argc, char **argv) {
#ifdef Q_OS_UNIX
    umask(0077);
#endif
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(AppIdentity::displayName());
    QCoreApplication::setApplicationName(AppIdentity::displayName());
    const QString profile = qEnvironmentVariableIsSet("PROMPTPAD_PROFILE_DIR")
        ? qEnvironmentVariable("PROMPTPAD_PROFILE_DIR")
        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (qEnvironmentVariableIsSet("PROMPTPAD_PROFILE_DIR")) {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile);
    }
    QFont interfaceFont = QApplication::font();
    interfaceFont.setPixelSize(14);
    app.setFont(interfaceFont);

    const QStringList args = app.arguments();
    Ipc::Request request;
    request.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (args.size() == 1) request.command = QStringLiteral("activate");
    else if (args.size() == 2 && args[1] == QStringLiteral("--new")) request.command = QStringLiteral("new");
    else if (args.size() == 3 && args[1] == QStringLiteral("--wait")) {
        request.command = QStringLiteral("wait"); request.path = args[2];
    } else if (args.size() == 2 && !args[1].startsWith(QStringLiteral("--"))) {
        request.command = QStringLiteral("open"); request.path = args[1];
    } else {
        std::fputs("Usage: promptpad [--new | /path/to/file | --wait /path/to/file]\n", stderr);
        return 2;
    }

    QString error;
    bool connected = false;
    const QString endpoint = Ipc::endpointName(profile);
    const int forwarded = Ipc::forwardIfRunning(endpoint, request, &connected, &error);
    if (connected) {
        if (forwarded != 0 && !error.isEmpty()) std::fprintf(stderr, "%s: %s\n", qPrintable(AppIdentity::displayName()), qPrintable(error));
        return forwarded;
    }

    Store store;
    if (!store.open(profile, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Could not open local drafts"), error);
        return 1;
    }
    MainWindow window(&store, request.command == QStringLiteral("activate"));
    Ipc::Server server(endpoint, &window);
    if (!server.start(&error)) {
        QMessageBox::critical(nullptr, QStringLiteral("Could not start local launcher"), error);
        return 1;
    }
    if (request.command == QStringLiteral("new")) window.newPrompt();
    else if (request.command == QStringLiteral("open") || request.command == QStringLiteral("wait")) {
        const QString id = window.openPath(request.path, request.command == QStringLiteral("wait"));
        if (id.isEmpty()) return 2;
        if (request.command == QStringLiteral("wait")) {
            if (!window.attachWait(id)) return 2;
            QObject::connect(&window, &MainWindow::waitFinished, &app, [&app, id](const QString &completed, bool success) {
                if (completed == id) app.exit(success ? 0 : 2);
            });
        }
    }
    window.show();
    return app.exec();
}
