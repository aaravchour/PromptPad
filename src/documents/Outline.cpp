#include "documents/Outline.h"

#include <QByteArrayView>

namespace Outline {
namespace {
QByteArrayView trim(QByteArrayView text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text = text.sliced(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t')) text = text.first(text.size() - 1);
    return text;
}

int indentation(QByteArrayView text) {
    int spaces = 0;
    while (spaces < text.size() && text[spaces] == ' ') ++spaces;
    return spaces;
}

int fenceRun(QByteArrayView text, char marker) {
    int count = 0;
    while (count < text.size() && text[count] == marker) ++count;
    return count;
}

QString titleFrom(QByteArrayView raw) {
    auto text = trim(raw);
    // Optional ATX closing markers require a separating space.
    qsizetype end = text.size();
    while (end > 0 && text[end - 1] == '#') --end;
    if (end < text.size() && end > 0 && text[end - 1] == ' ') text = trim(text.first(end));
    return QString::fromUtf8(text.data(), text.size());
}
}

QVector<Heading> parse(const QByteArray &source) {
    QVector<Heading> result;
    const QByteArrayView bytes(source);
    qsizetype start = 0;
    int lineNumber = 0;
    char fenceMarker = 0;
    int fenceLength = 0;
    qsizetype previousStart = -1;
    int previousLine = -1;
    QByteArrayView previousText;
    while (start < bytes.size()) {
        qsizetype end = start;
        while (end < bytes.size() && bytes[end] != '\n' && bytes[end] != '\r') ++end;
        qsizetype next = end;
        if (next < bytes.size() && bytes[next] == '\r') ++next;
        if (next < bytes.size() && bytes[next] == '\n') ++next;
        const QByteArrayView raw = bytes.sliced(start, end - start);
        const int spaces = indentation(raw);
        const QByteArrayView body = spaces <= 3 ? raw.sliced(spaces) : QByteArrayView{};
        bool fenceLine = false;
        if (!body.empty() && (body.front() == '`' || body.front() == '~')) {
            const int run = fenceRun(body, body.front());
            if (!fenceMarker && run >= 3) {
                fenceMarker = body.front(); fenceLength = run; fenceLine = true;
            } else if (fenceMarker == body.front() && run >= fenceLength && trim(body.sliced(run)).empty()) {
                fenceMarker = 0; fenceLength = 0; fenceLine = true;
            }
        }
        if (fenceLine || fenceMarker) {
            previousStart = -1;
        } else {
            int hashes = fenceRun(body, '#');
            if (hashes >= 1 && hashes <= 6 && (hashes == body.size() || body[hashes] == ' ' || body[hashes] == '\t')) {
                result.append({hashes, start, next, 0, lineNumber, -1, titleFrom(body.sliced(hashes))});
                previousStart = -1;
            } else {
                const QByteArrayView stripped = trim(body);
                int underline = 0;
                if (!stripped.empty() && (stripped.front() == '=' || stripped.front() == '-')) {
                    underline = fenceRun(stripped, stripped.front());
                    if (underline != stripped.size()) underline = 0;
                }
                if (previousStart >= 0 && underline > 0) {
                    result.append({stripped.front() == '=' ? 1 : 2, previousStart, next, 0,
                                   previousLine, -1, QString::fromUtf8(previousText.data(), previousText.size())});
                    previousStart = -1;
                } else if (!stripped.empty() && spaces <= 3) {
                    previousStart = start; previousLine = lineNumber; previousText = stripped;
                } else {
                    previousStart = -1;
                }
            }
        }
        start = next;
        ++lineNumber;
    }
    QVector<int> stack;
    for (int i = 0; i < result.size(); ++i) {
        while (!stack.isEmpty() && result[stack.last()].level >= result[i].level) {
            result[stack.takeLast()].sectionEnd = result[i].start;
        }
        result[i].parent = stack.isEmpty() ? -1 : stack.last();
        stack.append(i);
    }
    while (!stack.isEmpty()) result[stack.takeLast()].sectionEnd = bytes.size();
    return result;
}

int sectionAt(const QVector<Heading> &headings, qsizetype bytePosition) {
    int selected = -1;
    for (int i = 0; i < headings.size(); ++i) {
        if (headings[i].start > bytePosition) break;
        if (bytePosition < headings[i].sectionEnd) selected = i;
    }
    return selected;
}

Edit move(const QByteArray &source, const QVector<Heading> &headings, int index, bool down) {
    Edit edit;
    if (index < 0 || index >= headings.size()) return edit;
    const Heading &current = headings[index];
    int sibling = -1;
    if (down) {
        for (int i = index + 1; i < headings.size(); ++i) {
            if (headings[i].level < current.level) break;
            if (headings[i].level == current.level) { sibling = i; break; }
        }
    } else {
        for (int i = index - 1; i >= 0; --i) {
            if (headings[i].level < current.level) break;
            if (headings[i].level == current.level) { sibling = i; break; }
        }
    }
    if (sibling < 0 || headings[sibling].parent != current.parent) return edit;
    const Heading &other = headings[sibling];
    const Heading &first = down ? current : other;
    const Heading &second = down ? other : current;
    if (first.sectionEnd != second.start || second.sectionEnd > source.size()) return edit;
    const QByteArray firstBytes = source.mid(first.start, first.sectionEnd - first.start);
    const QByteArray secondBytes = source.mid(second.start, second.sectionEnd - second.start);
    edit.valid = true;
    edit.start = first.start;
    edit.end = second.sectionEnd;
    edit.replacement = secondBytes + firstBytes;
    edit.caret = down ? first.start + secondBytes.size() : first.start;
    return edit;
}

}
