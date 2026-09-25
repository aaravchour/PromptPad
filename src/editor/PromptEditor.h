#pragma once

#include "ScintillaEditBase.h"

#include <QByteArray>
#include <QColor>

class PromptEditor final : public ScintillaEditBase {
    Q_OBJECT
public:
    explicit PromptEditor(QWidget *parent = nullptr);
    static QString defaultFontFamily();

    QByteArray textUtf8() const;
    void setTextUtf8(const QByteArray &text);
    QByteArray selectedUtf8() const;
    void replaceSelection(const QByteArray &text);
    void setAppearance(bool dark);
    void setEditorFont(const QString &family, int logicalPixels, int extraSpacing);
    void setWrap(bool on);
    void setLineNumbers(bool on);
    void setComfortableWidth(bool on);
    qsizetype lengthBytes() const;

signals:
    void contextualFormattingRequested(const QPoint &globalPosition);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateWritingInsets();
    bool comfortableWidth_ = true;
    bool dark_ = false;
};
