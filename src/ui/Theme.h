#pragma once

#include <QColor>
#include <QString>

/*! Shared palette and stylesheet for the control room. Mirrors the dark
 *  cyan/amber control-room look of the original prototype. */
namespace theme {

inline QColor background()   { return QColor(0x07, 0x09, 0x0e); }
inline QColor panel()        { return QColor(0x12, 0x18, 0x24); }
inline QColor panelDark()    { return QColor(0x0b, 0x10, 0x1a); }
inline QColor panelDeep()    { return QColor(0x09, 0x0d, 0x16); }
inline QColor border()       { return QColor(0x1e, 0x29, 0x3b); }
inline QColor borderLight()  { return QColor(0x33, 0x41, 0x55); }
inline QColor accent()       { return QColor(0x38, 0xbd, 0xf8); }
inline QColor cyan()         { return QColor(0x06, 0xb6, 0xd4); }
inline QColor success()      { return QColor(0x10, 0xb9, 0x81); }
inline QColor warning()      { return QColor(0xf5, 0x9e, 0x0b); }
inline QColor danger()       { return QColor(0xef, 0x44, 0x44); }
inline QColor purple()       { return QColor(0xa8, 0x55, 0xf7); }
inline QColor text()         { return QColor(0xcb, 0xd5, 0xe1); }
inline QColor textBright()   { return QColor(0xf1, 0xf5, 0xf9); }
inline QColor dim()          { return QColor(0x64, 0x74, 0x8b); }
inline QColor fuel()         { return QColor(0x0e, 0x74, 0x90); }
inline QColor controlRod()   { return QColor(0xf5, 0x9e, 0x0b); }
inline QColor reflector()    { return QColor(0x05, 0x96, 0x69); }
inline QColor emptySocket()  { return QColor(0x14, 0x1d, 0x2b); }

inline QString monoFamily() { return QStringLiteral("Consolas"); }

/*! Colour for a fuel-channel temperature: teal while nominal, amber when it
 *  gets hot, red near the damage threshold. */
inline QColor temperatureColor(double celsius)
{
    if (celsius >= 950.0)
        return QColor(0xff, 0x2d, 0x2d);
    if (celsius >= 820.0)
        return QColor(0xf9, 0x73, 0x16);
    if (celsius >= 720.0)
        return QColor(0xf5, 0x9e, 0x0b);
    if (celsius >= 640.0)
        return QColor(0x0d, 0x94, 0x88);
    return QColor(0x0e, 0x74, 0x90);
}

inline QColor alarmColor(int level)
{
    return level >= 2 ? danger() : (level == 1 ? warning() : success());
}

QString styleSheet();

} // namespace theme
