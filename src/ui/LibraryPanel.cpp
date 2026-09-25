#include "ui/LibraryPanel.h"
#include "Appearance.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QUuid>
#include <QVBoxLayout>

namespace {
constexpr int kindPrompt = 0;
constexpr int kindTemplate = 1;
constexpr int subtitleRole = Qt::UserRole + 2;

QIcon figmaIcon(bool dark, const QString &name) {
    return QIcon(QStringLiteral(":/figma/%1-%2.svg").arg(dark ? QStringLiteral("dark") : QStringLiteral("light"), name));
}

QPushButton *iconButton(const QString &accessibleName, QWidget *parent) {
    auto *button = new QPushButton(parent);
    button->setAccessibleName(accessibleName);
    button->setToolTip(accessibleName);
    button->setFixedSize(30, 30);
    button->setIconSize(QSize(16, 16));
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

class ItemDelegate final : public QStyledItemDelegate {
public:
    explicit ItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    void setDark(bool dark) { dark_ = dark; }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return QSize(245, 50); }
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const Appearance palette = Appearance::forMode(dark_);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        if (option.state & QStyle::State_Selected) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(palette.surface);
            painter->drawRoundedRect(option.rect.adjusted(0, 1, -2, -1), 8, 8);
        }
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        icon.paint(painter, QRect(option.rect.left() + 10, option.rect.top() + 9, 16, 16));
        const int x = option.rect.left() + 36;
        QFont title = option.font;
        title.setPixelSize(12);
        title.setWeight(option.state & QStyle::State_Selected ? QFont::Medium : QFont::Normal);
        painter->setFont(title);
        painter->setPen(palette.text);
        painter->drawText(QRect(x, option.rect.top() + 4, option.rect.width() - 43, 22),
                          Qt::AlignVCenter | Qt::TextSingleLine, index.data(Qt::DisplayRole).toString());
        QFont subtitle = option.font;
        subtitle.setPixelSize(10);
        painter->setFont(subtitle);
        painter->setPen(palette.secondaryText);
        painter->drawText(QRect(x, option.rect.top() + 25, option.rect.width() - 43, 18),
                          Qt::AlignVCenter | Qt::TextSingleLine, index.data(subtitleRole).toString());
        painter->restore();
    }
private:
    bool dark_ = false;
};
}

LibraryPanel::LibraryPanel(Store *store, QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint), store_(store) {
    setObjectName(QStringLiteral("libraryPanel"));
    setAccessibleName(QStringLiteral("Prompt library"));
    setAttribute(Qt::WA_TranslucentBackground);
    setMinimumSize(320, 380);
    resize(404, 464);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *surface = new QFrame(this);
    surface->setObjectName(QStringLiteral("librarySurface"));
    outer->addWidget(surface);
    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(15, 10, 14, 8);
    layout->setSpacing(9);

    dragHandle_ = new QWidget(surface);
    dragHandle_->setObjectName(QStringLiteral("libraryDragHandle"));
    dragHandle_->setAccessibleName(QStringLiteral("Drag library"));
    dragHandle_->setCursor(Qt::OpenHandCursor);
    dragHandle_->installEventFilter(this);
    auto *header = new QHBoxLayout(dragHandle_);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    title_ = new QLabel(QStringLiteral("Your library"), dragHandle_);
    title_->setObjectName(QStringLiteral("libraryTitle"));
    title_->setCursor(Qt::OpenHandCursor);
    title_->installEventFilter(this);
    header->addWidget(title_);
    header->addStretch();
    copyButton_ = iconButton(QStringLiteral("Copy entire prompt"), dragHandle_);
    newButton_ = iconButton(QStringLiteral("New prompt"), dragHandle_);
    closeButton_ = iconButton(QStringLiteral("Close library"), dragHandle_);
    header->addWidget(copyButton_);
    header->addWidget(newButton_);
    header->addWidget(closeButton_);
    layout->addWidget(dragHandle_);
    connect(copyButton_, &QPushButton::clicked, this, &LibraryPanel::copyPromptRequested);
    connect(newButton_, &QPushButton::clicked, this, &LibraryPanel::newPromptRequested);
    connect(closeButton_, &QPushButton::clicked, this, &LibraryPanel::closeRequested);

    search_ = new QLineEdit(surface);
    search_->setObjectName(QStringLiteral("librarySearch"));
    search_->setAccessibleName(QStringLiteral("Search prompts, templates and folders"));
    search_->setPlaceholderText(QStringLiteral("Search prompts, templates and folders"));
    search_->setClearButtonEnabled(true);
    search_->setFixedHeight(34);
    layout->addWidget(search_);
    connect(search_, &QLineEdit::textChanged, this, [this] { refreshItems(); });
    connect(search_, &QLineEdit::returnPressed, this, [this] {
        if (items_->count()) openItem(items_->item(0));
    });

    auto *body = new QHBoxLayout;
    body->setSpacing(10);
    auto *folderColumn = new QWidget(surface);
    folderColumn->setFixedWidth(118);
    auto *folderLayout = new QVBoxLayout(folderColumn);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->setSpacing(4);
    auto *folderHeader = new QHBoxLayout;
    auto *folderCaption = new QLabel(QStringLiteral("FOLDERS"), folderColumn);
    folderCaption->setObjectName(QStringLiteral("folderCaption"));
    folderHeader->addWidget(folderCaption);
    folderHeader->addStretch();
    folderButton_ = iconButton(QStringLiteral("New folder"), folderColumn);
    folderButton_->setFixedSize(25, 28);
    folderHeader->addWidget(folderButton_);
    folderLayout->addLayout(folderHeader);
    connect(folderButton_, &QPushButton::clicked, this, &LibraryPanel::newFolder);
    folders_ = new QListWidget(folderColumn);
    folders_->setObjectName(QStringLiteral("libraryFolders"));
    folders_->setAccessibleName(QStringLiteral("Folders"));
    folders_->setFrameShape(QFrame::NoFrame);
    folders_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    folders_->setContextMenuPolicy(Qt::CustomContextMenu);
    folderLayout->addWidget(folders_, 1);
    connect(folders_, &QListWidget::currentItemChanged, this, [this] { refreshItems(); });
    connect(folders_, &QListWidget::customContextMenuRequested, this, &LibraryPanel::showFolderMenu);
    auto *device = new QLabel(QStringLiteral("On this device"), folderColumn);
    device->setObjectName(QStringLiteral("libraryDevice"));
    folderLayout->addWidget(device);
    body->addWidget(folderColumn);
    auto *divider = new QFrame(surface);
    divider->setObjectName(QStringLiteral("libraryDivider"));
    divider->setFrameShape(QFrame::VLine);
    body->addWidget(divider);

    auto *itemColumn = new QWidget(surface);
    auto *itemLayout = new QVBoxLayout(itemColumn);
    itemLayout->setContentsMargins(0, 0, 0, 0);
    itemLayout->setSpacing(7);
    auto *filters = new QHBoxLayout;
    filters->setSpacing(2);
    auto *group = new QButtonGroup(this);
    group->setExclusive(true);
    for (auto *button : {allFilter_ = new QPushButton(QStringLiteral("All"), itemColumn),
                         promptsFilter_ = new QPushButton(QStringLiteral("Prompts"), itemColumn),
                         templatesFilter_ = new QPushButton(QStringLiteral("Templates"), itemColumn)}) {
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFixedHeight(27);
        group->addButton(button);
        filters->addWidget(button, 1);
        connect(button, &QPushButton::toggled, this, [this](bool checked) { if (checked) refreshItems(); });
    }
    allFilter_->setObjectName(QStringLiteral("libraryFilterAll"));
    promptsFilter_->setObjectName(QStringLiteral("libraryFilterPrompts"));
    templatesFilter_->setObjectName(QStringLiteral("libraryFilterTemplates"));
    allFilter_->setChecked(true);
    itemLayout->addLayout(filters);
    items_ = new QListWidget(itemColumn);
    items_->setObjectName(QStringLiteral("libraryItems"));
    items_->setAccessibleName(QStringLiteral("Saved prompts and templates"));
    items_->setFrameShape(QFrame::NoFrame);
    items_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    items_->setItemDelegate(new ItemDelegate(items_));
    items_->setContextMenuPolicy(Qt::CustomContextMenu);
    itemLayout->addWidget(items_, 1);
    connect(items_, &QListWidget::itemClicked, this, &LibraryPanel::openItem);
    connect(items_, &QListWidget::itemActivated, this, &LibraryPanel::openItem);
    connect(items_, &QListWidget::customContextMenuRequested, this, &LibraryPanel::showItemMenu);
    body->addWidget(itemColumn, 1);
    layout->addLayout(body, 1);

    auto *rule = new QFrame(surface);
    rule->setObjectName(QStringLiteral("libraryRule"));
    rule->setFrameShape(QFrame::HLine);
    layout->addWidget(rule);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(2);
    saveButton_ = new QPushButton(QStringLiteral("Save"), surface);
    templateButton_ = new QPushButton(QStringLiteral("Save as template"), surface);
    previewButton_ = new QPushButton(QStringLiteral("Preview"), surface);
    settingsButton_ = iconButton(QStringLiteral("Settings"), surface);
    saveButton_->setFixedWidth(62);
    templateButton_->setFixedWidth(138);
    previewButton_->setFixedWidth(82);
    for (auto *button : {saveButton_, templateButton_, previewButton_}) {
        button->setCursor(Qt::PointingHandCursor);
        button->setIconSize(QSize(16, 16));
        footer->addWidget(button);
    }
    footer->addStretch();
    footer->addWidget(settingsButton_);
    layout->addLayout(footer);
    connect(saveButton_, &QPushButton::clicked, this, &LibraryPanel::saveRequested);
    connect(templateButton_, &QPushButton::clicked, this, &LibraryPanel::saveTemplateRequested);
    connect(previewButton_, &QPushButton::clicked, this, &LibraryPanel::previewRequested);
    connect(settingsButton_, &QPushButton::clicked, this, [this] {
        emit settingsRequested(settingsButton_->mapToGlobal(QPoint(0, settingsButton_->height())));
    });
    setAppearance(false);
}

void LibraryPanel::setAppearance(bool dark) {
    dark_ = dark;
    const Appearance palette = Appearance::forMode(dark);
    setStyleSheet(QStringLiteral(R"(
      #libraryPanel { background: transparent; }
      #librarySurface { background: %1; border: 1px solid %2; border-radius: 16px; }
      #libraryTitle { color: %3; font-size: 16px; font-weight: 600; }
      #folderCaption, #libraryDevice { color: %4; font-size: 10px; }
      #librarySearch { background: %5; color: %3; border: 0; border-radius: 8px; padding: 4px 8px; font-size: 11px; }
      #librarySearch:focus { border: 1px solid %6; }
      #libraryFolders, #libraryItems { background: transparent; border: 0; outline: 0; color: %3; }
      #libraryFolders::item { height: 30px; border-radius: 7px; padding-left: 5px; font-size: 11px; }
      #libraryFolders::item:selected { background: %7; color: %6; }
      #libraryFolders::item:hover { background: %5; }
      #libraryItems::item { border: 0; }
      #libraryDivider, #libraryRule { background: %2; border: 0; }
      QPushButton { background: transparent; color: %3; border: 0; border-radius: 7px; padding: 3px 5px; font-size: 11px; }
      QPushButton:hover, QPushButton:focus { background: %5; }
      QPushButton:checked { background: %7; color: %6; }
    )").arg(palette.panel.name(), palette.border.name(), palette.text.name(),
            palette.secondaryText.name(), palette.surface.name(), palette.accent.name(), palette.selection.name()));
    copyButton_->setIcon(figmaIcon(dark, QStringLiteral("copy")));
    newButton_->setIcon(figmaIcon(dark, QStringLiteral("plus")));
    closeButton_->setIcon(figmaIcon(dark, QStringLiteral("close")));
    folderButton_->setIcon(figmaIcon(dark, QStringLiteral("plus")));
    saveButton_->setIcon(figmaIcon(dark, QStringLiteral("save")));
    templateButton_->setIcon(figmaIcon(dark, QStringLiteral("template")));
    previewButton_->setIcon(figmaIcon(dark, QStringLiteral("preview")));
    settingsButton_->setIcon(figmaIcon(dark, QStringLiteral("settings")));
    if (isVisible()) refresh();
}

QString LibraryPanel::selectedFolderId() const {
    return folders_->currentItem() ? folders_->currentItem()->data(Qt::UserRole).toString() : QString();
}

void LibraryPanel::setStarredOnly(bool on) {
    starredOnly_ = on;
    title_->setText(on ? QStringLiteral("Starred prompts") : QStringLiteral("Your library"));
    if (on) promptsFilter_->setChecked(true);
    if (isVisible()) refreshItems();
}

void LibraryPanel::refresh() {
    refreshFolders();
    refreshItems();
}

void LibraryPanel::refreshFolders() {
    const QString selected = selectedFolderId();
    folders_->blockSignals(true);
    folders_->clear();
    auto *all = new QListWidgetItem(figmaIcon(dark_, QStringLiteral("all")), QStringLiteral("All items"), folders_);
    all->setData(Qt::UserRole, QString());
    QListWidgetItem *toSelect = all;
    for (const FolderRecord &folder : store_->folders()) {
        auto *item = new QListWidgetItem(figmaIcon(dark_, QStringLiteral("folder")), folder.title, folders_);
        item->setData(Qt::UserRole, folder.id);
        if (folder.id == selected) toSelect = item;
    }
    folders_->setCurrentItem(toSelect);
    folders_->blockSignals(false);
}

void LibraryPanel::refreshItems() {
    if (!items_ || !folders_) return;
    const QString query = search_->text().trimmed();
    const QString folder = selectedFolderId();
    const bool showPrompts = !templatesFilter_->isChecked();
    const bool showTemplates = !promptsFilter_->isChecked();
    const auto savedFolders = store_->folders();
    QStringList matchingFolderIds;
    for (const FolderRecord &entry : savedFolders)
        if (!query.isEmpty() && entry.title.contains(query, Qt::CaseInsensitive)) matchingFolderIds.append(entry.id);
    items_->clear();
    QVector<DraftRecord> drafts = query.isEmpty() ? store_->listDrafts() : store_->listDrafts(false, query);
    if (!matchingFolderIds.isEmpty()) {
        QSet<QString> seen;
        for (const auto &entry : drafts) seen.insert(entry.id);
        for (const auto &entry : store_->listDrafts())
            if (matchingFolderIds.contains(entry.folderId) && !seen.contains(entry.id)) drafts.append(entry);
    }
    if (showPrompts) for (const DraftRecord &draft : drafts) {
        if (starredOnly_ && !draft.starred) continue;
        if (!folder.isEmpty() && draft.folderId != folder) continue;
        const QString title = draft.title.isEmpty() ? QStringLiteral("New prompt") : draft.title;
        auto *item = new QListWidgetItem(figmaIcon(dark_, QStringLiteral("document")), title, items_);
        item->setData(Qt::UserRole, draft.id);
        item->setData(Qt::UserRole + 1, kindPrompt);
        item->setData(subtitleRole, draft.path.isEmpty() ? QStringLiteral("Prompt · local draft")
            : QStringLiteral("Prompt · saved file"));
        item->setToolTip(draft.path.isEmpty() ? title : draft.path);
    }
    QVector<TemplateRecord> templates = query.isEmpty() ? store_->templates() : store_->templates(query);
    if (!matchingFolderIds.isEmpty()) {
        QSet<QString> seen;
        for (const auto &entry : templates) seen.insert(entry.id);
        for (const auto &entry : store_->templates())
            if (matchingFolderIds.contains(entry.folderId) && !seen.contains(entry.id)) templates.append(entry);
    }
    if (showTemplates && !starredOnly_) for (const TemplateRecord &entry : templates) {
        if (!folder.isEmpty() && entry.folderId != folder) continue;
        auto *item = new QListWidgetItem(figmaIcon(dark_, QStringLiteral("template")), entry.title, items_);
        item->setData(Qt::UserRole, entry.id);
        item->setData(Qt::UserRole + 1, kindTemplate);
        item->setData(subtitleRole, QStringLiteral("Template · reusable"));
        item->setToolTip(QStringLiteral("Open as a new independent draft"));
    }
    if (auto *delegate = dynamic_cast<ItemDelegate *>(items_->itemDelegate())) delegate->setDark(dark_);
    items_->viewport()->update();
}

void LibraryPanel::openItem(QListWidgetItem *item) {
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (item->data(Qt::UserRole + 1).toInt() == kindTemplate) emit openTemplateRequested(id);
    else emit openPromptRequested(id);
}

void LibraryPanel::newFolder() {
    bool okay = false;
    const QString title = QInputDialog::getText(this, QStringLiteral("New folder"),
        QStringLiteral("Folder name"), QLineEdit::Normal, {}, &okay).trimmed();
    if (!okay || title.isEmpty()) return;
    const FolderRecord folder{QUuid::createUuid().toString(QUuid::WithoutBraces), title};
    QString error;
    if (!store_->saveFolder(folder, &error)) {
        QMessageBox::warning(this, QStringLiteral("Could not create folder"), error);
        return;
    }
    refresh();
    for (int index = 0; index < folders_->count(); ++index)
        if (folders_->item(index)->data(Qt::UserRole).toString() == folder.id) folders_->setCurrentRow(index);
}

void LibraryPanel::showFolderMenu(const QPoint &point) {
    auto *item = folders_->itemAt(point);
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    if (id.isEmpty()) return;
    const QString oldTitle = item->text();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Rename folder…"), this, [this, id, oldTitle] {
        bool okay = false;
        const QString title = QInputDialog::getText(this, QStringLiteral("Rename folder"),
            QStringLiteral("Folder name"), QLineEdit::Normal, oldTitle, &okay).trimmed();
        if (!okay || title.isEmpty()) return;
        QString error;
        if (!store_->saveFolder({id, title}, &error)) QMessageBox::warning(this, QStringLiteral("Could not rename folder"), error);
        else refresh();
    });
    menu.addAction(QStringLiteral("Remove folder…"), this, [this, id] {
        if (QMessageBox::question(this, QStringLiteral("Remove folder?"),
            QStringLiteral("Prompts and templates in this folder will remain in All items.")) != QMessageBox::Yes) return;
        QString error;
        if (!store_->removeFolder(id, &error)) QMessageBox::warning(this, QStringLiteral("Could not remove folder"), error);
        else { emit folderRemoved(id); refresh(); }
    });
    menu.exec(folders_->viewport()->mapToGlobal(point));
}

void LibraryPanel::showItemMenu(const QPoint &point) {
    auto *item = items_->itemAt(point);
    if (!item) return;
    const QString id = item->data(Qt::UserRole).toString();
    const bool isTemplate = item->data(Qt::UserRole + 1).toInt() == kindTemplate;
    QMenu menu(this);
    menu.addAction(isTemplate ? QStringLiteral("Open as new draft") : QStringLiteral("Open"),
                   this, [this, item] { openItem(item); });
    if (!isTemplate) {
        menu.addAction(QStringLiteral("Star / unstar"), this, [this, id] { emit starPromptRequested(id); });
        menu.addAction(QStringLiteral("Duplicate"), this, [this, id] { emit duplicatePromptRequested(id); });
    } else {
        menu.addAction(QStringLiteral("Rename template…"), this, [this, id] {
            const TemplateRecord item = store_->loadTemplate(id);
            bool okay = false;
            const QString title = QInputDialog::getText(this, QStringLiteral("Rename template"),
                QStringLiteral("Template name"), QLineEdit::Normal, item.title, &okay).trimmed();
            if (!okay || title.isEmpty()) return;
            TemplateRecord updated = item;
            updated.title = title;
            QString error;
            if (!store_->saveTemplate(updated, &error)) QMessageBox::warning(this, QStringLiteral("Could not rename template"), error);
            else refreshItems();
        });
    }
    auto *move = menu.addMenu(QStringLiteral("Move to folder"));
    auto addDestination = [this, move, id, isTemplate](const QString &title, const QString &folderId) {
        move->addAction(title, this, [this, id, isTemplate, folderId] {
            if (!isTemplate) { emit movePromptRequested(id, folderId); return; }
            QString error;
            if (!store_->setTemplateFolder(id, folderId, &error)) QMessageBox::warning(this, QStringLiteral("Could not move template"), error);
            else refreshItems();
        });
    };
    addDestination(QStringLiteral("Unfiled"), {});
    for (const FolderRecord &folder : store_->folders()) addDestination(folder.title, folder.id);
    menu.addSeparator();
    if (isTemplate) menu.addAction(QStringLiteral("Remove template…"), this, [this, id] {
        if (QMessageBox::question(this, QStringLiteral("Remove template?"),
            QStringLiteral("Remove this reusable copy? Existing drafts remain intact.")) != QMessageBox::Yes) return;
        QString error;
        if (!store_->removeTemplate(id, &error)) QMessageBox::warning(this, QStringLiteral("Could not remove template"), error);
        else refreshItems();
    });
    else menu.addAction(QStringLiteral("Remove from library…"), this, [this, id] {
        emit removePromptRequested(id);
    });
    menu.exec(items_->viewport()->mapToGlobal(point));
}

bool LibraryPanel::eventFilter(QObject *watched, QEvent *event) {
    if (watched == dragHandle_ || watched == title_) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                dragging_ = true;
                dragStartGlobal_ = mouse->globalPosition().toPoint();
                dragStartPosition_ = pos();
                dragHandle_->setCursor(Qt::ClosedHandCursor);
                title_->setCursor(Qt::ClosedHandCursor);
                emit dragStarted();
                return true;
            }
        }
        if (dragging_ && event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            move(dragStartPosition_ + mouse->globalPosition().toPoint() - dragStartGlobal_);
            return true;
        }
        if (dragging_ && event->type() == QEvent::MouseButtonRelease) {
            dragging_ = false;
            dragHandle_->setCursor(Qt::OpenHandCursor);
            title_->setCursor(Qt::OpenHandCursor);
            emit dragFinished();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void LibraryPanel::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) { emit closeRequested(); return; }
    QWidget::keyPressEvent(event);
}
