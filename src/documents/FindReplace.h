#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace FindReplace {

struct Match {
    qsizetype start = 0; // UTF-8 byte offset
    qsizetype end = 0;
    QString replacement;
};

struct Result {
    QVector<Match> matches;
    QString error;
    bool truncated = false;
};

Result find(const QByteArray &source, const QString &pattern, const QString &replacement,
            bool regex, bool caseSensitive, bool wholeWord,
            qsizetype scopeStart, qsizetype scopeEnd, int limit = 10000);

}
