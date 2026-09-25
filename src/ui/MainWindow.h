#pragma once

#include "documents/Outline.h"
#include "storage/Store.h"

#include <QFileSystemWatcher>
#include <QMainWindow>
#include <QTimer>

#include <memory>
#include <vector>

class PromptEditor;
class LibraryPanel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QSplitter;
class QTabWidget;
class QTextBrowser;
class QVBoxLayout;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    enum class ViewMode { Editor, Preview, SplitPreview };
    enum class LibraryDockSide { Auto, Left, Right, Top, Bottom, Free };
    explicit MainWindow(Store *store, bool createBlankIfEmpty = true, QWidget *parent = nullptr);
    QString newPrompt();
    QString openPath(const QString &path, bool allowMissing = false);
    bool attachWait(const QString &id);
    void cancelWait(const QString &id);
    bool hasDocument(const QString &id) const;
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return viewMode_; }
    bool hasPreviewWidget() const { return preview_ != nullptr; }
    void setLibraryDockSide(LibraryDockSide side);
    LibraryDockSide libraryDockSide() const { return libraryDockSide_; }

signals:
    void waitFinished(const QString &id, bool success);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    struct Document {
        DraftRecord record;
        PromptEditor *editor = nullptr;
        QWidget *page = nullptr;
        QVector<Outline::Heading> headings;
        quint64 revision = 0;
        bool changedExternally = false;
        bool waiting = false;
        bool backupCurrent = false;
    };

    Store *store_;
    std::vector<std::unique_ptr<Document>> documents_;
    QSplitter *splitter_ = nullptr;
    QVBoxLayout *rootLayout_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QWidget *root_ = nullptr;
    QWidget *triggerArea_ = nullptr;
    QPushButton *libraryTrigger_ = nullptr;
    QGraphicsOpacityEffect *triggerEffect_ = nullptr;
    QPropertyAnimation *triggerAnimation_ = nullptr;
    QPropertyAnimation *libraryCloseAnimation_ = nullptr;
    QParallelAnimationGroup *libraryOpenAnimation_ = nullptr;
    LibraryPanel *library_ = nullptr;
    QWidget *previewContainer_ = nullptr;
    QWidget *previewHeader_ = nullptr;
    QTextBrowser *preview_ = nullptr;
    QListWidget *snippets_ = nullptr;
    QLineEdit *findEdit_ = nullptr;
    QLineEdit *replaceEdit_ = nullptr;
    QCheckBox *caseCheck_ = nullptr;
    QCheckBox *wordCheck_ = nullptr;
    QCheckBox *regexCheck_ = nullptr;
    QComboBox *scopeBox_ = nullptr;
    QLabel *matchCount_ = nullptr;
    QWidget *findBar_ = nullptr;
    QWidget *replaceRow_ = nullptr;
    QWidget *findOptionsRow_ = nullptr;
    QPushButton *findOptionsButton_ = nullptr;
    QAction *saveReturnMenuAction_ = nullptr;
    QAction *editorAction_ = nullptr;
    QAction *previewAction_ = nullptr;
    QAction *splitAction_ = nullptr;
    QFileSystemWatcher watcher_;
    QTimer backupDebounce_;
    QTimer backupMaximum_;
    QTimer outlineDebounce_;
    QTimer previewDebounce_;
    ViewMode viewMode_ = ViewMode::Editor;
    QString previewDocumentId_;
    quint64 previewRevision_ = 0;
    qsizetype selectionScopeStart_ = 0;
    qsizetype selectionScopeEnd_ = 0;
    bool dark_ = false;
    bool reduceMotion_ = false;
    bool alwaysShowTrigger_ = false;
    bool placingLibrary_ = false;
    bool closingLibrary_ = false;
    LibraryDockSide libraryDockSide_ = LibraryDockSide::Auto;
    bool movingWindow_ = false;
    Qt::Edges resizingWindow_;
    QPoint dragOrigin_;
    QRect dragGeometry_;

    void buildUi();
    void buildMenus();
    void applyAppearance();
    void buildFindBar();
    void hideFindBar();
    void renderPreview();
    void showNotice(const QString &text, int milliseconds = 3500);
    Document *current() const;
    Document *byId(const QString &id) const;
    Document *createDocument(const DraftRecord &record, const QByteArray &text, bool recovered);
    void restoreSession();
    void updateTab(Document *document);
    void updateStatus();
    void scheduleBackup();
    bool flushBackups();
    void scheduleOutline();
    void rebuildOutline(Document *document);
    void applyFoldLevels(Document *document);
    void showLibrary(bool starredOnly = false);
    void closeLibrary(bool restoreFocus = true);
    void placeLibrary();
    void finishLibraryDrag();
    void revealLibraryTrigger(bool reveal);
    void saveAsTemplate();
    void openTemplate(const QString &id);
    void showLibrarySettings(const QPoint &globalPosition);
    void showFormattingMenu(const QPoint &globalPosition);
    void formatSelection(const QByteArray &prefix, const QByteArray &suffix);
    void turnSelectionInto(const QByteArray &prefix);
    void showOutline();
    void refreshSnippets();
    void activateDraft(const QString &id);
    void closeTab(int index);
    void openDialog();
    bool saveDocument(Document *document, bool saveAs = false);
    void saveAndReturn();
    void copyPrompt();
    void sectionAction(const QString &action);
    void showFind(bool replace);
    void updateMatches(bool selectNext = false, bool backwards = false);
    void replaceOne();
    void replaceAll();
    void createSnippet();
    void showSnippets();
    void insertSnippet();
    void importSnippet();
    void exportSnippet();
    void checkpoint();
    void showCheckpoints();
    void showPalette(bool documentsOnly = false);
    void onExternalChange(const QString &path);
};
