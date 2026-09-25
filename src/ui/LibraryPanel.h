#pragma once

#include "storage/Store.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;

class LibraryPanel final : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPanel(Store *store, QWidget *parent = nullptr);
    void refresh();
    void setAppearance(bool dark);
    void setStarredOnly(bool on);
    QString selectedFolderId() const;

signals:
    void dragStarted();
    void dragFinished();
    void closeRequested();
    void copyPromptRequested();
    void newPromptRequested();
    void saveRequested();
    void saveTemplateRequested();
    void previewRequested();
    void settingsRequested(const QPoint &globalPosition);
    void openPromptRequested(const QString &id);
    void openTemplateRequested(const QString &id);
    void starPromptRequested(const QString &id);
    void duplicatePromptRequested(const QString &id);
    void removePromptRequested(const QString &id);
    void movePromptRequested(const QString &id, const QString &folderId);
    void folderRemoved(const QString &id);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    Store *store_;
    QLineEdit *search_ = nullptr;
    QLabel *title_ = nullptr;
    QListWidget *folders_ = nullptr;
    QListWidget *items_ = nullptr;
    QPushButton *copyButton_ = nullptr;
    QPushButton *newButton_ = nullptr;
    QPushButton *closeButton_ = nullptr;
    QPushButton *folderButton_ = nullptr;
    QPushButton *saveButton_ = nullptr;
    QPushButton *templateButton_ = nullptr;
    QPushButton *previewButton_ = nullptr;
    QPushButton *settingsButton_ = nullptr;
    QPushButton *allFilter_ = nullptr;
    QPushButton *promptsFilter_ = nullptr;
    QPushButton *templatesFilter_ = nullptr;
    bool dark_ = false;
    bool starredOnly_ = false;
    QWidget *dragHandle_ = nullptr;
    bool dragging_ = false;
    QPoint dragStartGlobal_;
    QPoint dragStartPosition_;

    void refreshFolders();
    void refreshItems();
    void openItem(QListWidgetItem *item);
    void newFolder();
    void showFolderMenu(const QPoint &point);
    void showItemMenu(const QPoint &point);
};
