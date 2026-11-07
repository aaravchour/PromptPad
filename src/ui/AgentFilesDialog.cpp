#include "ui/AgentFilesDialog.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

namespace {
constexpr int kPathRole = Qt::UserRole;
constexpr int kExistsRole = Qt::UserRole + 1;
constexpr int kRootRole = Qt::UserRole + 2;
constexpr int kCustomRole = Qt::UserRole + 3;
const QString kSettingsKey = QStringLiteral("agents/customFolders");

QString displayPath(const QString &path) {
    const QString home = QDir::homePath();
    if (path == home) return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/'))) return QStringLiteral("~") + path.mid(home.size());
    return QDir::toNativeSeparators(path);
}
}

AgentFilesDialog::AgentFilesDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Agent files"));
    resize(560, 560);
    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(QStringLiteral(
        "Instruction files of the coding agents installed on this computer. "
        "Open one to customise how that agent behaves. Changes apply when you save."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    filter_ = new QLineEdit(this);
    filter_->setPlaceholderText(QStringLiteral("Filter agents and files"));
    filter_->setClearButtonEnabled(true);
    layout->addWidget(filter_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setAccessibleName(QStringLiteral("Installed agents and their files"));
    tree_->setUniformRowHeights(true);
    layout->addWidget(tree_, 1);

    details_ = new QLabel(this);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(details_);

    auto *buttons = new QDialogButtonBox(this);
    open_ = buttons->addButton(QStringLiteral("Open"), QDialogButtonBox::AcceptRole);
    auto *reveal = buttons->addButton(QStringLiteral("Show folder"), QDialogButtonBox::ActionRole);
    auto *add = buttons->addButton(QStringLiteral("Add folder…"), QDialogButtonBox::ActionRole);
    remove_ = buttons->addButton(QStringLiteral("Remove folder"), QDialogButtonBox::ActionRole);
    auto *rescan = buttons->addButton(QStringLiteral("Rescan"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);

    connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { updateSelection(); });
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item && item->parent()) openCurrent();
    });
    connect(open_, &QPushButton::clicked, this, [this] { openCurrent(); });
    connect(reveal, &QPushButton::clicked, this, [this] {
        auto *item = tree_->currentItem();
        if (!item) return;
        QString folder = agentItem(item)->data(0, kRootRole).toString();
        if (item->parent()) {
            const QFileInfo info(item->data(0, kPathRole).toString());
            if (info.dir().exists()) folder = info.absolutePath();
        }
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
    });
    connect(add, &QPushButton::clicked, this, [this] { addFolder(); });
    connect(remove_, &QPushButton::clicked, this, [this] { removeFolder(); });
    connect(rescan, &QPushButton::clicked, this, [this] { refresh(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    refresh();
    filter_->setFocus();
}

QStringList AgentFilesDialog::customFolders() {
    return QSettings().value(kSettingsKey).toStringList();
}

void AgentFilesDialog::setCustomFolders(const QStringList &folders) {
    QSettings().setValue(kSettingsKey, folders);
}

void AgentFilesDialog::refresh() {
    const QString previous = tree_->currentItem() ? tree_->currentItem()->data(0, kPathRole).toString() : QString();
    tree_->clear();
    const auto agents = AgentConfigs::detect(QDir::homePath(), customFolders());
    QTreeWidgetItem *restore = nullptr;
    for (const auto &agent : agents) {
        auto *top = new QTreeWidgetItem(tree_);
        const auto existing = std::count_if(agent.files.begin(), agent.files.end(), [](const auto &file) { return file.exists; });
        const QString count = existing == 0 ? QStringLiteral("no Markdown files yet")
            : existing == 1 ? QStringLiteral("1 file") : QStringLiteral("%1 files").arg(existing);
        top->setText(0, QStringLiteral("%1  ·  %2").arg(agent.name, count));
        top->setData(0, kRootRole, agent.root);
        top->setData(0, kCustomRole, agent.custom);
        top->setToolTip(0, agent.root);
        QFont bold = top->font(0);
        bold.setBold(true);
        top->setFont(0, bold);
        for (const auto &file : agent.files) {
            auto *child = new QTreeWidgetItem(top);
            child->setText(0, file.exists ? file.relativePath : file.relativePath + QStringLiteral("  (create)"));
            child->setData(0, kPathRole, file.path);
            child->setData(0, kExistsRole, file.exists);
            child->setToolTip(0, file.path);
            if (!file.exists) {
                QFont italic = child->font(0);
                italic.setItalic(true);
                child->setFont(0, italic);
            }
            if (file.path == previous) restore = child;
        }
        top->setExpanded(true);
    }
    if (agents.isEmpty()) {
        auto *empty = new QTreeWidgetItem(tree_);
        empty->setText(0, QStringLiteral("No supported agents found. Use Add folder… to pick one."));
        empty->setFlags(Qt::NoItemFlags);
    }
    applyFilter();
    if (restore) tree_->setCurrentItem(restore);
    else if (auto *first = tree_->topLevelItem(0); first && first->childCount()) tree_->setCurrentItem(first->child(0));
    updateSelection();
}

void AgentFilesDialog::applyFilter() {
    const QString text = filter_->text().trimmed();
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto *top = tree_->topLevelItem(i);
        const bool agentMatches = text.isEmpty() || top->text(0).contains(text, Qt::CaseInsensitive);
        int visible = 0;
        for (int j = 0; j < top->childCount(); ++j) {
            auto *child = top->child(j);
            const bool show = agentMatches || child->text(0).contains(text, Qt::CaseInsensitive);
            child->setHidden(!show);
            if (show) ++visible;
        }
        top->setHidden(!agentMatches && visible == 0);
    }
}

QTreeWidgetItem *AgentFilesDialog::agentItem(QTreeWidgetItem *item) const {
    while (item && item->parent()) item = item->parent();
    return item;
}

void AgentFilesDialog::updateSelection() {
    auto *item = tree_->currentItem();
    const bool isFile = item && item->parent();
    open_->setEnabled(isFile);
    auto *top = agentItem(item);
    remove_->setEnabled(top && top->data(0, kCustomRole).toBool());
    if (!item) { details_->clear(); return; }
    if (isFile) {
        const bool exists = item->data(0, kExistsRole).toBool();
        details_->setText(displayPath(item->data(0, kPathRole).toString()) +
            (exists ? QString() : QStringLiteral("\nThis file does not exist yet. Saving it will create it.")));
    } else {
        details_->setText(displayPath(top->data(0, kRootRole).toString()));
    }
}

void AgentFilesDialog::openCurrent() {
    auto *item = tree_->currentItem();
    if (!item || !item->parent()) return;
    emit openRequested(item->data(0, kPathRole).toString(), item->data(0, kExistsRole).toBool());
    accept();
}

void AgentFilesDialog::addFolder() {
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Add an agent folder"), QDir::homePath(),
                                                             QFileDialog::ShowDirsOnly);
    if (folder.isEmpty()) return;
    const QString clean = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    QStringList folders = customFolders();
    if (!folders.contains(clean)) {
        folders.append(clean);
        setCustomFolders(folders);
    }
    refresh();
}

void AgentFilesDialog::removeFolder() {
    auto *top = agentItem(tree_->currentItem());
    if (!top || !top->data(0, kCustomRole).toBool()) return;
    QStringList folders = customFolders();
    folders.removeAll(top->data(0, kRootRole).toString());
    setCustomFolders(folders);
    refresh();
}
