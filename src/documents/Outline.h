#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace Outline {

struct Heading {
    int level = 0;
    qsizetype start = 0;
    qsizetype endOfLine = 0;
    qsizetype sectionEnd = 0;
    int line = 0;
    int parent = -1;
    QString title;
};

struct Edit {
    bool valid = false;
    qsizetype start = 0;
    qsizetype end = 0;
    QByteArray replacement;
    qsizetype caret = 0;
};

QVector<Heading> parse(const QByteArray &source);
int sectionAt(const QVector<Heading> &headings, qsizetype bytePosition);
Edit move(const QByteArray &source, const QVector<Heading> &headings, int index, bool down);

}
