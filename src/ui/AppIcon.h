#pragma once

#include <QIcon>
#include <QPixmap>

/*! Application branding. Every icon in the product — window, taskbar, About
 *  dialog, shortcut and executable — is derived from the RBMK research logo in
 *  ``logos/`` and embedded through the Qt resource system (see tools/make_icons.py). */
namespace appicon {

//! Multi-resolution window/taskbar icon built from the embedded PNG set.
QIcon windowIcon();

//! Logo rendered at \a side logical pixels for a widget on a screen with
//! \a devicePixelRatio, so the header badge stays sharp on HiDPI displays.
QPixmap logo(int side, qreal devicePixelRatio = 1.0);

} // namespace appicon
