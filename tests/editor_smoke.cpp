#include "editor/PromptEditor.h"
#include "documents/Outline.h"
#include "storage/Store.h"
#include "ui/MainWindow.h"
#include "ui/LibraryPanel.h"
#include "Scintilla.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QFile>
#include <QGraphicsOpacityEffect>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextDocument>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

class EditorSmoke : public QObject {
    Q_OBJECT
private slots:
    void exactUtf8AndUndo() {
        PromptEditor editor;
        editor.resize(300, 180);
        editor.show();
        const QByteArray text = QString::fromUtf8("# Café 🧭\n中文 {{braces}}\r\n" ).toUtf8();
        editor.setTextUtf8(text);
        QCOMPARE(editor.textUtf8(), text);
        editor.send(SCI_SETSEL, 0, 2);
        QCOMPARE(editor.selectedUtf8(), QByteArray("# "));
        editor.replaceSelection("## ");
        QVERIFY(editor.send(SCI_CANUNDO));
        editor.send(SCI_UNDO);
        QCOMPARE(editor.textUtf8(), text);
    }
    void wrappedMouseSelectionAndClipboard() {
        PromptEditor editor;
        editor.resize(180, 190);
        editor.setWrap(true);
        const QByteArray text("alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima mike november oscar papa quebec romeo sierra tango");
        editor.setTextUtf8(text);
        editor.show();
        QApplication::processEvents();
        QVERIFY(editor.send(SCI_WRAPCOUNT, 0) > 1);
        const QPoint start(20, 12);
        const QPoint end(100, 115);
        QTest::mousePress(editor.viewport(), Qt::LeftButton, Qt::NoModifier, start);
        QTest::mouseMove(editor.viewport(), end, 20);
        QTest::mouseRelease(editor.viewport(), Qt::LeftButton, Qt::NoModifier, end);
        const auto selectionStart = editor.send(SCI_GETSELECTIONSTART);
        const auto selectionEnd = editor.send(SCI_GETSELECTIONEND);
        QVERIFY(selectionEnd > selectionStart);
        QCOMPARE(editor.selectedUtf8(), text.mid(selectionStart, selectionEnd - selectionStart));
        editor.send(SCI_COPY);
        QCOMPARE(QApplication::clipboard()->text().toUtf8(), editor.selectedUtf8());
    }
    void responsiveWritingWidthPreservesSourceAndSelection() {
        PromptEditor editor;
        editor.resize(1080, 700);
        editor.show();
        QApplication::processEvents();
        const int wideInset = static_cast<int>(editor.send(SCI_GETMARGINLEFT));
        QVERIFY(wideInset >= 90);
        QCOMPARE(editor.send(SCI_GETMARGINRIGHT), sptr_t(wideInset));
        const QByteArray source = QString::fromUtf8("# Café 🧭\nA long prompt with {{literal}} tokens.\n").toUtf8();
        editor.setTextUtf8(source);
        editor.send(SCI_SETSEL, 2, 8);
        const auto anchor = editor.send(SCI_GETANCHOR);
        const auto caret = editor.send(SCI_GETCURRENTPOS);
        editor.resize(640, 700);
        QApplication::processEvents();
        QCOMPARE(editor.send(SCI_GETMARGINLEFT), sptr_t(24));
        QCOMPARE(editor.send(SCI_GETMARGINRIGHT), sptr_t(24));
        QCOMPARE(editor.textUtf8(), source);
        QCOMPARE(editor.send(SCI_GETANCHOR), anchor);
        QCOMPARE(editor.send(SCI_GETCURRENTPOS), caret);
        editor.resize(1080, 700);
        editor.setComfortableWidth(false);
        QCOMPARE(editor.send(SCI_GETMARGINLEFT), sptr_t(24));
        editor.setComfortableWidth(true);
        QCOMPARE(editor.send(SCI_GETMARGINLEFT), sptr_t(wideInset));
    }
    void compactFindAndReplaceRemainsAvailable() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        QAction *findAction = nullptr;
        QAction *replaceAction = nullptr;
        for (auto *action : window.findChildren<QAction *>()) {
            if (action->text() == QStringLiteral("Find…")) findAction = action;
            if (action->text() == QStringLiteral("Find and replace…")) replaceAction = action;
        }
        QVERIFY(findAction);
        QVERIFY(replaceAction);
        findAction->trigger();
        auto *findBar = window.findChild<QWidget *>(QStringLiteral("findBar"));
        auto *replaceRow = window.findChild<QWidget *>(QStringLiteral("replaceRow"));
        auto *optionsRow = window.findChild<QWidget *>(QStringLiteral("findOptionsRow"));
        QVERIFY(findBar);
        QVERIFY(replaceRow);
        QVERIFY(optionsRow);
        QVERIFY(findBar->isVisible());
        QVERIFY(!replaceRow->isVisible());
        QVERIFY(!optionsRow->isVisible());
        QPushButton *options = nullptr;
        for (auto *candidate : window.findChildren<QPushButton *>())
            if (candidate->accessibleName() == QStringLiteral("Find options")) options = candidate;
        QVERIFY(options);
        options->click();
        QVERIFY(optionsRow->isVisible());
        auto *find = window.findChild<QLineEdit *>();
        QVERIFY(find);
        find->setFocus();
        QTest::keyClick(find, Qt::Key_Escape);
        QTRY_VERIFY(!findBar->isVisible());
        replaceAction->trigger();
        QVERIFY(findBar->isVisible());
        QVERIFY(replaceRow->isVisible());
    }
    void writingWidthMenuSwitchesLayout() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.resize(1080, 700);
        window.show();
        QApplication::processEvents();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        QAction *widthAction = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Comfortable writing width")) widthAction = action;
        QVERIFY(widthAction);
        QVERIFY(widthAction->isChecked());
        const auto inset = editor->send(SCI_GETMARGINLEFT);
        QVERIFY(inset >= 90);
        widthAction->trigger();
        QVERIFY(!widthAction->isChecked());
        QCOMPARE(editor->send(SCI_GETMARGINLEFT), sptr_t(24));
        widthAction->trigger();
        QVERIFY(widthAction->isChecked());
        QCOMPARE(editor->send(SCI_GETMARGINLEFT), inset);
    }
    void simulatedImeCommit() {
        PromptEditor editor;
        editor.show();
        editor.setFocus();
        QInputMethodEvent preedit(QString::fromUtf8("に"), {});
        QApplication::sendEvent(&editor, &preedit);
        QInputMethodEvent commit;
        commit.setCommitString(QString::fromUtf8("日本語"));
        QApplication::sendEvent(&editor, &commit);
        QCOMPARE(editor.textUtf8(), QString::fromUtf8("日本語").toUtf8());
    }
    void multipleCaretsAndGroupedSectionUndo() {
        PromptEditor editor;
        editor.show();
        editor.setTextUtf8("alpha beta");
        editor.send(SCI_SETSEL, 0, 0);
        editor.send(SCI_ADDSELECTION, 6, 6);
        QCOMPARE(editor.send(SCI_GETSELECTIONS), sptr_t(2));
        editor.setFocus();
        QTest::keyClicks(&editor, "X");
        QCOMPARE(editor.textUtf8(), QByteArray("Xalpha Xbeta"));
        const QByteArray source("# Parent\n## One\nA\n## Two\nB\n");
        editor.setTextUtf8(source);
        const auto headings = Outline::parse(source);
        const auto edit = Outline::move(source, headings, 1, true);
        QVERIFY(edit.valid);
        editor.send(SCI_BEGINUNDOACTION);
        editor.send(SCI_SETTARGETSTART, edit.start);
        editor.send(SCI_SETTARGETEND, edit.end);
        editor.send(SCI_REPLACETARGET, edit.replacement.size(), reinterpret_cast<sptr_t>(edit.replacement.constData()));
        editor.send(SCI_ENDUNDOACTION);
        QVERIFY(editor.textUtf8() != source);
        editor.send(SCI_UNDO);
        QCOMPARE(editor.textUtf8(), source);
    }
    void foldingDoesNotChangeCopyPrompt() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        const QByteArray source("# One\nsecret body\n## Child\nmore text\n# Two\nend\n");
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        editor->setTextUtf8(source);
        window.show();
        QTRY_VERIFY(editor->send(SCI_GETFOLDLEVEL, 0) & SC_FOLDLEVELHEADERFLAG);
        editor->send(SCI_TOGGLEFOLD, 0);
        QVERIFY(!editor->send(SCI_GETLINEVISIBLE, 1));
        QAction *copy = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Copy Entire Prompt")) copy = action;
        QVERIFY(copy);
        copy->trigger();
        QCOMPARE(QApplication::clipboard()->text().toUtf8(), source);
    }
    void replaceAllAndSnippetInsertionUndoAsOneStep() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        const QString snippetPath = temp.filePath(QStringLiteral("snippet.md"));
        QFile snippet(snippetPath);
        QVERIFY(snippet.open(QIODevice::WriteOnly));
        QCOMPARE(snippet.write(QString::fromUtf8("🧭 {{braces}}").toUtf8()), qint64(15));
        snippet.close();
        QVERIFY2(store.saveSnippet({QStringLiteral("snippet-1"), QStringLiteral("Example"), snippetPath}, &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        const QByteArray original = QString::fromUtf8("α cat cat").toUtf8();
        editor->setTextUtf8(original);
        QAction *findAction = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Find and replace…")) findAction = action;
        QVERIFY(findAction);
        findAction->trigger();
        QLineEdit *find = nullptr;
        QLineEdit *replacement = nullptr;
        for (auto *field : window.findChildren<QLineEdit *>()) {
            if (field->accessibleName() == QStringLiteral("Find text")) find = field;
            if (field->accessibleName() == QStringLiteral("Replacement text")) replacement = field;
        }
        QVERIFY(find);
        QVERIFY(replacement);
        find->setText(QStringLiteral("cat"));
        replacement->setText(QString::fromUtf8("猫"));
        QPushButton *replaceAll = nullptr;
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text() == QStringLiteral("Replace all")) replaceAll = button;
        QVERIFY(replaceAll);
        replaceAll->click();
        QCOMPARE(editor->textUtf8(), QString::fromUtf8("α 猫 猫").toUtf8());
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), original);

        editor->setTextUtf8("start");
        editor->send(SCI_GOTOPOS, 5);
        QAction *snippets = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Snippets…")) snippets = action;
        QVERIFY(snippets);
        QTimer::singleShot(0, &window, [&window] {
            auto *list = window.findChild<QListWidget *>();
            QVERIFY(list);
            QCOMPARE(list->count(), 1);
            list->setCurrentRow(0);
            for (auto *button : window.findChildren<QPushButton *>())
                if (button->text() == QStringLiteral("Insert")) { button->click(); return; }
            QFAIL("Insert control missing from snippet dialog");
        });
        snippets->trigger();
        QCOMPARE(editor->textUtf8(), QByteArray("start") + QString::fromUtf8("🧭 {{braces}}").toUtf8());
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), QByteArray("start"));
    }
    void minimalWindowAndPreviewPreserveSource() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        window.activateWindow();
        auto *editor = window.findChild<PromptEditor *>();
        auto *tabs = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"));
        QVERIFY(editor);
        QVERIFY(tabs);
        QTRY_VERIFY(editor->hasFocus());
        QVERIFY(!tabs->tabBar()->isVisible());
        QVERIFY(!window.hasPreviewWidget());
        QVERIFY(!window.findChild<QListWidget *>());
        QVERIFY(!window.findChild<QTextBrowser *>());
        QVERIFY(!window.findChild<QLineEdit *>());
        QVERIFY(!window.findChild<QToolBar *>());
        QVERIFY(!window.findChild<QStatusBar *>());
        QTest::keyClicks(QApplication::focusWidget(), "x");
        QCOMPARE(editor->textUtf8(), QByteArray("x"));
        const QByteArray source = QString::fromUtf8("# Café 🧭\n\n**Bold** `{{literal}}`\n\n<img src=\"https://invalid.example/x\">\n").toUtf8();
        editor->setTextUtf8(source);
        editor->send(SCI_SETSEL, 3, 8);
        editor->replaceSelection("Café");
        QVERIFY(editor->send(SCI_CANUNDO));
        const QByteArray edited = editor->textUtf8();
        const auto anchor = editor->send(SCI_GETANCHOR);
        const auto caret = editor->send(SCI_GETCURRENTPOS);
        const auto firstLine = editor->send(SCI_GETFIRSTVISIBLELINE);
        window.setViewMode(MainWindow::ViewMode::Preview);
        QVERIFY(window.hasPreviewWidget());
        auto *preview = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(preview);
        QVERIFY(preview->document()->begin().blockFormat().headingLevel() > 0);
        QVERIFY(preview->isVisible());
        QVERIFY(!tabs->isVisible());
        QVERIFY(preview->toPlainText().contains(QStringLiteral("Bold")));
        auto *returnToEditor = window.findChild<QPushButton *>(QStringLiteral("previewEditButton"));
        QVERIFY(returnToEditor);
        QVERIFY(returnToEditor->isVisible());
        returnToEditor->click();
        QTRY_COMPARE(window.viewMode(), MainWindow::ViewMode::Editor);
        QCOMPARE(editor->textUtf8(), edited);
        QCOMPARE(editor->send(SCI_GETANCHOR), anchor);
        QCOMPARE(editor->send(SCI_GETCURRENTPOS), caret);
        window.setViewMode(MainWindow::ViewMode::Preview);
        preview = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(preview);
        QTest::keyClick(preview->viewport(), Qt::Key_Escape);
        QTRY_COMPARE(window.viewMode(), MainWindow::ViewMode::Editor);
        QCOMPARE(editor->textUtf8(), edited);
        window.setViewMode(MainWindow::ViewMode::Preview);
        preview = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(preview);
        window.setViewMode(MainWindow::ViewMode::SplitPreview);
        QVERIFY(tabs->isVisible());
        QVERIFY(preview->isVisible());
        window.setViewMode(MainWindow::ViewMode::Editor);
        QVERIFY(!window.hasPreviewWidget());
        QCOMPARE(editor->textUtf8(), edited);
        QCOMPARE(editor->send(SCI_GETFIRSTVISIBLELINE), firstLine);
        QCOMPARE(editor->send(SCI_GETANCHOR), anchor);
        QCOMPARE(editor->send(SCI_GETCURRENTPOS), caret);
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), source);
    }
    void libraryCanSnapToEverySideAndMoveFreely() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        editor->setTextUtf8("# Draft\nUnicode: \xf0\x9f\xa7\xad\n");
        editor->send(SCI_SETSEL, 2, 7);
        auto *trigger = window.findChild<QPushButton *>(QStringLiteral("libraryTrigger"));
        QVERIFY(trigger);
        trigger->click();
        auto *library = window.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        QVERIFY(library);
        QVERIFY(library->isVisible());
        for (const auto side : {MainWindow::LibraryDockSide::Left, MainWindow::LibraryDockSide::Right,
                                MainWindow::LibraryDockSide::Top, MainWindow::LibraryDockSide::Bottom}) {
            window.setLibraryDockSide(side);
            QCOMPARE(window.libraryDockSide(), side);
            if (side == MainWindow::LibraryDockSide::Left) QTest::qWait(230);
            QVERIFY(!window.geometry().intersects(library->geometry()));
            const QRect editorRect = window.geometry();
            const QRect panelRect = library->geometry();
            if (side == MainWindow::LibraryDockSide::Left) QVERIFY(panelRect.right() < editorRect.left());
            if (side == MainWindow::LibraryDockSide::Right) QVERIFY(panelRect.left() > editorRect.right());
            if (side == MainWindow::LibraryDockSide::Top) QVERIFY(panelRect.bottom() < editorRect.top());
            if (side == MainWindow::LibraryDockSide::Bottom) QVERIFY(panelRect.top() > editorRect.bottom());
        }
        window.setLibraryDockSide(MainWindow::LibraryDockSide::Free);
        library->move(window.geometry().left() - library->width() - 12, window.geometry().top());
        library->dragFinished();
        QCOMPARE(window.libraryDockSide(), MainWindow::LibraryDockSide::Left);
        QVERIFY(!window.geometry().intersects(library->geometry()));
        window.setLibraryDockSide(MainWindow::LibraryDockSide::Right);
        const QPoint followedPosition = library->pos();
        const QPoint editorPosition = window.pos();
        window.move(editorPosition + QPoint(0, 10));
        QCOMPARE(library->pos(), followedPosition + QPoint(0, 10));
        window.resize(window.width() + 30, window.height() + 20);
        QVERIFY(!window.geometry().intersects(library->geometry()));
        window.setLibraryDockSide(MainWindow::LibraryDockSide::Free);
        const QPoint independentPosition = library->pos();
        window.move(window.pos() + QPoint(12, 0));
        QCOMPARE(library->pos(), independentPosition);
        auto *handle = library->findChild<QWidget *>(QStringLiteral("libraryDragHandle"));
        QVERIFY(handle);
        const QPoint beforeDrag = library->pos();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, QPoint(70, 12));
        QTest::mouseMove(handle, QPoint(-100, 12));
        QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 12));
        QVERIFY(library->pos() != beforeDrag);
        QCOMPARE(window.libraryDockSide(), MainWindow::LibraryDockSide::Free);
        auto *panel = window.findChild<QWidget *>(QStringLiteral("editorPanel"));
        QVERIFY(panel);
        const QPoint beforeEditorDrag = window.pos();
        QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, QPoint(70, 20));
        QTest::mouseMove(panel, QPoint(100, 40));
        QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, QPoint(100, 40));
        QVERIFY(window.pos() != beforeEditorDrag);
        const QByteArray source = editor->textUtf8();
        QCOMPARE(editor->send(SCI_GETSELECTIONSTART), sptr_t(2));
        QCOMPARE(editor->send(SCI_GETSELECTIONEND), sptr_t(7));
        const QSize beforeResize = window.size();
        const QPoint edge(panel->width() - 3, panel->height() - 3);
        QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, edge);
        QTest::mouseMove(panel, edge + QPoint(30, 25));
        QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, edge + QPoint(30, 25));
        QVERIFY(window.width() > beforeResize.width());
        QVERIFY(window.height() > beforeResize.height());
        QCOMPARE(editor->textUtf8(), source);
        QCOMPARE(editor->send(SCI_GETSELECTIONSTART), sptr_t(2));
        QCOMPARE(editor->send(SCI_GETSELECTIONEND), sptr_t(7));
        library->move(library->x(), 80);
        const QPoint savedLibraryPosition = library->pos();
        window.close();
        MainWindow reopened(&store);
        reopened.show();
        QCOMPARE(reopened.libraryDockSide(), MainWindow::LibraryDockSide::Free);
        auto *reopenTrigger = reopened.findChild<QPushButton *>(QStringLiteral("libraryTrigger"));
        QVERIFY(reopenTrigger);
        reopenTrigger->click();
        auto *reopenedLibrary = reopened.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        QVERIFY(reopenedLibrary);
        QCOMPARE(reopenedLibrary->pos(), savedLibraryPosition);
    }
    void onDemandDialogsAndDebouncedPreview() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        const QByteArray source("# Demo\n\nLiteral `{{x}}`\n");
        editor->setTextUtf8(source);
        auto action = [&window](const QString &name) -> QAction * {
            for (auto *candidate : window.findChildren<QAction *>())
                if (candidate->text() == name) return candidate;
            return nullptr;
        };
        QVERIFY(!window.findChild<QTreeWidget *>());
        QVERIFY(!window.findChild<QListWidget *>());
        editor->send(SCI_SETSEL, 2, 2);
        editor->send(SCI_ADDSELECTION, 8, 8);
        QCOMPARE(editor->send(SCI_GETSELECTIONS), sptr_t(2));
        window.setViewMode(MainWindow::ViewMode::Preview);
        window.setViewMode(MainWindow::ViewMode::Editor);
        QCOMPARE(editor->send(SCI_GETSELECTIONS), sptr_t(2));
        editor->send(SCI_SETSEL, source.size(), source.size());
        bool outlineShown = false;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            outlineShown = dialog && dialog->findChild<QTreeWidget *>();
            if (dialog) dialog->reject();
        });
        QVERIFY(action(QStringLiteral("Jump to heading…")));
        action(QStringLiteral("Jump to heading…"))->trigger();
        QVERIFY(outlineShown);
        QVERIFY(action(QStringLiteral("Browse drafts…")));
        action(QStringLiteral("Browse drafts…"))->trigger();
        auto *library = window.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        QVERIFY(library);
        QVERIFY(library->isVisible());
        QVERIFY(!window.geometry().intersects(library->geometry()));
        auto *items = library->findChild<QListWidget *>(QStringLiteral("libraryItems"));
        QVERIFY(items);
        QCOMPARE(items->count(), 1);
        QVERIFY(!QApplication::activeModalWidget());
        library->closeRequested();
        QTRY_VERIFY(!library->isVisible());
        QVERIFY(!window.findChild<QTreeWidget *>());
        QVERIFY(!window.findChild<QTreeWidget *>());
        window.setViewMode(MainWindow::ViewMode::SplitPreview);
        auto *preview = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(preview);
        QCOMPARE(editor->textUtf8(), source);
        QVERIFY(!preview->document()->resourceProvider()(QUrl(QStringLiteral("https://invalid.example/image.png"))).isValid());
        editor->send(SCI_GOTOPOS, source.size());
        editor->replaceSelection("\n## Tail\n");
        QVERIFY(!preview->toPlainText().contains(QStringLiteral("Tail")));
        QTRY_VERIFY_WITH_TIMEOUT(preview->toPlainText().contains(QStringLiteral("Tail")), 2000);
        const QByteArray changed = editor->textUtf8();
        QVERIFY(action(QStringLiteral("Copy Entire Prompt")));
        action(QStringLiteral("Copy Entire Prompt"))->trigger();
        QCOMPARE(QApplication::clipboard()->text().toUtf8(), changed);
        window.setViewMode(MainWindow::ViewMode::Editor);
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), source);
    }
    void starringJustEditedDraftKeepsRecovery() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        const QByteArray source = QString::fromUtf8("# Draft 🧭\r\n{{literal}}\r\n").toUtf8();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        editor->setTextUtf8(source);
        QAction *browse = nullptr;
        for (auto *candidate : window.findChildren<QAction *>())
            if (candidate->text() == QStringLiteral("Browse drafts…")) browse = candidate;
        QVERIFY(browse);
        browse->trigger();
        auto *library = window.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        QVERIFY(library);
        auto *list = library->findChild<QListWidget *>(QStringLiteral("libraryItems"));
        QVERIFY(list);
        QCOMPARE(list->count(), 1);
        const QString id = list->item(0)->data(Qt::UserRole).toString();
        library->starPromptRequested(id);
        library->closeRequested();
        const DraftRecord stored = store.loadDraft(id);
        QVERIFY(stored.starred);
        QCOMPARE(stored.content, source);
    }
    void formattingPreservesUtf8OffsetsAndUndo() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        const QByteArray source = QString::fromUtf8("Café 🧭 tail\n").toUtf8();
        const QByteArray compass = QString::fromUtf8("🧭").toUtf8();
        const qsizetype start = QString::fromUtf8("Café ").toUtf8().size();
        editor->setTextUtf8(source);
        editor->send(SCI_SETSEL, start, start + compass.size());
        QAction *bold = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Bold")) bold = action;
        QVERIFY(bold);
        bold->trigger();
        QCOMPARE(editor->textUtf8(), QString::fromUtf8("Café **🧭** tail\n").toUtf8());
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), source);
        editor->send(SCI_SETSEL, start, start + compass.size());
        QAction *underline = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == QStringLiteral("Underline")) underline = action;
        QVERIFY(underline);
        underline->trigger();
        const QByteArray underlined = QString::fromUtf8("Café <u>🧭</u> tail\n").toUtf8();
        QCOMPARE(editor->textUtf8(), underlined);
        window.setViewMode(MainWindow::ViewMode::Preview);
        auto *preview = window.findChild<QTextBrowser *>(QStringLiteral("markdownPreview"));
        QVERIFY(preview);
        QVERIFY(preview->toPlainText().contains(QString::fromUtf8("🧭")));
        QVERIFY(preview->toHtml().contains(QStringLiteral("text-decoration: underline")));
        window.setViewMode(MainWindow::ViewMode::Editor);
        editor->send(SCI_UNDO);
        QCOMPARE(editor->textUtf8(), source);
    }
    void rightClickOpensContextualFormatting() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        auto *editor = window.findChild<PromptEditor *>();
        QVERIFY(editor);
        editor->setTextUtf8(QString::fromUtf8("Choose 🧭 here\n").toUtf8());
        editor->send(SCI_SETSEL, 7, 11);
        bool shown = false;
        bool emitted = false;
        QObject::connect(editor, &PromptEditor::contextualFormattingRequested, &window,
                         [&](const QPoint &) { emitted = true; });
        QTimer::singleShot(40, &window, [&] {
            auto *menu = window.findChild<QMenu *>(QStringLiteral("formattingMenu"));
            shown = menu && menu->isVisible();
            if (menu) menu->close();
        });
        const QPoint position(static_cast<int>(editor->send(SCI_POINTXFROMPOSITION, 0, 7)) + 1,
                              static_cast<int>(editor->send(SCI_POINTYFROMPOSITION, 0, 7)) + 3);
        QTest::mouseClick(editor->viewport(), Qt::RightButton, Qt::NoModifier, position);
        QContextMenuEvent context(QContextMenuEvent::Mouse, position, editor->mapToGlobal(position));
        QApplication::sendEvent(editor->viewport(), &context);
        QTest::qWait(60);
        QVERIFY(emitted);
        QVERIFY(shown);
        QCOMPARE(editor->selectedUtf8(), QString::fromUtf8("🧭").toUtf8());
    }
    void libraryFiltersSearchAndTemplateCopies() {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp.path());
        Store store;
        QString error;
        QVERIFY2(store.open(temp.path(), &error), qPrintable(error));
        QVERIFY2(store.saveFolder({QStringLiteral("work"), QStringLiteral("Work")}, &error), qPrintable(error));
        const QByteArray templateText = QString::fromUtf8("# Feature 🧭\n{{placeholder}}\n").toUtf8();
        QVERIFY2(store.saveTemplate({QStringLiteral("template-id"), QStringLiteral("Feature brief"),
            QStringLiteral("work"), templateText, 0}, &error), qPrintable(error));
        MainWindow window(&store);
        window.show();
        window.activateWindow();
        auto *originalEditor = window.findChild<PromptEditor *>();
        QVERIFY(originalEditor);
        originalEditor->setTextUtf8("Unfinished first draft\n");
        auto *trigger = window.findChild<QPushButton *>(QStringLiteral("libraryTrigger"));
        QVERIFY(trigger);
        auto *effect = qobject_cast<QGraphicsOpacityEffect *>(trigger->graphicsEffect());
        QVERIFY(effect);
        QTRY_VERIFY(effect->opacity() < 0.1);
        QApplication::processEvents();
        trigger->setFocus();
        QVERIFY(trigger->hasFocus());
        QTRY_VERIFY(effect->opacity() > 0.9);
        trigger->click();
        auto *library = window.findChild<LibraryPanel *>(QStringLiteral("libraryPanel"));
        QVERIFY(library);
        QVERIFY(library->isVisible());
        auto *items = library->findChild<QListWidget *>(QStringLiteral("libraryItems"));
        auto *folders = library->findChild<QListWidget *>(QStringLiteral("libraryFolders"));
        auto *search = library->findChild<QLineEdit *>(QStringLiteral("librarySearch"));
        auto *templates = library->findChild<QPushButton *>(QStringLiteral("libraryFilterTemplates"));
        QVERIFY(items && folders && search && templates);
        QCOMPARE(folders->count(), 2);
        templates->click();
        QCOMPARE(items->count(), 1);
        folders->setCurrentRow(1);
        QCOMPARE(library->selectedFolderId(), QStringLiteral("work"));
        QCOMPARE(items->count(), 1);
        search->setText(QStringLiteral("no match"));
        QCOMPARE(items->count(), 0);
        search->setText(QStringLiteral("placeholder"));
        QCOMPARE(items->count(), 1);
        search->clear();
        library->openTemplateRequested(QStringLiteral("template-id"));
        QTRY_VERIFY(!library->isVisible());
        auto *editor = window.findChild<QTabWidget *>(QStringLiteral("documentTabs"))->currentWidget()->findChild<PromptEditor *>();
        QVERIFY(editor);
        QCOMPARE(editor->textUtf8(), templateText);
        editor->send(SCI_GOTOPOS, templateText.size());
        editor->replaceSelection("change");
        QCOMPARE(store.loadTemplate(QStringLiteral("template-id")).content, templateText);
        trigger->click();
        QVERIFY(library->isVisible());
        library->saveRequested();
        const auto currentDrafts = store.listDrafts(true);
        QVERIFY(currentDrafts.size() >= 2);
        bool foundCopy = false;
        bool preservedFirst = false;
        for (const auto &draft : currentDrafts)
            if (store.loadDraft(draft.id).content == templateText + "change") foundCopy = true;
            else if (store.loadDraft(draft.id).content == QByteArray("Unfinished first draft\n")) preservedFirst = true;
        QVERIFY(foundCopy);
        QVERIFY(preservedFirst);
    }
};

QTEST_MAIN(EditorSmoke)
#include "editor_smoke.moc"
