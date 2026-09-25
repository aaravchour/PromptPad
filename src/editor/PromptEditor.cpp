#include "editor/PromptEditor.h"
#include "Appearance.h"

#include "Scintilla.h"
#include "SciLexer.h"
#include "ILexer.h"
#include "LexerModule.h"

#include <QFontDatabase>
#include <QContextMenuEvent>
#include <QResizeEvent>

extern const Lexilla::LexerModule lmMarkdown;

namespace {
void colour(ScintillaEditBase *editor, int style, const QColor &foreground, const QColor &background) {
    editor->send(SCI_STYLESETFORE, style, foreground.red() | foreground.green() << 8 | foreground.blue() << 16);
    editor->send(SCI_STYLESETBACK, style, background.red() | background.green() << 8 | background.blue() << 16);
}
QByteArray headingFont() {
    static const QByteArray family = [] {
        const QStringList available = QFontDatabase::families();
        for (const QString candidate : {QStringLiteral("IBM Plex Mono"), QStringLiteral("Menlo"),
                                        QStringLiteral("Andale Mono"), QStringLiteral("DejaVu Sans Mono")})
            if (available.contains(candidate)) return candidate.toUtf8();
        for (const QString &candidate : available)
            if (QFontDatabase::isFixedPitch(candidate)) return candidate.toUtf8();
        return QFontDatabase::systemFont(QFontDatabase::FixedFont).family().toUtf8();
    }();
    return family;
}
}

PromptEditor::PromptEditor(QWidget *parent) : ScintillaEditBase(parent) {
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(lmMarkdown.Create()));
    send(SCI_SETMULTIPLESELECTION, 1);
    send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    send(SCI_SETMULTIPASTE, SC_MULTIPASTE_EACH);
    send(SCI_SETINDENTATIONGUIDES, SC_IV_NONE);
    send(SCI_SETWRAPMODE, SC_WRAP_WORD);
    send(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_NONE);
    send(SCI_SETCARETPERIOD, 0);
    send(SCI_SETMARGINWIDTHN, 0, 0);
    send(SCI_SETMARGINWIDTHN, 1, 0);
    send(SCI_SETMARGINWIDTHN, 2, 0);
    setEditorFont(defaultFontFamily(), 14, 3);
    setAppearance(false);
    updateWritingInsets();
    setAccessibleName(QStringLiteral("Prompt editor"));
}

QString PromptEditor::defaultFontFamily() {
    static const QString family = [] {
        const QStringList available = QFontDatabase::families();
#ifdef Q_OS_MACOS
        if (available.contains(QStringLiteral("Helvetica Neue"))) return QStringLiteral("Helvetica Neue");
#endif
        const QString preferred = QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
        if (available.contains(preferred, Qt::CaseInsensitive)) return preferred;
        for (const QString &candidate : available)
            if (!QFontDatabase::isFixedPitch(candidate)) return candidate;
        return preferred;
    }();
    return family;
}

QByteArray PromptEditor::textUtf8() const {
    const auto length = static_cast<qsizetype>(send(SCI_GETLENGTH));
    QByteArray out(length + 1, '\0');
    send(SCI_GETTEXT, length + 1, reinterpret_cast<sptr_t>(out.data()));
    out.truncate(length);
    return out;
}

void PromptEditor::setTextUtf8(const QByteArray &text) {
    send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>(text.constData()));
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
}

QByteArray PromptEditor::selectedUtf8() const {
    const auto length = static_cast<qsizetype>(send(SCI_GETSELTEXT, 0, 0));
    if (length <= 0) return {};
    QByteArray out(length + 1, '\0');
    send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(out.data()));
    out.truncate(length);
    return out;
}

void PromptEditor::replaceSelection(const QByteArray &text) {
    send(SCI_REPLACESEL, 0, reinterpret_cast<sptr_t>(text.constData()));
}

void PromptEditor::setAppearance(bool dark) {
    dark_ = dark;
    const Appearance palette = Appearance::forMode(dark);
    colour(this, STYLE_DEFAULT, palette.text, palette.canvas);
    send(SCI_STYLECLEARALL);
    for (int style : {SCE_MARKDOWN_HEADER1, SCE_MARKDOWN_HEADER2, SCE_MARKDOWN_HEADER3,
                      SCE_MARKDOWN_HEADER4, SCE_MARKDOWN_HEADER5, SCE_MARKDOWN_HEADER6}) {
        colour(this, style, palette.accent, palette.canvas);
        const QByteArray mono = headingFont();
        send(SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(mono.constData()));
        send(SCI_STYLESETSIZEFRACTIONAL, style, 13 * 7200 / qMax(1, logicalDpiY()));
    }
    for (int style : {SCE_MARKDOWN_CODE, SCE_MARKDOWN_CODE2, SCE_MARKDOWN_CODEBK,
                      SCE_MARKDOWN_BLOCKQUOTE}) {
        colour(this, style, palette.secondaryText, palette.canvas);
        if (style != SCE_MARKDOWN_BLOCKQUOTE) {
            const QByteArray mono = headingFont();
            send(SCI_STYLESETFONT, style, reinterpret_cast<sptr_t>(mono.constData()));
            send(SCI_STYLESETSIZEFRACTIONAL, style, 13 * 7200 / qMax(1, logicalDpiY()));
        }
    }
    colour(this, SCE_MARKDOWN_LINK, palette.accent, palette.canvas);
    auto scintillaColour = [](const QColor &value) {
        return value.red() | value.green() << 8 | value.blue() << 16;
    };
    send(SCI_SETCARETFORE, scintillaColour(palette.accent));
    send(SCI_SETSELFORE, 1, scintillaColour(palette.text));
    send(SCI_SETSELBACK, 1, scintillaColour(palette.selection));
    for (int margin = 0; margin < 3; ++margin)
        send(SCI_SETMARGINBACKN, margin, scintillaColour(palette.canvas));
    send(SCI_SETCARETLINEVISIBLE, 0);
    viewport()->update();
}

void PromptEditor::setEditorFont(const QString &family, int logicalPixels, int extraSpacing) {
    const QByteArray name = family.toUtf8();
    send(SCI_STYLESETFONT, STYLE_DEFAULT, reinterpret_cast<sptr_t>(name.constData()));
    const int pointHundredths = logicalPixels * 7200 / qMax(1, logicalDpiY());
    send(SCI_STYLESETSIZEFRACTIONAL, STYLE_DEFAULT, pointHundredths);
    send(SCI_SETEXTRAASCENT, extraSpacing);
    send(SCI_SETEXTRADESCENT, extraSpacing);
    send(SCI_STYLECLEARALL);
    setAppearance(dark_);
}

void PromptEditor::setWrap(bool on) { send(SCI_SETWRAPMODE, on ? SC_WRAP_WORD : SC_WRAP_NONE); }
void PromptEditor::setLineNumbers(bool on) {
    send(SCI_SETMARGINWIDTHN, 0, on ? 48 : 0);
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    updateWritingInsets();
}
void PromptEditor::setComfortableWidth(bool on) {
    if (comfortableWidth_ == on) return;
    comfortableWidth_ = on;
    updateWritingInsets();
}
void PromptEditor::resizeEvent(QResizeEvent *event) {
    ScintillaEditBase::resizeEvent(event);
    updateWritingInsets();
}
void PromptEditor::contextMenuEvent(QContextMenuEvent *event) {
    const auto position = send(SCI_POSITIONFROMPOINTCLOSE, event->pos().x(), event->pos().y());
    const auto start = send(SCI_GETSELECTIONSTART);
    const auto end = send(SCI_GETSELECTIONEND);
    if (position >= 0 && (position < start || position > end)) send(SCI_SETEMPTYSELECTION, position);
    emit contextualFormattingRequested(event->globalPos());
    event->accept();
}
void PromptEditor::updateWritingInsets() {
    constexpr int minimumInset = 24;
    constexpr int readingWidth = 880;
    const int gutter = static_cast<int>(send(SCI_GETMARGINWIDTHN, 0));
    const int available = qMax(0, viewport()->width() - gutter);
    const int inset = comfortableWidth_ ? qMax(minimumInset, (available - readingWidth) / 2) : minimumInset;
    if (send(SCI_GETMARGINLEFT) != inset) send(SCI_SETMARGINLEFT, 0, inset);
    if (send(SCI_GETMARGINRIGHT) != inset) send(SCI_SETMARGINRIGHT, 0, inset);
}
qsizetype PromptEditor::lengthBytes() const { return static_cast<qsizetype>(send(SCI_GETLENGTH)); }
