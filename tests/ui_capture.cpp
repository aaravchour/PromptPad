#include "editor/PromptEditor.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"
#include "ui/LibraryPanel.h"
#include "Scintilla.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QMenu>
#include <QListWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (argc < 4 || argc > 6) return 2;
    QCoreApplication::setOrganizationName(QStringLiteral("PromptPadTest"));
    QCoreApplication::setApplicationName(QStringLiteral("PromptPadCapture"));
    QTemporaryDir profile;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile.path());
    QSettings().setValue(QStringLiteral("appearance/mode"), QString::fromUtf8(argv[2]));
    Store store;
    QString error;
    if (!store.open(profile.path(), &error)) return 3;
    const QString view = argc >= 5 ? QString::fromUtf8(argv[4]) : QStringLiteral("editor");
    if (view.startsWith(QStringLiteral("library"))) {
        for (const FolderRecord &folder : {FolderRecord{QStringLiteral("coding"), QStringLiteral("Coding")},
                                           FolderRecord{QStringLiteral("writing"), QStringLiteral("Writing")},
                                           FolderRecord{QStringLiteral("personal"), QStringLiteral("Personal")}})
            store.saveFolder(folder, &error);
        DraftRecord one;
        one.id = QStringLiteral("sample-one"); one.title = QStringLiteral("Refactor the sign-in flow");
        one.folderId = QStringLiteral("coding"); one.content = "# Refactor the sign-in flow\n";
        one.modified = false; one.open = true; store.saveDraft(one, &error);
        DraftRecord two;
        two.id = QStringLiteral("sample-two"); two.title = QStringLiteral("Review a pull request");
        two.folderId = QStringLiteral("coding"); two.content = "# Review a pull request\n";
        two.modified = true; store.saveDraft(two, &error);
        store.saveTemplate({QStringLiteral("template-one"), QStringLiteral("Coding task"),
            QStringLiteral("coding"), "# Coding task\n", 0}, &error);
        store.saveTemplate({QStringLiteral("template-two"), QStringLiteral("Bug report"),
            QStringLiteral("coding"), "# Bug report\n", 0}, &error);
        store.saveTemplate({QStringLiteral("template-three"), QStringLiteral("Writing brief"),
            QStringLiteral("writing"), "# Writing brief\n", 0}, &error);
    }
    MainWindow window(&store);
    window.resize(QString::fromUtf8(argv[3]).toInt(), argc >= 6 ? QString::fromUtf8(argv[5]).toInt() : 352);
    const auto *editor = window.findChild<PromptEditor *>();
    const QByteArray sample = "# Refactor the sign-in flow\n\n"
        "Make the sign-in flow simpler, without\nchanging its behaviour.\n\n"
        "## Requirements\n\n"
        "- Keep sessions working.\n"
        "- Add tests for expired links.\n"
        "- Explain what changed.\n";
    if (view != QStringLiteral("blank")) const_cast<PromptEditor *>(editor)->setTextUtf8(sample);
    window.show();
    if (view == QStringLiteral("preview")) window.setViewMode(MainWindow::ViewMode::Preview);
    if (view == QStringLiteral("split")) window.setViewMode(MainWindow::ViewMode::SplitPreview);
    if (view == QStringLiteral("find") || view == QStringLiteral("replace")) {
        const QString label = view == QStringLiteral("find") ? QStringLiteral("Find…") : QStringLiteral("Find and replace…");
        for (auto *action : window.findChildren<QAction *>()) if (action->text() == label) { action->trigger(); break; }
    }
    if (view.startsWith(QStringLiteral("library"))) {
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Open library")) { action->trigger(); break; }
        if (view == QStringLiteral("library-left")) window.setLibraryDockSide(MainWindow::LibraryDockSide::Left);
        if (view == QStringLiteral("library-right")) window.setLibraryDockSide(MainWindow::LibraryDockSide::Right);
        if (view == QStringLiteral("library-top")) window.setLibraryDockSide(MainWindow::LibraryDockSide::Top);
        if (view == QStringLiteral("library-bottom")) window.setLibraryDockSide(MainWindow::LibraryDockSide::Bottom);
    }
    auto capture = [&] {
        QWidget *extra = nullptr;
        if (view.startsWith(QStringLiteral("library"))) extra = window.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        if (view == QStringLiteral("format")) {
            extra = QApplication::activePopupWidget();
            if (!extra) for (auto *widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && widget->objectName() == QStringLiteral("formattingMenu")) { extra = widget; break; }
            if (!extra) extra = window.findChild<QMenu *>(QStringLiteral("formattingMenu"));
        }
        const QPixmap editorImage = window.grab();
        if (!extra || (view != QStringLiteral("format") && !extra->isVisible())) {
            app.exit(editorImage.save(QString::fromUtf8(argv[1])) ? 0 : 4);
            return;
        }
        const QPixmap extraImage = extra->grab();
        const QRect combined = window.geometry().united(extra->geometry());
        QImage image(combined.size() + QSize(32, 32), QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(QStringLiteral("#EAE7EF")));
        QPainter painter(&image);
        painter.drawPixmap(window.pos() - combined.topLeft() + QPoint(16, 16), editorImage);
        painter.drawPixmap(extra->pos() - combined.topLeft() + QPoint(16, 16), extraImage);
        painter.end();
        app.exit(image.save(QString::fromUtf8(argv[1])) ? 0 : 4);
        if (view == QStringLiteral("format")) extra->close();
    };
    if (view == QStringLiteral("format")) {
        const qsizetype start = sample.indexOf("Keep sessions working.");
        if (start >= 0) const_cast<PromptEditor *>(editor)->send(SCI_SETSEL, start, start + qsizetype(22));
        QTimer::singleShot(300, &app, capture);
        QTimer::singleShot(0, &window, [&] {
            for (auto *action : window.findChildren<QAction *>())
                if (action->text() == QStringLiteral("Formatting…")) { action->trigger(); break; }
        });
    } else QTimer::singleShot(900, &app, capture);
    return app.exec();
}
