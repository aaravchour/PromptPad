#pragma once

#include <QColor>

// Shared colours for Qt chrome and the Scintilla surface.
struct Appearance {
    QColor canvas;
    QColor surface;
    QColor text;
    QColor secondaryText;
    QColor accent;
    QColor border;
    QColor selection;
    QColor panel;

    static Appearance forMode(bool dark) {
        return dark
            ? Appearance{QColor("#25242B"), QColor("#38343E"), QColor("#EEEAF3"),
                         QColor("#B5AEBE"), QColor("#C1ABFA"), QColor("#49434F"), QColor("#4A3B65"), QColor("#302C35")}
            : Appearance{QColor("#FFFEFB"), QColor("#F5F1F8"), QColor("#302E36"),
                         QColor("#726B7A"), QColor("#7055B5"), QColor("#DFDAE4"), QColor("#EEE8F8"), QColor("#FFFFFF")};
    }
};
