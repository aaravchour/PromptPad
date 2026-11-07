#include "AppIdentity.h"
#include "launcher/Ipc.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QFont>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <utility>

#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif

namespace {
// macOS hands files from Finder (double-click, Open With, Dock drop) to the
// running app as QFileOpenEvents instead of command-line arguments. Overriding
// event() sees only events sent to the application object, so ordinary input
// does not pay for this. Files that arrive before the window exists are queued.
class PromptPadApplication final : public QApplication {
public:
    using QApplication::QApplication;

    void setFileHandler(std::function<void(const QString &)> handler) {
        handler_ = std::move(handler);
        const QStringList queued = std::exchange(pending_, {});
        for (const QString &path : queued) handler_(path);
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::FileOpen) {
            const QString path = static_cast<QFileOpenEvent *>(event)->file();
            if (!path.isEmpty()) {
                if (handler_) handler_(path);
                else pending_ << path;
            }
            return true;
        }
        return QApplication::event(event);
    }

private:
    std::function<void(const QString &)> handler_;
    QStringList pending_;
};
}

int main(int argc, char **argv) {
#ifdef Q_OS_UNIX
    umask(0077);
#endif
    PromptPadApplication app(argc, argv);
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

    QStringList args = app.arguments();
    // Older macOS releases add a process serial number argument on Finder launches.
    args.erase(std::remove_if(args.begin() + 1, args.end(), [](const QString &arg) {
        return arg.startsWith(QStringLiteral("-psn_"));
    }), args.end());
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
    app.setFileHandler([&window](const QString &path) { window.openFromSystem(path); });
    if (request.command != QStringLiteral("wait") && QGuiApplication::platformName() == QStringLiteral("cocoa")
        && !qEnvironmentVariableIsSet("PROMPTPAD_PROFILE_DIR"))
        QTimer::singleShot(800, &window, [&window] { window.offerDefaultMarkdownHandler(); });
    return app.exec();
}
