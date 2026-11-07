#pragma once

#include "agents/AgentConfigs.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Lists the instruction Markdown of locally installed coding agents
// (Claude Code, Codex, Hermes Agent, Cursor, …) and lets the user open one in
// the editor. Extra folders the user adds are remembered in QSettings.
class AgentFilesDialog final : public QDialog {
    Q_OBJECT
public:
    explicit AgentFilesDialog(QWidget *parent = nullptr);

    static QStringList customFolders();
    static void setCustomFolders(const QStringList &folders);

signals:
    // `exists` is false for a well-known file that has not been created yet.
    void openRequested(const QString &path, bool exists);

private:
    QLineEdit *filter_ = nullptr;
    QTreeWidget *tree_ = nullptr;
    QLabel *details_ = nullptr;
    QPushButton *open_ = nullptr;
    QPushButton *remove_ = nullptr;

    void refresh();
    void applyFilter();
    void updateSelection();
    void openCurrent();
    void addFolder();
    void removeFolder();
    QTreeWidgetItem *agentItem(QTreeWidgetItem *item) const;
};
