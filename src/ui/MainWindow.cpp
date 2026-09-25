#include "ui/MainWindow.h"
#include "AppIdentity.h"
#include "Appearance.h"

#include "documents/FileIO.h"
#include "documents/FindReplace.h"
#include "editor/PromptEditor.h"
#include "ui/LibraryPanel.h"
#include "Scintilla.h"

#include <QtConcurrent>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDialog>
#include <QFontDatabase>
#include <QGraphicsOpacityEffect>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QPainterPath>
#include <QRegion>
#include <QScreen>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QShowEvent>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QWindow>

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace {
class SafeMarkdownPreview final : public QTextBrowser {
public:
    explicit SafeMarkdownPreview(QWidget *parent) : QTextBrowser(parent) {
        setObjectName(QStringLiteral("markdownPreview"));
        setAccessibleName(QStringLiteral("Markdown preview"));
        setOpenLinks(false);
        setOpenExternalLinks(false);
        setReadOnly(true);
        setFrameShape(QFrame::NoFrame);
        setStyleSheet(QStringLiteral("background: transparent; border: 0;"));
        viewport()->setAutoFillBackground(false);
        QFont readingFont(PromptEditor::defaultFontFamily());
        readingFont.setPixelSize(14);
        setFont(readingFont);
        document()->setResourceProvider([](const QUrl &) { return QVariant{}; });
        document()->setUndoRedoEnabled(false);
        document()->setDocumentMargin(0);
    }
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};

QString canonicalOrAbsolute(const QString &path) {
    QFileInfo info(path);
    return info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
}

QPushButton *button(const QString &label, QWidget *parent, const QString &name = {}) {
    auto *result = new QPushButton(label, parent);
    if (!name.isEmpty()) result->setObjectName(name);
    result->setAccessibleName(label);
    result->setCursor(Qt::PointingHandCursor);
    return result;
}
}

MainWindow::MainWindow(Store *store, bool createBlankIfEmpty, QWidget *parent) : QMainWindow(parent), store_(store) {
    Q_INIT_RESOURCE(assets);
    reduceMotion_ = QSettings().value(QStringLiteral("appearance/reduceMotion"), false).toBool();
    alwaysShowTrigger_ = QSettings().value(QStringLiteral("appearance/alwaysShowTrigger"), false).toBool();
    const int savedDockSide = QSettings().value(QStringLiteral("window/libraryDockSide"), 0).toInt();
    if (savedDockSide >= 0 && savedDockSide <= static_cast<int>(LibraryDockSide::Free))
        libraryDockSide_ = static_cast<LibraryDockSide>(savedDockSide);
    setWindowTitle(AppIdentity::displayName());
    setAcceptDrops(true);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
    setMinimumSize(340, 270);
    resize(420, 352);
    buildUi();
    buildMenus();
    applyAppearance();
    const QByteArray savedGeometry = QSettings().value(QStringLiteral("window/padGeometry")).toByteArray();
    if (!savedGeometry.isEmpty()) restoreGeometry(savedGeometry);
    else if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center() - QPoint(width() / 2, height() / 2));
    }
    if (QScreen *screen = QGuiApplication::screenAt(frameGeometry().center())) {
        const QRect available = screen->availableGeometry();
        move(qBound(available.left(), x(), std::max(available.left(), available.right() - width() + 1)),
             qBound(available.top(), y(), std::max(available.top(), available.bottom() - height() + 1)));
    }
    backupDebounce_.setSingleShot(true);
    backupMaximum_.setSingleShot(true);
    outlineDebounce_.setSingleShot(true);
    previewDebounce_.setSingleShot(true);
    connect(&backupDebounce_, &QTimer::timeout, this, [this] { flushBackups(); });
    connect(&backupMaximum_, &QTimer::timeout, this, [this] { flushBackups(); });
    connect(&outlineDebounce_, &QTimer::timeout, this, [this] { rebuildOutline(current()); });
    connect(&previewDebounce_, &QTimer::timeout, this, &MainWindow::renderPreview);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, &MainWindow::onExternalChange);
    restoreSession();
    if (documents_.empty() && createBlankIfEmpty) newPrompt();
    updateStatus();
}

void MainWindow::buildUi() {
    root_ = new QWidget(this);
    root_->setObjectName(QStringLiteral("editorPanel"));
    root_->setMouseTracking(true);
    root_->setToolTip(QStringLiteral("Drag the top edge to move; drag any edge to resize"));
    root_->installEventFilter(this);
    rootLayout_ = new QVBoxLayout(root_);
    rootLayout_->setContentsMargins(1, 44, 1, 1);
    rootLayout_->setSpacing(0);
    splitter_ = new QSplitter(Qt::Horizontal, root_);
    splitter_->setHandleWidth(1);
    splitter_->setChildrenCollapsible(false);
    rootLayout_->addWidget(splitter_, 1);
    setCentralWidget(root_);

    triggerArea_ = new QWidget(root_);
    triggerArea_->setObjectName(QStringLiteral("libraryTriggerArea"));
    triggerArea_->setFixedSize(52, 43);
    triggerArea_->setMouseTracking(true);
    triggerArea_->installEventFilter(this);
    libraryTrigger_ = new QPushButton(triggerArea_);
    libraryTrigger_->setObjectName(QStringLiteral("libraryTrigger"));
    libraryTrigger_->setAccessibleName(QStringLiteral("Open library"));
    libraryTrigger_->setToolTip(QStringLiteral("Open library (Ctrl+.)"));
    libraryTrigger_->setFixedSize(32, 32);
    libraryTrigger_->move(10, 8);
    libraryTrigger_->setCursor(Qt::PointingHandCursor);
    libraryTrigger_->installEventFilter(this);
    triggerEffect_ = new QGraphicsOpacityEffect(libraryTrigger_);
    libraryTrigger_->setGraphicsEffect(triggerEffect_);
    triggerEffect_->setOpacity(0);
    connect(libraryTrigger_, &QPushButton::clicked, this, [this] {
        if (library_ && library_->isVisible()) closeLibrary(); else showLibrary();
    });
    triggerArea_->raise();

    tabs_ = new QTabWidget(splitter_);
    tabs_->setObjectName(QStringLiteral("documentTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    tabs_->tabBar()->setExpanding(false);
    tabs_->tabBar()->hide();
    splitter_->addWidget(tabs_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(tabs_, &QTabWidget::currentChanged, this, [this] {
        updateStatus();
        scheduleOutline();
        if (viewMode_ != ViewMode::Editor) renderPreview();
    });
}

void MainWindow::buildFindBar() {
    if (findBar_) return;
    findBar_ = new QWidget(centralWidget());
    findBar_->setObjectName(QStringLiteral("findBar"));
    auto *findLayout = new QVBoxLayout(findBar_);
    findLayout->setContentsMargins(16, 12, 16, 12);
    findLayout->setSpacing(8);
    auto *findRow = new QHBoxLayout;
    findRow->setSpacing(8);
    findEdit_ = new QLineEdit(findBar_);
    findEdit_->setPlaceholderText(QStringLiteral("Find in prompt"));
    findEdit_->setAccessibleName(QStringLiteral("Find text"));
    connect(findEdit_, &QLineEdit::textChanged, this, [this] { updateMatches(); });
    connect(findEdit_, &QLineEdit::returnPressed, this, [this] { updateMatches(true); });
    findRow->addWidget(findEdit_, 1);
    auto *previous = button(QStringLiteral("↑"), findBar_);
    previous->setAccessibleName(QStringLiteral("Previous match"));
    connect(previous, &QPushButton::clicked, this, [this] { updateMatches(true, true); });
    findRow->addWidget(previous);
    auto *next = button(QStringLiteral("↓"), findBar_);
    next->setAccessibleName(QStringLiteral("Next match"));
    connect(next, &QPushButton::clicked, this, [this] { updateMatches(true); });
    findRow->addWidget(next);
    matchCount_ = new QLabel(findBar_);
    matchCount_->setMinimumWidth(76);
    findRow->addWidget(matchCount_);
    findOptionsButton_ = button(QStringLiteral("Options"), findBar_);
    findOptionsButton_->setCheckable(true);
    findOptionsButton_->setAccessibleName(QStringLiteral("Find options"));
    connect(findOptionsButton_, &QPushButton::toggled, this,
            [this](bool open) { findOptionsRow_->setVisible(open); });
    findRow->addWidget(findOptionsButton_);
    auto *close = button(QStringLiteral("×"), findBar_);
    close->setAccessibleName(QStringLiteral("Close find"));
    connect(close, &QPushButton::clicked, this, &MainWindow::hideFindBar);
    findRow->addWidget(close);
    findLayout->addLayout(findRow);
    replaceRow_ = new QWidget(findBar_);
    replaceRow_->setObjectName(QStringLiteral("replaceRow"));
    auto *replaceRow = new QHBoxLayout(replaceRow_);
    replaceRow->setContentsMargins(0, 0, 0, 0);
    replaceRow->setSpacing(8);
    replaceEdit_ = new QLineEdit(replaceRow_);
    replaceEdit_->setPlaceholderText(QStringLiteral("Replace with"));
    replaceEdit_->setAccessibleName(QStringLiteral("Replacement text"));
    replaceRow->addWidget(replaceEdit_, 1);
    connect(replaceEdit_, &QLineEdit::returnPressed, this, &MainWindow::replaceOne);
    auto *replaceButton = button(QStringLiteral("Replace"), replaceRow_);
    connect(replaceButton, &QPushButton::clicked, this, &MainWindow::replaceOne);
    replaceRow->addWidget(replaceButton);
    auto *allButton = button(QStringLiteral("Replace all"), replaceRow_);
    connect(allButton, &QPushButton::clicked, this, &MainWindow::replaceAll);
    replaceRow->addWidget(allButton);
    findLayout->addWidget(replaceRow_);

    findOptionsRow_ = new QWidget(findBar_);
    findOptionsRow_->setObjectName(QStringLiteral("findOptionsRow"));
    auto *optionsRow = new QHBoxLayout(findOptionsRow_);
    optionsRow->setContentsMargins(0, 0, 0, 0);
    optionsRow->setSpacing(12);
    caseCheck_ = new QCheckBox(QStringLiteral("Case"), findOptionsRow_);
    wordCheck_ = new QCheckBox(QStringLiteral("Word"), findOptionsRow_);
    regexCheck_ = new QCheckBox(QStringLiteral("Regex"), findOptionsRow_);
    for (auto *check : {caseCheck_, wordCheck_, regexCheck_}) {
        optionsRow->addWidget(check);
        connect(check, &QCheckBox::toggled, this, [this] { updateMatches(); });
    }
    scopeBox_ = new QComboBox(findOptionsRow_);
    scopeBox_->addItems({QStringLiteral("Document"), QStringLiteral("Selection")});
    scopeBox_->setAccessibleName(QStringLiteral("Find scope"));
    connect(scopeBox_, &QComboBox::currentIndexChanged, this, [this] { updateMatches(); });
    optionsRow->addWidget(scopeBox_);
    optionsRow->addStretch();
    findLayout->addWidget(findOptionsRow_);
    rootLayout_->addWidget(findBar_);
    findBar_->hide();
    replaceRow_->hide();
    findOptionsRow_->hide();
    findBar_->installEventFilter(this);
    for (auto *control : findBar_->findChildren<QWidget *>()) control->installEventFilter(this);
}

void MainWindow::hideFindBar() {
    if (!findBar_) return;
    findBar_->hide();
    if (auto *doc = current()) doc->editor->setFocus();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == triggerArea_ || watched == libraryTrigger_) {
        if (event->type() == QEvent::Enter || event->type() == QEvent::FocusIn) revealLibraryTrigger(true);
        if (event->type() == QEvent::Leave || event->type() == QEvent::FocusOut)
            QTimer::singleShot(0, this, [this] {
                const bool hover = triggerArea_->underMouse() || libraryTrigger_->underMouse();
                revealLibraryTrigger(hover || libraryTrigger_->hasFocus() || (library_ && library_->isVisible()));
            });
    }
    bool resizeSurface = watched == root_ || watched == previewContainer_ || watched == previewHeader_ ||
        (preview_ && watched == preview_->viewport());
    bool moveSurface = watched == root_ || watched == previewHeader_ ||
        (previewHeader_ && watched == previewHeader_->findChild<QLabel *>(QStringLiteral("previewCaption")));
    if (!resizeSurface)
        for (const auto &doc : documents_) if (watched == doc->editor->viewport()) { resizeSurface = true; break; }
    if (resizeSurface && !resizingWindow_ && !movingWindow_ && event->type() == QEvent::MouseMove) {
        const QPoint point = mapFromGlobal(static_cast<QMouseEvent *>(event)->globalPosition().toPoint());
        const bool left = point.x() < 7;
        const bool right = point.x() >= width() - 7;
        const bool top = point.y() < 7;
        const bool bottom = point.y() >= height() - 7;
        if (auto *surface = qobject_cast<QWidget *>(watched)) {
            if ((left && top) || (right && bottom)) surface->setCursor(Qt::SizeFDiagCursor);
            else if ((right && top) || (left && bottom)) surface->setCursor(Qt::SizeBDiagCursor);
            else if (left || right) surface->setCursor(Qt::SizeHorCursor);
            else if (top || bottom) surface->setCursor(Qt::SizeVerCursor);
            else surface->unsetCursor();
        }
    }
    if ((resizingWindow_ || movingWindow_) && (resizeSurface || moveSurface)) {
        if (event->type() == QEvent::MouseMove) {
            const QPoint delta = static_cast<QMouseEvent *>(event)->globalPosition().toPoint() - dragOrigin_;
            if (movingWindow_) move(dragGeometry_.topLeft() + delta);
            else {
                QRect next = dragGeometry_;
                if (resizingWindow_.testFlag(Qt::LeftEdge)) next.setLeft(next.left() + delta.x());
                if (resizingWindow_.testFlag(Qt::RightEdge)) next.setRight(next.right() + delta.x());
                if (resizingWindow_.testFlag(Qt::TopEdge)) next.setTop(next.top() + delta.y());
                if (resizingWindow_.testFlag(Qt::BottomEdge)) next.setBottom(next.bottom() + delta.y());
                if (next.width() < minimumWidth()) {
                    if (resizingWindow_.testFlag(Qt::LeftEdge)) next.setLeft(next.right() - minimumWidth() + 1);
                    else next.setRight(next.left() + minimumWidth() - 1);
                }
                if (next.height() < minimumHeight()) {
                    if (resizingWindow_.testFlag(Qt::TopEdge)) next.setTop(next.bottom() - minimumHeight() + 1);
                    else next.setBottom(next.top() + minimumHeight() - 1);
                }
                setGeometry(next);
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            resizingWindow_ = {};
            movingWindow_ = false;
            return true;
        }
    }
    if ((resizeSurface || moveSurface) && event->type() == QEvent::MouseButtonPress) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const QPoint point = mapFromGlobal(mouse->globalPosition().toPoint());
        Qt::Edges edges;
        if (point.x() < 7) edges |= Qt::LeftEdge;
        if (point.x() >= width() - 7) edges |= Qt::RightEdge;
        if (point.y() < 7) edges |= Qt::TopEdge;
        if (point.y() >= height() - 7) edges |= Qt::BottomEdge;
        if (mouse->button() == Qt::LeftButton && edges && resizeSurface) {
            resizingWindow_ = edges;
            dragOrigin_ = mouse->globalPosition().toPoint();
            dragGeometry_ = geometry();
            return true;
        }
        if (moveSurface && mouse->button() == Qt::LeftButton &&
            (watched != root_ || point.y() < 43) &&
            (watched != root_ || !triggerArea_->geometry().contains(mouse->position().toPoint()))) {
            movingWindow_ = true;
            dragOrigin_ = mouse->globalPosition().toPoint();
            dragGeometry_ = geometry();
            return true;
        }
    }
    if (watched == qApp && library_ && library_->isVisible()) {
        if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            closeLibrary(); return true;
        }
    }
    if (viewMode_ == ViewMode::Preview && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape &&
        (watched == preview_ || (preview_ && watched == preview_->viewport()))) {
        QTimer::singleShot(0, this, [this] { setViewMode(ViewMode::Editor); });
        return true;
    }
    if (findBar_ && findBar_->isVisible() && event->type() == QEvent::KeyPress) {
        auto *control = qobject_cast<QWidget *>(watched);
        if (control && (control == findBar_ || findBar_->isAncestorOf(control)) &&
            static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            hideFindBar();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setViewMode(ViewMode mode) {
    if (mode == viewMode_) return;
    auto *doc = current();
    const auto firstLine = doc ? doc->editor->send(SCI_GETFIRSTVISIBLELINE) : 0;
    viewMode_ = mode;
    rootLayout_->setContentsMargins(1, mode == ViewMode::Preview ? 0 : 44, 1, 1);
    if (mode != ViewMode::Editor && !preview_) {
        previewContainer_ = new QWidget(splitter_);
        previewContainer_->setObjectName(QStringLiteral("previewContainer"));
        previewContainer_->setMouseTracking(true);
        previewContainer_->installEventFilter(this);
        auto *layout = new QVBoxLayout(previewContainer_);
        layout->setContentsMargins(23, 12, 20, 10);
        layout->setSpacing(7);
        previewHeader_ = new QWidget(previewContainer_);
        previewHeader_->setObjectName(QStringLiteral("previewDragHandle"));
        previewHeader_->setAccessibleName(QStringLiteral("Drag editor window"));
        previewHeader_->setCursor(Qt::OpenHandCursor);
        previewHeader_->installEventFilter(this);
        auto *header = new QHBoxLayout(previewHeader_);
        header->setContentsMargins(0, 0, 0, 0);
        auto *label = new QLabel(QStringLiteral("PREVIEW"), previewHeader_);
        label->setObjectName(QStringLiteral("previewCaption"));
        label->setCursor(Qt::OpenHandCursor);
        label->installEventFilter(this);
        header->addWidget(label);
        header->addStretch();
        auto *editButton = button(QStringLiteral("Edit"), previewHeader_, QStringLiteral("previewEditButton"));
        editButton->setToolTip(QStringLiteral("Return to editor (Ctrl+Alt+1)"));
        editButton->setFixedHeight(28);
        connect(editButton, &QPushButton::clicked, this, [this] {
            QTimer::singleShot(0, this, [this] { setViewMode(ViewMode::Editor); });
        });
        header->addWidget(editButton);
        layout->addWidget(previewHeader_);
        preview_ = new SafeMarkdownPreview(previewContainer_);
        preview_->viewport()->installEventFilter(this);
        QPalette previewPalette = preview_->palette();
        previewPalette.setColor(QPalette::Text, Appearance::forMode(dark_).text);
        previewPalette.setColor(QPalette::Base, Qt::transparent);
        preview_->setPalette(previewPalette);
        layout->addWidget(preview_, 1);
        splitter_->addWidget(previewContainer_);
        splitter_->setStretchFactor(0, 1);
        splitter_->setStretchFactor(1, 1);
    }
    if (mode == ViewMode::Editor) {
        previewDebounce_.stop();
        QWidget *oldContainer = previewContainer_;
        if (preview_) preview_->viewport()->removeEventFilter(this);
        if (previewContainer_) previewContainer_->removeEventFilter(this);
        if (previewHeader_) {
            previewHeader_->removeEventFilter(this);
            if (auto *caption = previewHeader_->findChild<QLabel *>(QStringLiteral("previewCaption")))
                caption->removeEventFilter(this);
        }
        preview_ = nullptr;
        previewContainer_ = nullptr;
        previewHeader_ = nullptr;
        delete oldContainer;
        previewDocumentId_.clear();
        tabs_->show();
        if (doc) doc->editor->setFocus();
    } else {
        if (previewContainer_) previewContainer_->show();
        tabs_->setVisible(mode == ViewMode::SplitPreview);
        if (mode == ViewMode::SplitPreview) splitter_->setSizes({width() / 2, width() / 2});
        renderPreview();
        if (mode == ViewMode::Preview && preview_) preview_->setFocus();
        else if (doc) doc->editor->setFocus();
    }
    if (doc) {
        doc->editor->send(SCI_SETFIRSTVISIBLELINE, firstLine);
    }
    editorAction_->setChecked(mode == ViewMode::Editor);
    previewAction_->setChecked(mode == ViewMode::Preview);
    splitAction_->setChecked(mode == ViewMode::SplitPreview);
}

void MainWindow::renderPreview() {
    if (!preview_ || viewMode_ == ViewMode::Editor || !preview_->isVisible()) return;
    auto *doc = current();
    if (!doc) { preview_->clear(); previewDocumentId_.clear(); return; }
    if (previewDocumentId_ == doc->record.id && previewRevision_ == doc->revision) return;
    const QByteArray source = doc->editor->textUtf8();
    preview_->document()->setMarkdown(QString::fromUtf8(source),
        QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub));
    QTextCursor content(preview_->document());
    content.select(QTextCursor::Document);
    QTextBlockFormat breathingRoom;
    breathingRoom.setTopMargin(2);
    breathingRoom.setBottomMargin(5);
    breathingRoom.setLineHeight(120, QTextBlockFormat::ProportionalHeight);
    content.mergeBlockFormat(breathingRoom);
    preview_->verticalScrollBar()->setValue(0);
    previewDocumentId_ = doc->record.id;
    previewRevision_ = doc->revision;
}

void MainWindow::buildMenus() {
    auto add = [this](QMenu *menu, const QString &title, const QKeySequence &shortcut,
                      const std::function<void()> &callback) {
        QAction *action = menu->addAction(title);
        if (!shortcut.isEmpty()) action->setShortcut(shortcut);
        action->setShortcutContext(Qt::ApplicationShortcut);
        connect(action, &QAction::triggered, this, callback);
        return action;
    };
    auto *file = menuBar()->addMenu(QStringLiteral("File"));
    add(file, QStringLiteral("New prompt"), QKeySequence::New, [this] { newPrompt(); });
    add(file, QStringLiteral("Open…"), QKeySequence::Open, [this] { openDialog(); });
    file->addSeparator();
    add(file, QStringLiteral("Save"), QKeySequence::Save, [this] { saveDocument(current()); });
    add(file, QStringLiteral("Save As…"), QKeySequence::SaveAs, [this] { saveDocument(current(), true); });
    saveReturnMenuAction_ = add(file, QStringLiteral("Save and return"), QKeySequence(QStringLiteral("Ctrl+Shift+Return")), [this] { saveAndReturn(); });
    saveReturnMenuAction_->setVisible(false);
    file->addSeparator();
    add(file, QStringLiteral("Close tab"), QKeySequence::Close, [this] { closeTab(tabs_->currentIndex()); });

    auto *edit = menuBar()->addMenu(QStringLiteral("Edit"));
    add(edit, QStringLiteral("Undo"), QKeySequence::Undo, [this] { if (auto *doc = current()) doc->editor->send(SCI_UNDO); });
    add(edit, QStringLiteral("Redo"), QKeySequence::Redo, [this] { if (auto *doc = current()) doc->editor->send(SCI_REDO); });
    edit->addSeparator();
    add(edit, QStringLiteral("Cut"), QKeySequence::Cut, [this] { if (auto *doc = current()) doc->editor->send(SCI_CUT); });
    add(edit, QStringLiteral("Copy selection"), QKeySequence::Copy, [this] { if (auto *doc = current()) doc->editor->send(SCI_COPY); });
    add(edit, QStringLiteral("Paste"), QKeySequence::Paste, [this] { if (auto *doc = current()) doc->editor->send(SCI_PASTE); });
    add(edit, QStringLiteral("Select all"), QKeySequence::SelectAll, [this] { if (auto *doc = current()) doc->editor->send(SCI_SELECTALL); });
    edit->addSeparator();
    add(edit, QStringLiteral("Find…"), QKeySequence::Find, [this] { showFind(false); });
    add(edit, QStringLiteral("Find and replace…"), QKeySequence(QStringLiteral("Ctrl+H")), [this] { showFind(true); });
    add(edit, QStringLiteral("Copy Entire Prompt"), QKeySequence(QStringLiteral("Ctrl+Return")), [this] { copyPrompt(); });
    edit->addSeparator();
    add(edit, QStringLiteral("Bold"), QKeySequence(QStringLiteral("Ctrl+B")), [this] { formatSelection("**", "**"); });
    add(edit, QStringLiteral("Italic"), QKeySequence(QStringLiteral("Ctrl+I")), [this] { formatSelection("*", "*"); });
    add(edit, QStringLiteral("Underline"), QKeySequence(QStringLiteral("Ctrl+U")), [this] { formatSelection("<u>", "</u>"); });
    add(edit, QStringLiteral("Formatting…"), QKeySequence(QStringLiteral("Shift+F10")), [this] {
        auto *doc = current();
        if (!doc) return;
        const auto caret = doc->editor->send(SCI_GETCURRENTPOS);
        const QPoint point(static_cast<int>(doc->editor->send(SCI_POINTXFROMPOSITION, 0, caret)),
                           static_cast<int>(doc->editor->send(SCI_POINTYFROMPOSITION, 0, caret)) + 23);
        showFormattingMenu(doc->editor->mapToGlobal(point));
    });

    auto *section = menuBar()->addMenu(QStringLiteral("Section"));
    for (const auto &label : {"Select section", "Copy section", "Duplicate section", "Move section up", "Move section down", "Toggle section fold"}) {
        const QString name = QString::fromLatin1(label);
        add(section, name, {}, [this, name] { sectionAction(name); });
    }

    add(section, QStringLiteral("Jump to heading…"), QKeySequence(QStringLiteral("Ctrl+Shift+O")), [this] { showOutline(); });

    auto *view = menuBar()->addMenu(QStringLiteral("View"));
    auto *modeGroup = new QActionGroup(this);
    editorAction_ = add(view, QStringLiteral("Editor"), QKeySequence(QStringLiteral("Ctrl+Alt+1")), [this] { setViewMode(ViewMode::Editor); });
    previewAction_ = add(view, QStringLiteral("Preview"), QKeySequence(QStringLiteral("Ctrl+Alt+2")), [this] { setViewMode(ViewMode::Preview); });
    splitAction_ = add(view, QStringLiteral("Split Preview"), QKeySequence(QStringLiteral("Ctrl+Alt+3")), [this] { setViewMode(ViewMode::SplitPreview); });
    for (auto *action : {editorAction_, previewAction_, splitAction_}) {
        action->setCheckable(true);
        modeGroup->addAction(action);
    }
    editorAction_->setChecked(true);
    view->addSeparator();
    auto *wrap = add(view, QStringLiteral("Wrap lines"), {}, [this] {
        QSettings settings; const bool enabled = !settings.value(QStringLiteral("editor/wrap"), true).toBool();
        settings.setValue(QStringLiteral("editor/wrap"), enabled);
        for (const auto &doc : documents_) doc->editor->setWrap(enabled);
    });
    wrap->setCheckable(true);
    wrap->setChecked(QSettings().value(QStringLiteral("editor/wrap"), true).toBool());
    connect(wrap, &QAction::triggered, this, [wrap] { wrap->setChecked(QSettings().value(QStringLiteral("editor/wrap"), true).toBool()); });
    auto *comfortable = add(view, QStringLiteral("Comfortable writing width"), {}, [this] {
        QSettings settings;
        const bool enabled = !settings.value(QStringLiteral("editor/comfortableWidth"), true).toBool();
        settings.setValue(QStringLiteral("editor/comfortableWidth"), enabled);
        for (const auto &doc : documents_) doc->editor->setComfortableWidth(enabled);
    });
    comfortable->setCheckable(true);
    comfortable->setChecked(QSettings().value(QStringLiteral("editor/comfortableWidth"), true).toBool());
    connect(comfortable, &QAction::triggered, this, [comfortable] {
        comfortable->setChecked(QSettings().value(QStringLiteral("editor/comfortableWidth"), true).toBool());
    });
    auto *numbers = add(view, QStringLiteral("Line numbers"), {}, [this] {
        QSettings settings; const bool enabled = !settings.value(QStringLiteral("editor/lineNumbers"), false).toBool();
        settings.setValue(QStringLiteral("editor/lineNumbers"), enabled);
        for (const auto &doc : documents_) doc->editor->setLineNumbers(enabled);
    });
    numbers->setCheckable(true);
    numbers->setChecked(QSettings().value(QStringLiteral("editor/lineNumbers"), false).toBool());
    connect(numbers, &QAction::triggered, this, [numbers] { numbers->setChecked(QSettings().value(QStringLiteral("editor/lineNumbers"), false).toBool()); });
    auto *appearance = view->addMenu(QStringLiteral("Appearance"));
    for (const auto &mode : {"System", "Light", "Dark"}) {
        const QString name = QString::fromLatin1(mode);
        add(appearance, name, {}, [this, name] {
            QSettings().setValue(QStringLiteral("appearance/mode"), name);
            applyAppearance();
        });
    }
    auto *always = add(view, QStringLiteral("Always show library button"), {}, [this] {
        alwaysShowTrigger_ = !alwaysShowTrigger_;
        QSettings().setValue(QStringLiteral("appearance/alwaysShowTrigger"), alwaysShowTrigger_);
        revealLibraryTrigger(alwaysShowTrigger_);
    });
    always->setCheckable(true);
    always->setChecked(QSettings().value(QStringLiteral("appearance/alwaysShowTrigger"), false).toBool());
    auto *motion = add(view, QStringLiteral("Reduce motion"), {}, [this] {
        reduceMotion_ = !reduceMotion_;
        QSettings().setValue(QStringLiteral("appearance/reduceMotion"), reduceMotion_);
    });
    motion->setCheckable(true);
    motion->setChecked(QSettings().value(QStringLiteral("appearance/reduceMotion"), false).toBool());
    add(view, QStringLiteral("Editor font…"), {}, [this] {
        bool ok = false;
        const QFont currentFont(QSettings().value(QStringLiteral("editor/font"), PromptEditor::defaultFontFamily()).toString());
        const QFont chosen = QFontDialog::getFont(&ok, currentFont, this, QStringLiteral("Editor font"));
        if (!ok) return;
        QSettings().setValue(QStringLiteral("editor/font"), chosen.family());
        const int size = QSettings().value(QStringLiteral("editor/size"), 14).toInt();
        const int spacing = QSettings().value(QStringLiteral("editor/spacing"), 3).toInt();
        for (const auto &doc : documents_) doc->editor->setEditorFont(chosen.family(), size, spacing);
    });
    add(view, QStringLiteral("Editor text size…"), {}, [this] {
        bool ok = false;
        const int size = QInputDialog::getInt(this, QStringLiteral("Editor text size"), QStringLiteral("Logical pixels"),
                                               QSettings().value(QStringLiteral("editor/size"), 14).toInt(), 11, 32, 1, &ok);
        if (!ok) return;
        QSettings().setValue(QStringLiteral("editor/size"), size);
        const QString family = QSettings().value(QStringLiteral("editor/font"), PromptEditor::defaultFontFamily()).toString();
        const int spacing = QSettings().value(QStringLiteral("editor/spacing"), 3).toInt();
        for (const auto &doc : documents_) doc->editor->setEditorFont(family, size, spacing);
    });
    add(view, QStringLiteral("Line spacing…"), {}, [this] {
        bool ok = false;
        const int spacing = QInputDialog::getInt(this, QStringLiteral("Line spacing"), QStringLiteral("Extra pixels above and below"),
                                                 QSettings().value(QStringLiteral("editor/spacing"), 3).toInt(), 0, 12, 1, &ok);
        if (!ok) return;
        QSettings().setValue(QStringLiteral("editor/spacing"), spacing);
        const QString family = QSettings().value(QStringLiteral("editor/font"), PromptEditor::defaultFontFamily()).toString();
        const int size = QSettings().value(QStringLiteral("editor/size"), 14).toInt();
        for (const auto &doc : documents_) doc->editor->setEditorFont(family, size, spacing);
    });

    auto *tools = menuBar()->addMenu(QStringLiteral("Tools"));
    add(tools, QStringLiteral("Open library"), QKeySequence(QStringLiteral("Ctrl+.")), [this] { showLibrary(); });
    add(tools, QStringLiteral("Browse drafts…"), QKeySequence(QStringLiteral("Ctrl+Shift+L")), [this] { showLibrary(); });
    add(tools, QStringLiteral("Starred drafts…"), {}, [this] { showLibrary(true); });
    add(tools, QStringLiteral("Snippets…"), {}, [this] { showSnippets(); });
    tools->addSeparator();
    add(tools, QStringLiteral("Command palette…"), QKeySequence(QStringLiteral("Ctrl+Shift+P")), [this] { showPalette(); });
    add(tools, QStringLiteral("Quick document switch…"), QKeySequence(QStringLiteral("Ctrl+K")), [this] { showPalette(true); });
    tools->addSeparator();
    add(tools, QStringLiteral("Create snippet from selection…"), {}, [this] { createSnippet(); });
    add(tools, QStringLiteral("Import snippet…"), {}, [this] { importSnippet(); });
    tools->addSeparator();
    add(tools, QStringLiteral("Create checkpoint…"), {}, [this] { checkpoint(); });
    add(tools, QStringLiteral("Compare / restore checkpoint…"), {}, [this] { showCheckpoints(); });
    add(tools, QStringLiteral("Clear checkpoints and closed drafts…"), {}, [this] {
        if (QMessageBox::question(this, QStringLiteral("Clear local history?"),
            QStringLiteral("Remove checkpoints and closed app-owned drafts? Original files stay on disk.")) != QMessageBox::Yes) return;
        QString error;
        if (!store_->clearHistory(&error)) { QMessageBox::warning(this, QStringLiteral("Could not clear history"), error); return; }
        store_->clearClosedDrafts(&error);
        if (!error.isEmpty()) QMessageBox::warning(this, QStringLiteral("Could not clear some drafts"), error);
    });
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (QSettings().value(QStringLiteral("appearance/mode"), QStringLiteral("System")).toString() == QStringLiteral("System")) applyAppearance();
    });
}

void MainWindow::applyAppearance() {
    const QString mode = QSettings().value(QStringLiteral("appearance/mode"), QStringLiteral("System")).toString();
    const bool dark = mode == QStringLiteral("Dark") ||
        (mode == QStringLiteral("System") && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);
    const Appearance palette = Appearance::forMode(dark);
    dark_ = dark;
    qApp->setStyleSheet(QStringLiteral(R"(
      QMainWindow { background: transparent; color: %3; }
      #editorPanel { background: %1; border: 1px solid %6; border-radius: 18px; }
      #libraryTriggerArea { background: transparent; border: 0; }
      #libraryTrigger { background: transparent; border: 0; border-radius: 8px; padding: 0; }
      #libraryTrigger:hover, #libraryTrigger:focus { background: %2; }
      #previewContainer { background: transparent; }
      QTabWidget::pane { background: %1; color: %3; }
      #previewCaption { color: %4; font-size: 10px; font-weight: 600; letter-spacing: 1px; }
      QMenuBar, #findBar { background: %2; color: %3; }
      QLabel { color: %3; }
      QLineEdit, QComboBox, QListWidget, QTreeWidget, QPlainTextEdit {
        background: %1; color: %3; border: 1px solid %6; border-radius: 6px; padding: 7px;
        selection-background-color: %7;
      }
      QListWidget::item, QTreeWidget::item { padding: 6px; }
      QListWidget::item:selected, QTreeWidget::item:selected { background: %7; color: %3; }
      QPushButton { background: %2; color: %3; border: 1px solid %6; border-radius: 6px; padding: 7px 11px; }
      QPushButton:hover, QLineEdit:focus, QListWidget:focus { border-color: %5; }
      QPushButton:checked { background: %7; border-color: %5; }
      QTabWidget::pane { border: 0; }
      QTabBar::tab { background: %2; color: %4; padding: 8px 14px; border-right: 1px solid %6; }
      QTabBar::tab:selected { background: %1; color: %3; border-top: 2px solid %5; }
      QSplitter::handle { background: %6; }
      QMenu { background: %2; color: %3; border: 1px solid %6; }
      QMenu::item:selected { background: %7; }
    )").arg(palette.canvas.name(), palette.surface.name(), palette.text.name(),
            palette.secondaryText.name(), palette.accent.name(), palette.border.name(), palette.selection.name()));
    libraryTrigger_->setIcon(QIcon(QStringLiteral(":/figma/%1-ellipsis.svg").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"))));
    libraryTrigger_->setIconSize(QSize(32, 32));
    revealLibraryTrigger(alwaysShowTrigger_ || (library_ && library_->isVisible()));
    if (library_) library_->setAppearance(dark);
    for (const auto &document : documents_) document->editor->setAppearance(dark);
    if (preview_) {
        QPalette previewPalette = preview_->palette();
        previewPalette.setColor(QPalette::Text, palette.text);
        previewPalette.setColor(QPalette::Base, Qt::transparent);
        preview_->setPalette(previewPalette);
        previewDocumentId_.clear();
        if (viewMode_ != ViewMode::Editor) renderPreview();
    }
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, [this] {
        if (viewMode_ == ViewMode::Editor) if (auto *doc = current()) doc->editor->setFocus(Qt::OtherFocusReason);
    });
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    QPainterPath boundary;
    boundary.addRoundedRect(QRectF(rect()), 18, 18);
    setMask(QRegion(boundary.toFillPolygon().toPolygon()));
    if (triggerArea_) {
        triggerArea_->move(width() - 53, 1);
        triggerArea_->raise();
    }
    if (library_ && library_->isVisible()) placeLibrary();
}

void MainWindow::moveEvent(QMoveEvent *event) {
    QMainWindow::moveEvent(event);
    if (library_ && library_->isVisible()) placeLibrary();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event) {
    for (const auto &url : event->mimeData()->urls()) if (url.isLocalFile()) openPath(url.toLocalFile());
    event->acceptProposedAction();
}

MainWindow::Document *MainWindow::current() const {
    QWidget *page = tabs_->currentWidget();
    for (const auto &document : documents_) if (document->page == page) return document.get();
    return nullptr;
}

MainWindow::Document *MainWindow::byId(const QString &id) const {
    for (const auto &document : documents_) if (document->record.id == id) return document.get();
    return nullptr;
}

bool MainWindow::hasDocument(const QString &id) const { return byId(id) != nullptr; }

MainWindow::Document *MainWindow::createDocument(const DraftRecord &initial, const QByteArray &text, bool recovered) {
    auto document = std::make_unique<Document>();
    document->record = initial;
    document->record.content.clear();
    document->page = new QWidget(tabs_);
    auto *layout = new QVBoxLayout(document->page);
    layout->setContentsMargins(0, 0, 0, 0);
    document->editor = new PromptEditor(document->page);
    document->editor->viewport()->installEventFilter(this);
    document->editor->setComfortableWidth(QSettings().value(QStringLiteral("editor/comfortableWidth"), true).toBool());
    document->editor->setWrap(QSettings().value(QStringLiteral("editor/wrap"), true).toBool());
    document->editor->setLineNumbers(QSettings().value(QStringLiteral("editor/lineNumbers"), false).toBool());
    document->editor->setEditorFont(
        QSettings().value(QStringLiteral("editor/font"), PromptEditor::defaultFontFamily()).toString(),
        QSettings().value(QStringLiteral("editor/size"), 14).toInt(),
        QSettings().value(QStringLiteral("editor/spacing"), 3).toInt());
    const QString mode = QSettings().value(QStringLiteral("appearance/mode"), QStringLiteral("System")).toString();
    document->editor->setAppearance(mode == QStringLiteral("Dark") ||
        (mode == QStringLiteral("System") && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark));
    document->editor->setTextUtf8(text);
    const int crlf = text.count("\r\n");
    const int lf = text.count('\n') - crlf;
    const int cr = text.count('\r') - crlf;
    document->editor->send(SCI_SETEOLMODE, crlf > lf && crlf >= cr ? SC_EOL_CRLF : cr > lf ? SC_EOL_CR : SC_EOL_LF);
    document->editor->send(SCI_GOTOPOS, qBound(qsizetype(0), qsizetype(initial.caret), text.size()));
    document->editor->send(SCI_SETFIRSTVISIBLELINE, initial.firstVisibleLine);
    document->editor->setAccessibleName(QStringLiteral("Prompt editor: %1").arg(initial.title));
    connect(document->editor, &PromptEditor::contextualFormattingRequested, this, [this, editor = document->editor](const QPoint &point) {
        if (auto *doc = current(); doc && doc->editor == editor) showFormattingMenu(point);
    });
    layout->addWidget(document->editor);
    auto *pointer = document.get();
    documents_.push_back(std::move(document));
    const int tab = tabs_->addTab(pointer->page, QString());
    tabs_->setCurrentIndex(tab);
    tabs_->tabBar()->setVisible(tabs_->count() > 1);
    updateTab(pointer);
    const QString id = initial.id;
    connect(pointer->editor, &ScintillaEditBase::modified, this,
            [this, id](Scintilla::ModificationFlags type, Scintilla::Position, Scintilla::Position,
                       Scintilla::Position, const QByteArray &, Scintilla::Position,
                       Scintilla::FoldLevel, Scintilla::FoldLevel) {
        if (!(static_cast<int>(type) & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT))) return;
        auto *doc = byId(id);
        if (!doc) return;
        doc->record.modified = true;
        doc->backupCurrent = false;
        ++doc->revision;
        updateTab(doc); updateStatus();
        if (viewMode_ != ViewMode::Editor && doc == current()) previewDebounce_.start(280);
        scheduleBackup(); scheduleOutline();
    });
    connect(pointer->editor, &ScintillaEditBase::updateUi, this, [this, id](Scintilla::Update) {
        if (current() && current()->record.id == id) updateStatus();
    });
    connect(pointer->editor, &ScintillaEditBase::uriDropped, this, [this](const QString &uri) {
        const QUrl url(uri);
        if (url.isLocalFile()) openPath(url.toLocalFile());
    });
    if (!initial.path.isEmpty() && QFileInfo::exists(initial.path) && !watcher_.files().contains(initial.path))
        watcher_.addPath(initial.path);
    if (recovered) showNotice(QStringLiteral("Draft recovered"), 5000);
    scheduleOutline();
    return pointer;
}

void MainWindow::restoreSession() {
    const auto records = store_->listDrafts(true);
    int recoveredCount = 0;
    for (const auto &summary : records) {
        DraftRecord record = store_->loadDraft(summary.id);
        QByteArray text;
        bool recovered = false;
        if (record.modified || record.path.isEmpty()) {
            text = record.content;
            recovered = record.modified && !text.isEmpty();
        } else if (!record.path.isEmpty()) {
            const auto loaded = FileIO::read(record.path);
            if (loaded.ok) {
                text = loaded.text;
                record.savedHash = loaded.sha256;
                record.bom = loaded.utf8Bom;
            } else {
                record.modified = true;
                recovered = true;
                text = record.content;
            }
        }
        auto *doc = createDocument(record, text, recovered);
        if (recovered) ++recoveredCount;
        if (!record.path.isEmpty() && QFileInfo::exists(record.path) && !record.savedHash.isEmpty()) {
            const auto disk = FileIO::read(record.path);
            doc->changedExternally = !disk.ok || disk.sha256 != record.savedHash;
        }
    }
    if (recoveredCount) showNotice(QStringLiteral("%1 draft(s) recovered locally").arg(recoveredCount), 7000);
}

QString MainWindow::newPrompt() {
    if (current() && !flushBackups()) return {};
    DraftRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.title = QStringLiteral("New prompt");
    record.open = true;
    createDocument(record, {}, false);
    scheduleBackup();
    if (auto *doc = current()) doc->editor->setFocus();
    return record.id;
}

QString MainWindow::openPath(const QString &path, bool allowMissing) {
    const QString absolute = canonicalOrAbsolute(path);
    for (const auto &doc : documents_) if (doc->record.path == absolute) {
        tabs_->setCurrentWidget(doc->page);
        raise(); activateWindow();
        return doc->record.id;
    }
    const QFileInfo info(absolute);
    FileIO::Result loaded;
    if (info.exists()) {
        loaded = FileIO::read(absolute);
        if (!loaded.ok) { QMessageBox::warning(this, QStringLiteral("Could not open file"), loaded.error); return {}; }
    } else if (!allowMissing) {
        QMessageBox::warning(this, QStringLiteral("Could not open file"), QStringLiteral("This file does not exist."));
        return {};
    }
    DraftRecord record;
    record.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    record.path = absolute;
    record.title = QFileInfo(absolute).fileName();
    record.savedHash = loaded.sha256;
    record.bom = loaded.utf8Bom;
    record.open = true;
    createDocument(record, loaded.text, false);
    scheduleBackup();

    if (auto *doc = current()) doc->editor->setFocus();
    return record.id;
}

bool MainWindow::attachWait(const QString &id) {
    auto *doc = byId(id);
    if (!doc || doc->waiting || doc->record.path.isEmpty()) return false;
    doc->waiting = true;
    tabs_->setCurrentWidget(doc->page);
    updateStatus();
    return true;
}

void MainWindow::cancelWait(const QString &id) {
    if (auto *doc = byId(id)) { doc->waiting = false; updateStatus(); }
}

void MainWindow::updateTab(Document *document) {
    const int index = tabs_->indexOf(document->page);
    if (index < 0) return;
    const QString label = document->record.path.isEmpty() ? document->record.title : QFileInfo(document->record.path).fileName();
    tabs_->setTabText(index, label + (document->record.modified ? QStringLiteral(" •") : QString()));
    tabs_->setTabToolTip(index, document->record.path.isEmpty() ? QStringLiteral("Local draft") : document->record.path);
    if (document == current()) updateStatus();
}

void MainWindow::updateStatus() {
    auto *doc = current();
    const QString label = doc ? (doc->record.path.isEmpty() ? doc->record.title : QFileInfo(doc->record.path).fileName()) : AppIdentity::displayName();
    const QString marker = doc && doc->changedExternally ? QStringLiteral("⚠ ") :
        doc && doc->record.modified ? QStringLiteral("• ") : QString();
    const QString title = marker + label + QStringLiteral(" — ") + AppIdentity::displayName();
    if (windowTitle() != title) setWindowTitle(title);
    if (saveReturnMenuAction_) saveReturnMenuAction_->setVisible(doc && doc->waiting);
    if (saveReturnMenuAction_) saveReturnMenuAction_->setEnabled(doc && doc->waiting);
}

void MainWindow::showNotice(const QString &message, int milliseconds) {
    auto *bar = statusBar();
    bar->setSizeGripEnabled(false);
    bar->showMessage(message, milliseconds);
    bar->show();
    QTimer::singleShot(milliseconds, this, [this, bar] { if (bar->currentMessage().isEmpty()) bar->hide(); });
}

void MainWindow::scheduleBackup() {
    backupDebounce_.start(650);
    if (!backupMaximum_.isActive()) backupMaximum_.start(5000);
}

bool MainWindow::flushBackups() {
    backupDebounce_.stop();
    backupMaximum_.stop();
    bool allOkay = true;
    QString error;
    for (size_t index = 0; index < documents_.size(); ++index) {
        auto *doc = documents_[index].get();
        DraftRecord snapshot = doc->record;
        snapshot.open = true;
        snapshot.tabOrder = static_cast<int>(index);
        snapshot.caret = static_cast<int>(doc->editor->send(SCI_GETCURRENTPOS));
        snapshot.firstVisibleLine = static_cast<int>(doc->editor->send(SCI_GETFIRSTVISIBLELINE));
        if (snapshot.modified || snapshot.path.isEmpty()) snapshot.content = doc->editor->textUtf8();
        if (!store_->saveDraft(snapshot, &error)) { allOkay = false; break; }
        doc->record.caret = snapshot.caret;
        doc->record.firstVisibleLine = snapshot.firstVisibleLine;
        doc->backupCurrent = true;
    }
    if (!allOkay) {
        showNotice(QStringLiteral("Could not back up draft: %1").arg(error), 7000);
    } else {
        updateStatus();
    
    }
    return allOkay;
}

void MainWindow::scheduleOutline() { outlineDebounce_.start(220); }

void MainWindow::rebuildOutline(Document *document) {
    if (!document) return;
    const QString id = document->record.id;
    const quint64 revision = document->revision;
    QByteArray snapshot = document->editor->textUtf8();
    auto *watcher = new QFutureWatcher<QVector<Outline::Heading>>(this);
    connect(watcher, &QFutureWatcher<QVector<Outline::Heading>>::finished, this, [this, watcher, id, revision] {
        const auto headings = watcher->result();
        watcher->deleteLater();
        auto *doc = byId(id);
        if (!doc || doc->revision != revision) return;
        doc->headings = headings;
        applyFoldLevels(doc);
    });
    watcher->setFuture(QtConcurrent::run([snapshot = std::move(snapshot)] { return Outline::parse(snapshot); }));
}

void MainWindow::applyFoldLevels(Document *document) {
    const int lines = static_cast<int>(document->editor->send(SCI_GETLINECOUNT));
    int nextHeading = 0;
    int depth = 0;
    for (int line = 0; line < lines; ++line) {
        bool header = false;
        if (nextHeading < document->headings.size() && document->headings[nextHeading].line == line) {
            depth = document->headings[nextHeading].level;
            header = true;
            ++nextHeading;
        }
        document->editor->send(SCI_SETFOLDLEVEL, line,
            SC_FOLDLEVELBASE + std::max(0, depth - (header ? 1 : 0)) + (header ? SC_FOLDLEVELHEADERFLAG : 0));
    }
}

void MainWindow::showOutline() {
    auto *doc = current();
    if (!doc) return;
    const auto headings = Outline::parse(doc->editor->textUtf8());
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Jump to heading"));
    dialog.resize(430, 520);
    auto *layout = new QVBoxLayout(&dialog);
    auto *tree = new QTreeWidget(&dialog);
    tree->setHeaderHidden(true);
    tree->setAccessibleName(QStringLiteral("Document outline"));
    QVector<QTreeWidgetItem *> nodes;
    nodes.reserve(headings.size());
    for (int i = 0; i < headings.size(); ++i) {
        auto *item = new QTreeWidgetItem;
        item->setText(0, headings[i].title.isEmpty() ? QStringLiteral("(untitled heading)") : headings[i].title);
        item->setData(0, Qt::UserRole, i);
        if (headings[i].parent < 0) tree->addTopLevelItem(item);
        else nodes[headings[i].parent]->addChild(item);
        nodes.append(item);
    }
    tree->expandAll();
    if (tree->topLevelItemCount()) tree->setCurrentItem(tree->topLevelItem(0));
    layout->addWidget(tree);
    connect(tree, &QTreeWidget::itemActivated, &dialog, [&dialog] { dialog.accept(); });
    auto *outlineButtons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *jump = outlineButtons->addButton(QStringLiteral("Jump"), QDialogButtonBox::AcceptRole);
    jump->setEnabled(tree->currentItem() != nullptr);
    connect(outlineButtons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(outlineButtons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(outlineButtons);
    if (dialog.exec() == QDialog::Accepted && tree->currentItem()) {
        const int index = tree->currentItem()->data(0, Qt::UserRole).toInt();
        if (index >= 0 && index < headings.size()) {
            setViewMode(ViewMode::Editor);
            doc->editor->send(SCI_GOTOPOS, headings[index].start);
            doc->editor->setFocus();
        }
    }
}

void MainWindow::showLibrary(bool starredOnly) {
    if (closingLibrary_ && libraryCloseAnimation_) {
        libraryCloseAnimation_->stop();
        libraryCloseAnimation_->deleteLater();
        libraryCloseAnimation_ = nullptr;
        closingLibrary_ = false;
        library_->hide();
        library_->setWindowOpacity(1);
    }
    if (library_ && library_->isVisible()) {
        if (!starredOnly) { closeLibrary(); return; }
        library_->setStarredOnly(true);
        library_->refresh();
        return;
    }
    if (!flushBackups()) {
        showNotice(QStringLiteral("The latest draft could not be backed up"), 7000);
        return;
    }
    if (!library_) {
        library_ = new LibraryPanel(store_, this);
        library_->setAppearance(dark_);
        if (libraryDockSide_ == LibraryDockSide::Free) {
            const QByteArray saved = QSettings().value(QStringLiteral("window/libraryGeometry")).toByteArray();
            if (!saved.isEmpty()) library_->restoreGeometry(saved);
        }
        connect(library_, &LibraryPanel::dragStarted, this, [this] {
            setLibraryDockSide(LibraryDockSide::Free);
        });
        connect(library_, &LibraryPanel::dragFinished, this, &MainWindow::finishLibraryDrag);
        connect(library_, &LibraryPanel::closeRequested, this, [this] { closeLibrary(); });
        connect(library_, &LibraryPanel::copyPromptRequested, this, &MainWindow::copyPrompt);
        connect(library_, &LibraryPanel::newPromptRequested, this, [this] {
            if (!newPrompt().isEmpty()) closeLibrary();
        });
        connect(library_, &LibraryPanel::saveRequested, this, [this] {
            if (auto *doc = current(); doc && doc->record.title == QStringLiteral("New prompt")) {
                const auto headings = Outline::parse(doc->editor->textUtf8());
                if (!headings.isEmpty() && !headings.first().title.trimmed().isEmpty()) {
                    doc->record.title = headings.first().title.trimmed();
                    updateTab(doc);
                }
            }
            if (flushBackups()) {
                library_->refresh();
                showNotice(QStringLiteral("Saved in library"));
            }
        });
        connect(library_, &LibraryPanel::saveTemplateRequested, this, &MainWindow::saveAsTemplate);
        connect(library_, &LibraryPanel::previewRequested, this, [this] {
            setViewMode(ViewMode::Preview); closeLibrary(false);
        });
        connect(library_, &LibraryPanel::settingsRequested, this, &MainWindow::showLibrarySettings);
        connect(library_, &LibraryPanel::openPromptRequested, this, [this](const QString &id) {
            if (!flushBackups()) return;
            activateDraft(id); closeLibrary();
        });
        connect(library_, &LibraryPanel::openTemplateRequested, this, &MainWindow::openTemplate);
        connect(library_, &LibraryPanel::starPromptRequested, this, [this](const QString &id) {
            if (!flushBackups()) return;
            DraftRecord record = store_->loadDraft(id);
            if (record.id.isEmpty()) return;
            record.starred = !record.starred;
            QString error;
            if (!store_->saveDraft(record, &error)) QMessageBox::warning(library_, QStringLiteral("Could not update draft"), error);
            else {
                if (auto *doc = byId(id)) doc->record.starred = record.starred;
                library_->refresh();
            }
        });
        connect(library_, &LibraryPanel::duplicatePromptRequested, this, [this](const QString &id) {
            if (!flushBackups()) return;
            QByteArray text;
            if (auto *opened = byId(id)) text = opened->editor->textUtf8();
            else {
                const DraftRecord record = store_->loadDraft(id);
                if (record.modified || record.path.isEmpty()) text = record.content;
                else {
                    const auto loaded = FileIO::read(record.path);
                    if (!loaded.ok) { QMessageBox::warning(library_, QStringLiteral("Could not duplicate prompt"), loaded.error); return; }
                    text = loaded.text;
                }
            }
            const QString created = newPrompt();
            if (auto *doc = byId(created)) {
                doc->editor->setTextUtf8(text);
                doc->record.modified = true;
                scheduleBackup();
                closeLibrary();
            }
        });
        connect(library_, &LibraryPanel::removePromptRequested, this, [this](const QString &id) {
            if (QMessageBox::question(library_, QStringLiteral("Remove draft?"),
                QStringLiteral("Remove this draft and its local history? Any original file stays on disk.")) != QMessageBox::Yes) return;
            if (auto *doc = byId(id)) closeTab(tabs_->indexOf(doc->page));
            if (byId(id)) return;
            QString error;
            if (!store_->removeDraft(id, &error)) QMessageBox::warning(library_, QStringLiteral("Could not remove draft"), error);
            else library_->refresh();
        });
        connect(library_, &LibraryPanel::movePromptRequested, this, [this](const QString &id, const QString &folderId) {
            if (!flushBackups()) return;
            QString error;
            if (!store_->setDraftFolder(id, folderId, &error)) QMessageBox::warning(library_, QStringLiteral("Could not move prompt"), error);
            else {
                if (auto *doc = byId(id)) doc->record.folderId = folderId;
                library_->refresh();
            }
        });
        connect(library_, &LibraryPanel::folderRemoved, this, [this](const QString &id) {
            for (const auto &doc : documents_) if (doc->record.folderId == id) doc->record.folderId.clear();
        });
    }
    library_->setStarredOnly(starredOnly);
    library_->refresh();
    placeLibrary();
    const QPoint destination = library_->pos();
    const bool left = destination.x() < x();
    if (!reduceMotion_ && libraryDockSide_ != LibraryDockSide::Free)
        library_->move(destination + QPoint(left ? 4 : -4, 0));
    library_->setWindowOpacity(reduceMotion_ ? 1 : 0);
    library_->show();
    library_->raise();
    library_->activateWindow();
    revealLibraryTrigger(true);
    qApp->installEventFilter(this);
    if (!reduceMotion_) {
        auto *animations = new QParallelAnimationGroup(library_);
        libraryOpenAnimation_ = animations;
        auto *fade = new QPropertyAnimation(library_, "windowOpacity", animations);
        fade->setDuration(180);
        fade->setStartValue(0.0);
        fade->setEndValue(1.0);
        auto *slide = new QPropertyAnimation(library_, "pos", animations);
        slide->setDuration(180);
        slide->setStartValue(library_->pos());
        slide->setEndValue(destination);
        slide->setEasingCurve(QEasingCurve::OutCubic);
        connect(animations, &QParallelAnimationGroup::finished, this, [this] { libraryOpenAnimation_ = nullptr; });
        animations->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (auto *search = library_->findChild<QLineEdit *>(QStringLiteral("librarySearch"))) search->setFocus();
}

void MainWindow::closeLibrary(bool restoreFocus) {
    if (!library_ || !library_->isVisible() || closingLibrary_) return;
    qApp->removeEventFilter(this);
    if (libraryOpenAnimation_) {
        libraryOpenAnimation_->stop();
        libraryOpenAnimation_->deleteLater();
        libraryOpenAnimation_ = nullptr;
    }
    if (reduceMotion_) {
        library_->hide();
        library_->setWindowOpacity(1);
        revealLibraryTrigger(triggerArea_->underMouse());
    } else {
        closingLibrary_ = true;
        libraryCloseAnimation_ = new QPropertyAnimation(library_, "windowOpacity", library_);
        libraryCloseAnimation_->setDuration(120);
        libraryCloseAnimation_->setStartValue(library_->windowOpacity());
        libraryCloseAnimation_->setEndValue(0.0);
        connect(libraryCloseAnimation_, &QPropertyAnimation::finished, this, [this] {
            library_->hide();
            library_->setWindowOpacity(1);
            libraryCloseAnimation_ = nullptr;
            closingLibrary_ = false;
            revealLibraryTrigger(triggerArea_->underMouse());
        });
        libraryCloseAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
    }
    if (restoreFocus && viewMode_ != ViewMode::Preview) if (auto *doc = current()) doc->editor->setFocus();
}

void MainWindow::placeLibrary() {
    if (!library_ || placingLibrary_) return;
    placingLibrary_ = true;
    QScreen *screen = QGuiApplication::screenAt(
        libraryDockSide_ == LibraryDockSide::Free ? library_->geometry().center() : frameGeometry().center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) { placingLibrary_ = false; return; }
    const QRect available = screen->availableGeometry();
    const int gap = 12;
    if (libraryDockSide_ == LibraryDockSide::Free) {
        library_->move(qBound(available.left(), library_->x(),
                            std::max(available.left(), available.right() - library_->width() + 1)),
                       qBound(available.top(), library_->y(),
                            std::max(available.top(), available.bottom() - library_->height() + 1)));
        placingLibrary_ = false;
        return;
    }
    LibraryDockSide side = libraryDockSide_;
    if (side == LibraryDockSide::Auto) {
        const int rightRoom = available.right() - frameGeometry().right();
        const int leftRoom = frameGeometry().left() - available.left();
        side = rightRoom >= leftRoom ? LibraryDockSide::Right : LibraryDockSide::Left;
        if (available.width() < width() + library_->minimumWidth() + gap &&
            available.height() >= height() + library_->minimumHeight() + gap)
            side = available.bottom() - frameGeometry().bottom() >= frameGeometry().top() - available.top()
                ? LibraryDockSide::Bottom : LibraryDockSide::Top;
    }
    const bool horizontal = side == LibraryDockSide::Left || side == LibraryDockSide::Right;
    library_->resize(horizontal ? std::max(library_->minimumWidth(), std::min(404, available.width() - width() - gap))
                                : std::min(404, available.width()),
                     horizontal ? std::min(464, available.height())
                                : std::max(library_->minimumHeight(), std::min(464, available.height() - height() - gap)));
    QRect editor = frameGeometry();
    QPoint libraryPosition;
    if (side == LibraryDockSide::Left) {
        editor.moveLeft(qBound(available.left() + library_->width() + gap, editor.left(),
                               std::max(available.left() + library_->width() + gap, available.right() - editor.width() + 1)));
        libraryPosition = QPoint(editor.left() - library_->width() - gap,
                                 qBound(available.top(), editor.top(), available.bottom() - library_->height() + 1));
    } else if (side == LibraryDockSide::Right) {
        editor.moveLeft(qBound(available.left(), editor.left(),
                               std::max(available.left(), available.right() - editor.width() - library_->width() - gap + 1)));
        libraryPosition = QPoint(editor.right() + gap + 1,
                                 qBound(available.top(), editor.top(), available.bottom() - library_->height() + 1));
    } else if (side == LibraryDockSide::Top) {
        editor.moveTop(qBound(available.top() + library_->height() + gap, editor.top(),
                              std::max(available.top() + library_->height() + gap, available.bottom() - editor.height() + 1)));
        libraryPosition = QPoint(qBound(available.left(), editor.center().x() - library_->width() / 2,
                                          available.right() - library_->width() + 1),
                                 editor.top() - library_->height() - gap);
    } else {
        editor.moveTop(qBound(available.top(), editor.top(),
                              std::max(available.top(), available.bottom() - editor.height() - library_->height() - gap + 1)));
        libraryPosition = QPoint(qBound(available.left(), editor.center().x() - library_->width() / 2,
                                          available.right() - library_->width() + 1),
                                 editor.bottom() + gap + 1);
    }
    if (editor.topLeft() != frameGeometry().topLeft()) move(editor.topLeft());
    library_->move(libraryPosition);
    placingLibrary_ = false;
}

void MainWindow::setLibraryDockSide(LibraryDockSide side) {
    if (libraryOpenAnimation_) {
        libraryOpenAnimation_->stop();
        libraryOpenAnimation_->deleteLater();
        libraryOpenAnimation_ = nullptr;
        if (library_) library_->setWindowOpacity(1);
    }
    libraryDockSide_ = side;
    if (library_) placeLibrary();
    QSettings().setValue(QStringLiteral("window/libraryDockSide"), static_cast<int>(side));
}

void MainWindow::finishLibraryDrag() {
    if (!library_) return;
    const QRect editor = frameGeometry();
    const QRect panel = library_->frameGeometry();
    const bool overlapY = panel.bottom() >= editor.top() + 32 && panel.top() <= editor.bottom() - 32;
    const bool overlapX = panel.right() >= editor.left() + 32 && panel.left() <= editor.right() - 32;
    struct Candidate { LibraryDockSide side; int distance; };
    const Candidate candidates[] = {
        {LibraryDockSide::Left, overlapY ? std::abs(panel.right() + 1 - editor.left() + 12) : 10000},
        {LibraryDockSide::Right, overlapY ? std::abs(panel.left() - editor.right() - 13) : 10000},
        {LibraryDockSide::Top, overlapX ? std::abs(panel.bottom() + 1 - editor.top() + 12) : 10000},
        {LibraryDockSide::Bottom, overlapX ? std::abs(panel.top() - editor.bottom() - 13) : 10000}
    };
    const auto nearest = std::min_element(std::begin(candidates), std::end(candidates),
        [](const Candidate &a, const Candidate &b) { return a.distance < b.distance; });
    setLibraryDockSide(nearest->distance <= 54 ? nearest->side : LibraryDockSide::Free);
    QSettings().setValue(QStringLiteral("window/libraryGeometry"), library_->saveGeometry());
}

void MainWindow::revealLibraryTrigger(bool reveal) {
    if (!triggerEffect_) return;
    const qreal destination = reveal || alwaysShowTrigger_ || (library_ && library_->isVisible()) ? 1.0 : 0.0;
    if (triggerAnimation_) { triggerAnimation_->stop(); triggerAnimation_->deleteLater(); triggerAnimation_ = nullptr; }
    if (reduceMotion_) { triggerEffect_->setOpacity(destination); return; }
    triggerAnimation_ = new QPropertyAnimation(triggerEffect_, "opacity", this);
    triggerAnimation_->setDuration(destination > triggerEffect_->opacity() ? 100 : 140);
    triggerAnimation_->setStartValue(triggerEffect_->opacity());
    triggerAnimation_->setEndValue(destination);
    connect(triggerAnimation_, &QPropertyAnimation::finished, this, [this] { triggerAnimation_ = nullptr; });
    triggerAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::saveAsTemplate() {
    auto *doc = current();
    if (!doc || !flushBackups()) return;
    bool okay = false;
    const QString suggested = doc->record.title == QStringLiteral("New prompt") ? QString() : doc->record.title;
    const QString title = QInputDialog::getText(library_, QStringLiteral("Save as template"),
        QStringLiteral("Template name"), QLineEdit::Normal, suggested, &okay).trimmed();
    if (!okay || title.isEmpty()) return;
    TemplateRecord item;
    item.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    item.title = title;
    item.folderId = library_->selectedFolderId();
    item.content = doc->editor->textUtf8();
    QString error;
    if (!store_->saveTemplate(item, &error)) QMessageBox::warning(library_, QStringLiteral("Could not save template"), error);
    else { library_->refresh(); showNotice(QStringLiteral("Template saved locally")); }
}

void MainWindow::openTemplate(const QString &id) {
    if (!flushBackups()) return;
    const TemplateRecord source = store_->loadTemplate(id);
    if (source.id.isEmpty()) return;
    const QString created = newPrompt();
    if (auto *doc = byId(created)) {
        doc->record.title = source.title;
        doc->record.folderId = source.folderId;
        doc->editor->setTextUtf8(source.content);
        doc->record.modified = true;
        updateTab(doc);
        scheduleBackup();
        closeLibrary();
    }
}

void MainWindow::showLibrarySettings(const QPoint &position) {
    QMenu menu(library_);
    auto *placement = menu.addMenu(QStringLiteral("Library position"));
    for (const auto &[label, side] : {
             std::pair{QStringLiteral("Snap left"), LibraryDockSide::Left},
             std::pair{QStringLiteral("Snap right"), LibraryDockSide::Right},
             std::pair{QStringLiteral("Snap above"), LibraryDockSide::Top},
             std::pair{QStringLiteral("Snap below"), LibraryDockSide::Bottom},
             std::pair{QStringLiteral("Move freely"), LibraryDockSide::Free}}) {
        auto *action = placement->addAction(label, this, [this, side] { setLibraryDockSide(side); });
        action->setCheckable(true);
        action->setChecked(libraryDockSide_ == side);
    }
    menu.addSeparator();
    auto *appearance = menu.addMenu(QStringLiteral("Appearance"));
    for (const QString mode : {QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")})
        appearance->addAction(mode, this, [this, mode] { QSettings().setValue(QStringLiteral("appearance/mode"), mode); applyAppearance(); });
    menu.addSeparator();
    auto *always = menu.addAction(QStringLiteral("Always show library button"), this, [this] {
        alwaysShowTrigger_ = !alwaysShowTrigger_;
        QSettings().setValue(QStringLiteral("appearance/alwaysShowTrigger"), alwaysShowTrigger_);
        revealLibraryTrigger(alwaysShowTrigger_);
    });
    always->setCheckable(true); always->setChecked(alwaysShowTrigger_);
    auto *motion = menu.addAction(QStringLiteral("Reduce motion"), this, [this] {
        reduceMotion_ = !reduceMotion_;
        QSettings().setValue(QStringLiteral("appearance/reduceMotion"), reduceMotion_);
    });
    motion->setCheckable(true); motion->setChecked(reduceMotion_);
    menu.exec(position);
}

void MainWindow::formatSelection(const QByteArray &prefix, const QByteArray &suffix) {
    auto *doc = current();
    if (!doc) return;
    const qsizetype start = doc->editor->send(SCI_GETSELECTIONSTART);
    const qsizetype end = doc->editor->send(SCI_GETSELECTIONEND);
    const QByteArray selected = doc->editor->selectedUtf8();
    QByteArray replacement = prefix + selected + suffix;
    doc->editor->send(SCI_BEGINUNDOACTION);
    doc->editor->send(SCI_SETTARGETSTART, start);
    doc->editor->send(SCI_SETTARGETEND, end);
    doc->editor->send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<sptr_t>(replacement.constData()));
    doc->editor->send(SCI_SETSEL, start + prefix.size(), start + prefix.size() + selected.size());
    doc->editor->send(SCI_ENDUNDOACTION);
    doc->editor->setFocus();
}

void MainWindow::turnSelectionInto(const QByteArray &prefix) {
    auto *doc = current();
    if (!doc) return;
    const auto start = doc->editor->send(SCI_GETSELECTIONSTART);
    const auto line = doc->editor->send(SCI_LINEFROMPOSITION, start);
    const auto lineStart = doc->editor->send(SCI_POSITIONFROMLINE, line);
    doc->editor->send(SCI_BEGINUNDOACTION);
    doc->editor->send(SCI_SETTARGETSTART, lineStart);
    doc->editor->send(SCI_SETTARGETEND, lineStart);
    doc->editor->send(SCI_REPLACETARGET, prefix.size(), reinterpret_cast<sptr_t>(prefix.constData()));
    doc->editor->send(SCI_ENDUNDOACTION);
    doc->editor->setFocus();
}

void MainWindow::showFormattingMenu(const QPoint &position) {
    if (!current()) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("formattingMenu"));
    auto *rowAction = new QWidgetAction(&menu);
    auto *row = new QWidget(&menu);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(6, 4, 6, 4);
    layout->setSpacing(1);
    auto add = [this, row, layout, &menu](const QString &label, const QString &name, const QByteArray &prefix, const QByteArray &suffix) {
        auto *control = new QPushButton(label, row);
        control->setObjectName(name);
        control->setFixedSize(32, 28);
        control->setCursor(Qt::PointingHandCursor);
        control->setAccessibleName(name);
        layout->addWidget(control);
        connect(control, &QPushButton::clicked, &menu, [this, &menu, prefix, suffix] { menu.close(); formatSelection(prefix, suffix); });
    };
    add(QStringLiteral("B"), QStringLiteral("Bold"), "**", "**");
    add(QStringLiteral("I"), QStringLiteral("Italic"), "*", "*");
    add(QStringLiteral("U"), QStringLiteral("Underline"), "<u>", "</u>");
    add(QStringLiteral("S"), QStringLiteral("Strikethrough"), "~~", "~~");
    add(QStringLiteral("<>"), QStringLiteral("Inline code"), "`", "`");
    add(QStringLiteral("↗"), QStringLiteral("Link"), "[", "](https://)");
    rowAction->setDefaultWidget(row);
    menu.addAction(rowAction);
    menu.addSeparator();
    auto *turn = menu.addMenu(QStringLiteral("Turn into"));
    turn->addAction(QStringLiteral("Heading 1"), this, [this] { turnSelectionInto("# "); });
    turn->addAction(QStringLiteral("Heading 2"), this, [this] { turnSelectionInto("## "); });
    turn->addAction(QStringLiteral("Heading 3"), this, [this] { turnSelectionInto("### "); });
    turn->addAction(QStringLiteral("Bullet list"), this, [this] { turnSelectionInto("- "); });
    turn->addAction(QStringLiteral("Numbered list"), this, [this] { turnSelectionInto("1. "); });
    menu.addSeparator();
    menu.addAction(QStringLiteral("Copy"), this, [this] { current()->editor->send(SCI_COPY); });
    menu.addAction(QStringLiteral("Cut"), this, [this] { current()->editor->send(SCI_CUT); });
    menu.addAction(QStringLiteral("Paste"), this, [this] { current()->editor->send(SCI_PASTE); });
    QScreen *screen = QGuiApplication::screenAt(position);
    const QRect available = screen ? screen->availableGeometry() : QRect(position, QSize(500, 500));
    const QSize size = menu.sizeHint();
    menu.exec(QPoint(qBound(available.left(), position.x(), available.right() - size.width() + 1),
                     qBound(available.top(), position.y(), available.bottom() - size.height() + 1)));
}

void MainWindow::activateDraft(const QString &id) {
    if (auto *opened = byId(id)) { tabs_->setCurrentWidget(opened->page); opened->editor->setFocus(); return; }
    DraftRecord record = store_->loadDraft(id);
    if (record.id.isEmpty()) return;
    QByteArray content;
    bool recovered = record.modified;
    if (record.modified || record.path.isEmpty()) content = record.content;
    else {
        const auto loaded = FileIO::read(record.path);
        if (!loaded.ok) {
            QMessageBox::warning(this, QStringLiteral("Could not open draft"), loaded.error);
            return;
        }
        content = loaded.text;
        record.savedHash = loaded.sha256;
        record.bom = loaded.utf8Bom;
    }
    record.open = true;
    createDocument(record, content, recovered);
    scheduleBackup();
    if (auto *doc = current()) doc->editor->setFocus();
}

void MainWindow::closeTab(int index) {
    if (index < 0 || index >= tabs_->count()) return;
    auto *page = tabs_->widget(index);
    auto it = std::find_if(documents_.begin(), documents_.end(), [page](const auto &doc) { return doc->page == page; });
    if (it == documents_.end()) return;
    Document *doc = it->get();
    if (!flushBackups()) {
        QMessageBox::warning(this, QStringLiteral("Could not back up draft"),
                             QStringLiteral("The local backup failed. Keep this tab open and retry."));
        return;
    }
    QString error;
    if (!store_->setOpen(doc->record.id, false, &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not close draft"), error);
        return;
    }
    if (doc->waiting) emit waitFinished(doc->record.id, false);
    if (!doc->record.path.isEmpty() && watcher_.files().contains(doc->record.path)) watcher_.removePath(doc->record.path);
    tabs_->removeTab(index);
    tabs_->tabBar()->setVisible(tabs_->count() > 1);
    page->deleteLater();
    documents_.erase(it);
    if (documents_.empty()) newPrompt();

    updateStatus();
    if (viewMode_ != ViewMode::Editor) renderPreview();
}

void MainWindow::openDialog() {
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open prompt"), {},
                                                      QStringLiteral("Text and Markdown (*.md *.markdown *.txt *.xml *.json *.yaml *.yml);;All files (*)"));
    if (!path.isEmpty()) openPath(path);
}

bool MainWindow::saveDocument(Document *document, bool saveAs) {
    if (!document) return false;
    QString path = document->record.path;
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save prompt"),
                    path.isEmpty() ? QDir::homePath() + QStringLiteral("/prompt.md") : path,
                    QStringLiteral("Markdown (*.md);;Text (*.txt);;All files (*)"));
        if (path.isEmpty()) return false;
        path = QFileInfo(path).absoluteFilePath();
        for (const auto &other : documents_) if (other.get() != document && other->record.path == canonicalOrAbsolute(path)) {
            QMessageBox::warning(this, QStringLiteral("File already open"),
                                 QStringLiteral("This file is already open in another tab."));
            return false;
        }
    }
    const bool newDestination = path != document->record.path;
    QByteArray expected = newDestination ? QByteArray{} : document->record.savedHash;
    bool expectAbsent = newDestination || expected.isEmpty();
    if (newDestination && QFileInfo::exists(path)) {
        if (QMessageBox::question(this, QStringLiteral("Replace file?"),
            QStringLiteral("Replace the existing file at this path?")) != QMessageBox::Yes) return false;
        const auto existing = FileIO::read(path);
        if (!existing.ok) {
            QMessageBox::warning(this, QStringLiteral("Could not replace file"), existing.error);
            return false;
        }
        expected = existing.sha256;
        expectAbsent = false;
    }
    const auto saved = FileIO::save(path, document->editor->textUtf8(), document->record.bom, expected, expectAbsent);
    if (!saved.ok) {
        if (saved.conflict) {
            QMessageBox box(QMessageBox::Warning, QStringLiteral("File changed outside %1").arg(AppIdentity::displayName()),
                saved.error + QStringLiteral("\n\nYour draft remains backed up locally. Choose Save As to keep both versions."),
                QMessageBox::NoButton, this);
            auto *saveCopy = box.addButton(QStringLiteral("Save As…"), QMessageBox::ActionRole);
            box.addButton(QStringLiteral("Keep editing"), QMessageBox::RejectRole);
            box.exec();
            if (box.clickedButton() == saveCopy) return saveDocument(document, true);
        } else QMessageBox::warning(this, QStringLiteral("Could not save"), saved.error);
        updateStatus();
        return false;
    }
    const QString oldPath = document->record.path;
    document->record.path = saved.canonicalPath;
    document->record.title = QFileInfo(saved.canonicalPath).fileName();
    document->record.savedHash = saved.sha256;
    document->record.modified = false;
    document->record.bom = saved.utf8Bom;
    document->changedExternally = false;
    document->editor->send(SCI_SETSAVEPOINT);
    if (!oldPath.isEmpty() && oldPath != saved.canonicalPath && watcher_.files().contains(oldPath)) watcher_.removePath(oldPath);
    if (!watcher_.files().contains(saved.canonicalPath)) watcher_.addPath(saved.canonicalPath);
    updateTab(document);
    if (!flushBackups()) QMessageBox::warning(this, QStringLiteral("Saved to file"),
        QStringLiteral("The file was saved, but the local session backup failed."));
    updateStatus();
    showNotice(QStringLiteral("Saved to file"), 3500);
    return true;
}

void MainWindow::saveAndReturn() {
    auto *doc = current();
    if (!doc || !doc->waiting) return;
    if (!saveDocument(doc)) return;
    doc->waiting = false;
    emit waitFinished(doc->record.id, true);
    updateStatus();
}

void MainWindow::copyPrompt() {
    auto *doc = current();
    if (!doc) return;
    QApplication::clipboard()->setText(QString::fromUtf8(doc->editor->textUtf8()), QClipboard::Clipboard);
    showNotice(QStringLiteral("Copied"), 3500);
}

void MainWindow::sectionAction(const QString &action) {
    auto *doc = current();
    if (!doc) return;
    const QByteArray source = doc->editor->textUtf8();
    const auto headings = Outline::parse(source);
    const int index = Outline::sectionAt(headings, doc->editor->send(SCI_GETCURRENTPOS));
    if (index < 0) { showNotice(QStringLiteral("Place the caret inside a section"), 3500); return; }
    const auto &heading = headings[index];
    const QByteArray section = source.mid(heading.start, heading.sectionEnd - heading.start);
    if (action == QStringLiteral("Select section")) {
        doc->editor->send(SCI_SETSEL, heading.start, heading.sectionEnd);
    } else if (action == QStringLiteral("Copy section")) {
        QApplication::clipboard()->setText(QString::fromUtf8(section));
        showNotice(QStringLiteral("Section copied"), 3500);
    } else if (action == QStringLiteral("Duplicate section")) {
        QByteArray inserted = section;
        if (heading.sectionEnd == source.size() && !section.endsWith('\n') && !section.endsWith('\r')) inserted.prepend('\n');
        doc->editor->send(SCI_BEGINUNDOACTION);
        doc->editor->send(SCI_SETTARGETSTART, heading.sectionEnd);
        doc->editor->send(SCI_SETTARGETEND, heading.sectionEnd);
        doc->editor->send(SCI_REPLACETARGET, inserted.size(), reinterpret_cast<sptr_t>(inserted.constData()));
        doc->editor->send(SCI_ENDUNDOACTION);
    } else if (action == QStringLiteral("Move section up") || action == QStringLiteral("Move section down")) {
        const auto edit = Outline::move(source, headings, index, action == QStringLiteral("Move section down"));
        if (!edit.valid) { showNotice(QStringLiteral("No adjacent section at this level"), 3500); return; }
        doc->editor->send(SCI_BEGINUNDOACTION);
        doc->editor->send(SCI_SETTARGETSTART, edit.start);
        doc->editor->send(SCI_SETTARGETEND, edit.end);
        doc->editor->send(SCI_REPLACETARGET, edit.replacement.size(), reinterpret_cast<sptr_t>(edit.replacement.constData()));
        doc->editor->send(SCI_GOTOPOS, edit.caret);
        doc->editor->send(SCI_ENDUNDOACTION);
    } else if (action == QStringLiteral("Toggle section fold")) {
        doc->editor->send(SCI_TOGGLEFOLD, heading.line);
    }
    doc->editor->setFocus();
}

void MainWindow::onExternalChange(const QString &path) {
    if (QFileInfo::exists(path) && !watcher_.files().contains(path)) watcher_.addPath(path);
    for (const auto &doc : documents_) if (doc->record.path == path) {
        const auto read = FileIO::read(path);
        if (!read.ok || read.sha256 != doc->record.savedHash) {
            doc->changedExternally = true;
            if (doc.get() == current()) showNotice(QStringLiteral("File changed outside %1 — review before saving").arg(AppIdentity::displayName()), 7000);
        }
    }
    updateStatus();
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (!flushBackups()) {
        if (QMessageBox::question(this, QStringLiteral("Local backup failed"),
            QStringLiteral("Could not back up your latest changes. Quit anyway?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) {
            event->ignore(); return;
        }
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/padGeometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/libraryDockSide"), static_cast<int>(libraryDockSide_));
    if (library_) settings.setValue(QStringLiteral("window/libraryGeometry"), library_->saveGeometry());
    if (library_) library_->hide();
    for (const auto &doc : documents_) if (doc->waiting) emit waitFinished(doc->record.id, false);
    event->accept();
}

void MainWindow::showFind(bool replace) {
    auto *doc = current();
    if (!doc) return;
    if (viewMode_ == ViewMode::Preview) setViewMode(ViewMode::Editor);
    buildFindBar();
    const qsizetype start = doc->editor->send(SCI_GETSELECTIONSTART);
    const qsizetype end = doc->editor->send(SCI_GETSELECTIONEND);
    selectionScopeStart_ = start;
    selectionScopeEnd_ = end;
    const QByteArray selected = doc->editor->selectedUtf8();
    if (!selected.isEmpty() && selected.size() < 120 && !selected.contains('\n')) findEdit_->setText(QString::fromUtf8(selected));
    findBar_->show();
    replaceRow_->setVisible(replace);
    findOptionsRow_->setVisible(findOptionsButton_->isChecked());
    findEdit_->setFocus();
    findEdit_->selectAll();
    updateMatches();
}

void MainWindow::updateMatches(bool selectNext, bool backwards) {
    auto *doc = current();
    if (!doc || !findBar_->isVisible()) return;
    const QByteArray source = doc->editor->textUtf8();
    const qsizetype start = scopeBox_->currentIndex() == 1 ? selectionScopeStart_ : 0;
    const qsizetype end = scopeBox_->currentIndex() == 1 ? selectionScopeEnd_ : source.size();
    const auto found = FindReplace::find(source, findEdit_->text(), replaceEdit_->text(),
        regexCheck_->isChecked(), caseCheck_->isChecked(), wordCheck_->isChecked(), start, end);
    if (!found.error.isEmpty()) {
        matchCount_->setText(QStringLiteral("Invalid regex"));
        matchCount_->setToolTip(found.error);
        return;
    }
    matchCount_->setToolTip({});
    matchCount_->setText(found.truncated ? QStringLiteral("%1+ matches").arg(found.matches.size())
                                            : QStringLiteral("%1 matches").arg(found.matches.size()));
    if (!selectNext || found.matches.isEmpty()) return;
    const qsizetype caret = doc->editor->send(SCI_GETCURRENTPOS);
    const FindReplace::Match *choice = nullptr;
    if (backwards) {
        for (const auto &match : found.matches) if (match.start < caret) choice = &match;
        if (!choice) choice = &found.matches.last();
    } else {
        for (const auto &match : found.matches) if (match.start > caret || (match.start == caret && match.end > caret)) { choice = &match; break; }
        if (!choice) choice = &found.matches.first();
    }
    doc->editor->send(SCI_SETSEL, choice->start, choice->end);
    doc->editor->send(SCI_SCROLLCARET);
    doc->editor->setFocus();
}

void MainWindow::replaceOne() {
    auto *doc = current();
    if (!doc) return;
    const QByteArray source = doc->editor->textUtf8();
    const auto found = FindReplace::find(source, findEdit_->text(), replaceEdit_->text(),
        regexCheck_->isChecked(), caseCheck_->isChecked(), wordCheck_->isChecked(),
        scopeBox_->currentIndex() == 1 ? selectionScopeStart_ : 0,
        scopeBox_->currentIndex() == 1 ? selectionScopeEnd_ : source.size());
    if (!found.error.isEmpty()) { matchCount_->setText(QStringLiteral("Invalid regex")); return; }
    const qsizetype begin = doc->editor->send(SCI_GETSELECTIONSTART);
    const qsizetype end = doc->editor->send(SCI_GETSELECTIONEND);
    const auto it = std::find_if(found.matches.begin(), found.matches.end(), [begin, end](const auto &match) {
        return match.start == begin && match.end == end;
    });
    if (it == found.matches.end()) { updateMatches(true); return; }
    doc->editor->replaceSelection(replaceEdit_->text().toUtf8());
    updateMatches(true);
}

void MainWindow::replaceAll() {
    auto *doc = current();
    if (!doc) return;
    const QByteArray source = doc->editor->textUtf8();
    const auto found = FindReplace::find(source, findEdit_->text(), replaceEdit_->text(),
        regexCheck_->isChecked(), caseCheck_->isChecked(), wordCheck_->isChecked(),
        scopeBox_->currentIndex() == 1 ? selectionScopeStart_ : 0,
        scopeBox_->currentIndex() == 1 ? selectionScopeEnd_ : source.size());
    if (!found.error.isEmpty()) { matchCount_->setText(QStringLiteral("Invalid regex")); return; }
    if (found.truncated) {
        QMessageBox::information(this, QStringLiteral("Too many matches"),
                                 QStringLiteral("Replace all is limited to 10,000 matches at a time. Narrow the search."));
        return;
    }
    if (found.matches.isEmpty()) return;
    const QByteArray replacement = replaceEdit_->text().toUtf8();
    doc->editor->send(SCI_BEGINUNDOACTION);
    for (auto it = found.matches.crbegin(); it != found.matches.crend(); ++it) {
        doc->editor->send(SCI_SETTARGETSTART, it->start);
        doc->editor->send(SCI_SETTARGETEND, it->end);
        doc->editor->send(SCI_REPLACETARGET, replacement.size(), reinterpret_cast<sptr_t>(replacement.constData()));
    }
    doc->editor->send(SCI_ENDUNDOACTION);
    showNotice(QStringLiteral("Replaced %1 matches").arg(found.matches.size()), 4000);
    updateMatches();
}

void MainWindow::showSnippets() {
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Snippets"));
    dialog.resize(480, 470);
    auto *layout = new QVBoxLayout(&dialog);
    snippets_ = new QListWidget(&dialog);
    snippets_->setAccessibleName(QStringLiteral("Snippets"));
    layout->addWidget(snippets_, 1);
    auto *buttons = new QHBoxLayout;
    auto *insert = button(QStringLiteral("Insert"), &dialog);
    auto *create = button(QStringLiteral("From selection"), &dialog);
    auto *import = button(QStringLiteral("Import…"), &dialog);
    auto *exportButton = button(QStringLiteral("Export…"), &dialog);
    auto *close = button(QStringLiteral("Close"), &dialog);
    buttons->addWidget(insert);
    buttons->addWidget(create);
    buttons->addWidget(import);
    buttons->addWidget(exportButton);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(insert, &QPushButton::clicked, &dialog, [this, &dialog] { insertSnippet(); dialog.accept(); });
    connect(snippets_, &QListWidget::itemDoubleClicked, &dialog, [this, &dialog] { insertSnippet(); dialog.accept(); });
    connect(create, &QPushButton::clicked, &dialog, [this] { createSnippet(); });
    connect(import, &QPushButton::clicked, &dialog, [this] { importSnippet(); });
    connect(exportButton, &QPushButton::clicked, &dialog, [this] { exportSnippet(); });
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    refreshSnippets();
    create->setEnabled(current() && !current()->editor->selectedUtf8().isEmpty());
    auto updateButtons = [this, insert, exportButton] {
        const bool chosen = snippets_ && snippets_->currentItem();
        insert->setEnabled(chosen);
        exportButton->setEnabled(chosen);
    };
    connect(snippets_, &QListWidget::currentItemChanged, &dialog, updateButtons);
    updateButtons();
    dialog.exec();
    snippets_ = nullptr;
    if (auto *doc = current()) if (viewMode_ != ViewMode::Preview) doc->editor->setFocus();
}

void MainWindow::refreshSnippets() {
    if (!snippets_) return;
    snippets_->clear();
    for (const auto &snippet : store_->snippets()) {
        auto *item = new QListWidgetItem(snippet.title, snippets_);
        item->setData(Qt::UserRole, snippet.id);
        item->setToolTip(snippet.path);
    }
    if (snippets_->count()) snippets_->setCurrentRow(0);
    snippets_->setContextMenuPolicy(Qt::CustomContextMenu);
    snippets_->disconnect(this);
    connect(snippets_, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto *item = snippets_->itemAt(point);
        if (!item) return;
        const QString id = item->data(Qt::UserRole).toString();
        QMenu menu(this);
        menu.addAction(QStringLiteral("Remove snippet"), this, [this, id] {
            for (const auto &snippet : store_->snippets()) if (snippet.id == id) {
                QString error;
                if (!store_->removeSnippet(id, &error)) { QMessageBox::warning(this, QStringLiteral("Could not remove snippet"), error); return; }
                QFile::remove(snippet.path);
                refreshSnippets();
                return;
            }
        });
        menu.exec(snippets_->viewport()->mapToGlobal(point));
    });
}

void MainWindow::createSnippet() {
    auto *doc = current();
    if (!doc) return;
    const QByteArray selection = doc->editor->selectedUtf8();
    if (selection.isEmpty()) { showNotice(QStringLiteral("Select text to make a snippet"), 4000); return; }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create snippet"), QStringLiteral("Name"),
                                                QLineEdit::Normal, {}, &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString folder = QDir(store_->directory()).filePath(QStringLiteral("snippets"));
    if (!QDir().mkpath(folder)) { QMessageBox::warning(this, QStringLiteral("Could not create snippet"), QStringLiteral("Could not create snippet directory.")); return; }
    const QString path = QDir(folder).filePath(id + QStringLiteral(".md"));
    const auto saved = FileIO::save(path, selection, false, {}, true);
    if (!saved.ok) { QMessageBox::warning(this, QStringLiteral("Could not create snippet"), saved.error); return; }
    QString error;
    if (!store_->saveSnippet({id, name, path}, &error)) { QMessageBox::warning(this, QStringLiteral("Could not create snippet"), error); return; }
    refreshSnippets();
    showNotice(QStringLiteral("Snippet saved locally"), 3500);
}

void MainWindow::insertSnippet() {
    if (!snippets_) { showSnippets(); return; }
    auto *doc = current();
    auto *item = snippets_->currentItem();
    if (!doc || !item) { showNotice(QStringLiteral("Choose a snippet first"), 3500); return; }
    for (const auto &snippet : store_->snippets()) if (snippet.id == item->data(Qt::UserRole).toString()) {
        const auto read = FileIO::read(snippet.path);
        if (!read.ok) { QMessageBox::warning(this, QStringLiteral("Could not read snippet"), read.error); return; }
        doc->editor->send(SCI_BEGINUNDOACTION);
        doc->editor->replaceSelection(read.text);
        doc->editor->send(SCI_ENDUNDOACTION);
        doc->editor->setFocus();
        return;
    }
}

void MainWindow::importSnippet() {
    const QString source = QFileDialog::getOpenFileName(this, QStringLiteral("Import snippet"), {},
                                                        QStringLiteral("Text and Markdown (*.md *.txt);;All files (*)"));
    if (source.isEmpty()) return;
    const auto loaded = FileIO::read(source);
    if (!loaded.ok) { QMessageBox::warning(this, QStringLiteral("Could not import snippet"), loaded.error); return; }
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Import snippet"), QStringLiteral("Name"),
        QLineEdit::Normal, QFileInfo(source).completeBaseName(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString folder = QDir(store_->directory()).filePath(QStringLiteral("snippets"));
    if (!QDir().mkpath(folder)) { QMessageBox::warning(this, QStringLiteral("Could not import snippet"), QStringLiteral("Could not create snippet directory.")); return; }
    const QString path = QDir(folder).filePath(id + QStringLiteral(".md"));
    const auto saved = FileIO::save(path, loaded.text, loaded.utf8Bom, {}, true);
    if (!saved.ok) { QMessageBox::warning(this, QStringLiteral("Could not import snippet"), saved.error); return; }
    QString error;
    if (!store_->saveSnippet({id, name, path}, &error)) { QMessageBox::warning(this, QStringLiteral("Could not import snippet"), error); return; }
    refreshSnippets();
}

void MainWindow::exportSnippet() {
    if (!snippets_) { showSnippets(); return; }
    auto *item = snippets_->currentItem();
    if (!item) { showNotice(QStringLiteral("Choose a snippet first"), 3500); return; }
    for (const auto &snippet : store_->snippets()) if (snippet.id == item->data(Qt::UserRole).toString()) {
        const auto read = FileIO::read(snippet.path);
        if (!read.ok) { QMessageBox::warning(this, QStringLiteral("Could not export snippet"), read.error); return; }
        const QString target = QFileDialog::getSaveFileName(this, QStringLiteral("Export snippet"),
            QDir::homePath() + QStringLiteral("/") + snippet.title + QStringLiteral(".md"),
            QStringLiteral("Markdown (*.md);;Text (*.txt);;All files (*)"));
        if (target.isEmpty()) return;
        if (QFileInfo::exists(target) && QMessageBox::question(this, QStringLiteral("Replace file?"),
            QStringLiteral("Replace the existing file?")) != QMessageBox::Yes) return;
        QByteArray expected;
        if (QFileInfo::exists(target)) {
            const auto old = FileIO::read(target);
            if (!old.ok) { QMessageBox::warning(this, QStringLiteral("Could not export snippet"), old.error); return; }
            expected = old.sha256;
        }
        const auto saved = FileIO::save(target, read.text, read.utf8Bom, expected, expected.isEmpty());
        if (!saved.ok) QMessageBox::warning(this, QStringLiteral("Could not export snippet"), saved.error);
        else showNotice(QStringLiteral("Snippet exported"), 3500);
        return;
    }
}

void MainWindow::checkpoint() {
    auto *doc = current();
    if (!doc || !flushBackups()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Create checkpoint"), QStringLiteral("Name"),
        QLineEdit::Normal, QStringLiteral("Checkpoint %1").arg(QDateTime::currentDateTime().toString(QStringLiteral("dd MMM HH:mm"))), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    QString error;
    if (!store_->saveCheckpoint(doc->record.id, name, doc->editor->textUtf8(), &error))
        QMessageBox::warning(this, QStringLiteral("Could not create checkpoint"), error);
    else showNotice(QStringLiteral("Checkpoint saved locally"), 3500);
}

void MainWindow::showCheckpoints() {
    auto *doc = current();
    if (!doc) return;
    const auto records = store_->checkpoints(doc->record.id);
    if (records.isEmpty()) { QMessageBox::information(this, QStringLiteral("Checkpoints"), QStringLiteral("No checkpoints for this prompt yet.")); return; }
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Compare and restore checkpoint"));
    dialog.resize(1000, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *list = new QListWidget(&dialog);
    list->setMaximumHeight(130);
    for (const auto &record : records) {
        auto *item = new QListWidgetItem(record.name + QStringLiteral(" · ") +
            QDateTime::fromSecsSinceEpoch(record.createdAt).toString(QStringLiteral("dd MMM yyyy, HH:mm")), list);
        item->setData(Qt::UserRole, record.id);
    }
    layout->addWidget(list);
    auto *split = new QSplitter(Qt::Horizontal, &dialog);
    auto *currentText = new QPlainTextEdit(split);
    currentText->setReadOnly(true);
    currentText->setPlainText(QString::fromUtf8(doc->editor->textUtf8()));
    currentText->setAccessibleName(QStringLiteral("Current prompt"));
    auto *oldText = new QPlainTextEdit(split);
    oldText->setReadOnly(true);
    oldText->setAccessibleName(QStringLiteral("Selected checkpoint"));
    layout->addWidget(split, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    auto *restore = buttons->addButton(QStringLiteral("Restore checkpoint"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(list, &QListWidget::currentItemChanged, &dialog, [this, oldText](QListWidgetItem *item) {
        if (item) oldText->setPlainText(QString::fromUtf8(store_->loadCheckpoint(item->data(Qt::UserRole).toLongLong())));
    });
    connect(restore, &QPushButton::clicked, &dialog, [this, doc, list, &dialog] {
        if (!list->currentItem()) return;
        QString error;
        if (!store_->saveCheckpoint(doc->record.id, QStringLiteral("Before restore"), doc->editor->textUtf8(), &error)) {
            QMessageBox::warning(&dialog, QStringLiteral("Could not preserve current draft"), error); return;
        }
        const QByteArray content = store_->loadCheckpoint(list->currentItem()->data(Qt::UserRole).toLongLong());
        doc->editor->send(SCI_BEGINUNDOACTION);
        doc->editor->send(SCI_SELECTALL);
        doc->editor->replaceSelection(content);
        doc->editor->send(SCI_ENDUNDOACTION);
        dialog.accept();
        doc->editor->setFocus();
    });
    layout->addWidget(buttons);
    list->setCurrentRow(0);
    dialog.exec();
}

void MainWindow::showPalette(bool documentsOnly) {
    if (!flushBackups()) showNotice(QStringLiteral("The latest draft could not be backed up"), 7000);
    QDialog dialog(this);
    dialog.setWindowTitle(documentsOnly ? QStringLiteral("Quick document switch") : QStringLiteral("Command palette"));
    dialog.resize(520, 460);
    auto *layout = new QVBoxLayout(&dialog);
    auto *search = new QLineEdit(&dialog);
    search->setPlaceholderText(documentsOnly ? QStringLiteral("Find a draft") : QStringLiteral("Type a command or draft name"));
    layout->addWidget(search);
    auto *list = new QListWidget(&dialog);
    layout->addWidget(list, 1);
    struct Entry { QString label; std::function<void()> invoke; };
    std::vector<Entry> entries;
    if (!documentsOnly) {
        entries = {
            {QStringLiteral("New prompt"), [this] { newPrompt(); }},
            {QStringLiteral("Open file"), [this] { openDialog(); }},
            {QStringLiteral("Save"), [this] { saveDocument(current()); }},
            {QStringLiteral("Copy Entire Prompt"), [this] { copyPrompt(); }},
            {QStringLiteral("Find and replace"), [this] { showFind(true); }},
            {QStringLiteral("Create checkpoint"), [this] { checkpoint(); }},
            {QStringLiteral("Editor view"), [this] { setViewMode(ViewMode::Editor); }},
            {QStringLiteral("Preview"), [this] { setViewMode(ViewMode::Preview); }},
            {QStringLiteral("Split Preview"), [this] { setViewMode(ViewMode::SplitPreview); }},
            {QStringLiteral("Browse drafts"), [this] { showLibrary(); }},
            {QStringLiteral("Jump to heading"), [this] { showOutline(); }},
            {QStringLiteral("Snippets"), [this] { showSnippets(); }}
        };
    }
    for (const auto &draft : store_->listDrafts()) {
        entries.push_back({QStringLiteral("Open: ") + draft.title, [this, id = draft.id] { activateDraft(id); }});
    }
    auto rebuild = [list, search, &entries] {
        list->clear();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) if (entries[i].label.contains(search->text(), Qt::CaseInsensitive)) {
            auto *item = new QListWidgetItem(entries[i].label, list);
            item->setData(Qt::UserRole, i);
        }
        if (list->count()) list->setCurrentRow(0);
    };
    connect(search, &QLineEdit::textChanged, &dialog, rebuild);
    connect(search, &QLineEdit::returnPressed, &dialog, [&dialog] { dialog.accept(); });
    connect(list, &QListWidget::itemDoubleClicked, &dialog, [&dialog] { dialog.accept(); });
    rebuild();
    search->setFocus();
    if (dialog.exec() == QDialog::Accepted && list->currentItem()) entries[list->currentItem()->data(Qt::UserRole).toInt()].invoke();
}
