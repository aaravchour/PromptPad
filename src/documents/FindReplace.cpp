#include "documents/FindReplace.h"

#include <QRegularExpression>
#include <QStringView>

namespace FindReplace {

Result find(const QByteArray &source, const QString &pattern, const QString &replacement,
            bool regex, bool caseSensitive, bool wholeWord,
            qsizetype scopeStart, qsizetype scopeEnd, int limit) {
    Result output;
    if (pattern.isEmpty()) return output;
    scopeStart = qBound(qsizetype(0), scopeStart, source.size());
    scopeEnd = qBound(scopeStart, scopeEnd, source.size());
    const QString text = QString::fromUtf8(source.constData() + scopeStart, scopeEnd - scopeStart);
    QString expression = regex ? pattern : QRegularExpression::escape(pattern);
    if (wholeWord) expression = QStringLiteral("(?<![\\p{L}\\p{N}_])(?:") + expression + QStringLiteral(")(?![\\p{L}\\p{N}_])");
    QRegularExpression::PatternOptions options = QRegularExpression::UseUnicodePropertiesOption;
    if (!caseSensitive) options |= QRegularExpression::CaseInsensitiveOption;
    const QRegularExpression compiled(expression, options);
    if (!compiled.isValid()) {
        output.error = compiled.errorString();
        return output;
    }
    auto iterator = compiled.globalMatch(text);
    qsizetype lastUtf16 = 0;
    qsizetype lastByte = scopeStart;
    while (iterator.hasNext()) {
        const auto match = iterator.next();
        if (!match.hasMatch()) break;
        if (output.matches.size() >= limit) { output.truncated = true; break; }
        const qsizetype start = match.capturedStart();
        const qsizetype end = match.capturedEnd();
        lastByte += QStringView(text).mid(lastUtf16, start - lastUtf16).toUtf8().size();
        const qsizetype startByte = lastByte;
        lastByte += QStringView(text).mid(start, end - start).toUtf8().size();
        output.matches.append({startByte, lastByte, replacement});
        lastUtf16 = end;
    }
    return output;
}

}
