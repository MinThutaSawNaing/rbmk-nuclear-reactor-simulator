#include "ui/CoreView.h"

#include "core/ReactorCore.h"
#include "ui/Theme.h"

#include <QContextMenuEvent>
#include <QDateTime>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>

namespace {
constexpr double kLayoutSize = 600.0;
constexpr double kLayoutCentre = 300.0;
constexpr double kChannelStep = 500.0 / 18.0;

double clampd(double v, double lo, double hi)
{
    return qBound(lo, v, hi);
}

QString formatFlux(double flux)
{
    return QStringLiteral("%1e13").arg(flux / 1.0e13, 0, 'f', 2);
}
} // namespace

CoreView::CoreView(rbmk::ReactorCore *core, QWidget *parent)
    : QWidget(parent)
    , m_core(core)
{
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(420, 420);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void CoreView::setSelectedIndex(int index)
{
    const bool groupChanged = (index < 0 && !m_multi.isEmpty());
    if (groupChanged)
        m_multi.clear();
    if (m_selected == index) {
        if (groupChanged) {
            update();
            emit selectionChanged();
        }
        return;
    }
    m_selected = index;
    update();
    if (groupChanged)
        emit selectionChanged();
}

QVector<int> CoreView::selection() const
{
    QVector<int> result;
    result.reserve(m_multi.size() + 1);
    if (m_selected >= 0)
        result.append(m_selected);
    for (int index : m_multi) {
        if (index != m_selected)
            result.append(index);
    }
    std::sort(result.begin(), result.end());
    return result;
}

int CoreView::selectionCount() const
{
    return m_multi.size() + (m_selected >= 0 ? 1 : 0);
}

bool CoreView::isSelected(int index) const
{
    return index >= 0 && (index == m_selected || m_multi.contains(index));
}

void CoreView::notifySelectionChanged()
{
    update();
    emit selectionChanged();
}

void CoreView::clearMultiSelection()
{
    if (m_multi.isEmpty())
        return;
    m_multi.clear();
    notifySelectionChanged();
    emit statusMessage(QStringLiteral("Multi-selection cleared."));
}

void CoreView::selectAllControlRods()
{
    if (!m_core)
        return;
    m_multi.clear();
    int primary = -1;
    for (int i = 0; i < m_core->channelCount(); ++i) {
        if (!m_core->channel(i).isControlRod())
            continue;
        if (primary < 0)
            primary = i;
        else
            m_multi.insert(i);
    }
    m_selected = primary;
    notifySelectionChanged();
    emit channelSelected(m_selected);
    if (primary < 0)
        emit statusMessage(QStringLiteral("No control rod drives installed in this core."));
    else
        emit statusMessage(QStringLiteral("All %1 control rod drives selected - drag or wheel "
                                         "one to trim the group, Ctrl+click to deselect.")
                               .arg(m_multi.size() + 1));
}

void CoreView::toggleSelection(int index)
{
    if (!m_core || !m_core->isValidChannel(index))
        return;

    if (index == m_selected) {
        // Removing the primary hands the selection to another member.
        if (!m_multi.isEmpty()) {
            const auto it = std::min_element(m_multi.cbegin(), m_multi.cend());
            const int next = *it;
            m_multi.remove(next);
            m_selected = next;
        } else {
            m_selected = -1;
        }
    } else if (m_multi.contains(index)) {
        m_multi.remove(index);
    } else {
        if (m_selected >= 0)
            m_multi.insert(m_selected);   // the old primary becomes a group member
        m_selected = index;
    }

    notifySelectionChanged();
    emit channelSelected(m_selected);
    if (selectionCount() > 1)
        emit statusMessage(QStringLiteral("%1 sockets selected - Ctrl+click to add or remove, "
                                         "Esc to clear.")
                               .arg(selectionCount()));
    else if (m_selected >= 0)
        emit statusMessage(QStringLiteral("%1 selected. Ctrl+click to add more sockets.")
                               .arg(m_core->channel(m_selected).coord));
    else
        emit statusMessage(QStringLiteral("Selection cleared."));
}

void CoreView::extendSelectionTo(int index)
{
    if (!m_core || !m_core->isValidChannel(index))
        return;
    const int anchor = (m_selected >= 0) ? m_selected : index;
    const rbmk::Channel &from = m_core->channel(anchor);
    const rbmk::Channel &to = m_core->channel(index);
    const int rowLo = qMin(from.row, to.row);
    const int rowHi = qMax(from.row, to.row);
    const int colLo = qMin(from.col, to.col);
    const int colHi = qMax(from.col, to.col);

    m_multi.clear();
    for (int i = 0; i < m_core->channelCount(); ++i) {
        const rbmk::Channel &ch = m_core->channel(i);
        if (!ch.inside || i == anchor)
            continue;
        if (ch.row >= rowLo && ch.row <= rowHi && ch.col >= colLo && ch.col <= colHi)
            m_multi.insert(i);
    }
    m_selected = anchor;

    notifySelectionChanged();
    emit channelSelected(m_selected);
    emit statusMessage(QStringLiteral("%1 sockets selected (rectangle %2-%3).")
                           .arg(selectionCount())
                           .arg(from.coord, to.coord));
}

QVector<int> CoreView::selectedRodChannels() const
{
    QVector<int> rods;
    if (!m_core)
        return rods;
    for (int index : selection()) {
        if (m_core->isValidChannel(index) && m_core->channel(index).isControlRod())
            rods.append(index);
    }
    return rods;
}

void CoreView::resetZoom()
{
    m_zoom = 1.0;
    emit zoomChanged(m_zoom);
    update();
}

void CoreView::setShowCoordinates(bool show)
{
    m_showCoordinates = show;
    update();
}

void CoreView::changeZoom(double factor)
{
    const double next = clampd(m_zoom * factor, 1.0, 3.5);
    if (qFuzzyCompare(next, m_zoom))
        return;
    m_zoom = next;
    emit zoomChanged(m_zoom);
    update();
}

double CoreView::fitScale() const
{
    const double usable = qMin(width(), height()) - 26.0;
    return qMax(0.1, usable / kLayoutSize) * m_zoom;
}

double CoreView::socketSize() const
{
    return kChannelStep * 0.86;
}

QPointF CoreView::layoutToWidget(double x, double y) const
{
    const double s = fitScale();
    const QPointF centre(width() / 2.0, height() / 2.0);
    return centre + QPointF((x - kLayoutCentre) * s, (y - kLayoutCentre) * s);
}

QPointF CoreView::widgetToLayout(const QPointF &pos) const
{
    const double s = fitScale();
    const QPointF centre(width() / 2.0, height() / 2.0);
    return QPointF((pos.x() - centre.x()) / s + kLayoutCentre,
                   (pos.y() - centre.y()) / s + kLayoutCentre);
}

QPointF CoreView::channelCentre(int index) const
{
    if (!m_core || !m_core->isValidChannel(index))
        return QPointF();
    const rbmk::Channel &ch = m_core->channel(index);
    return layoutToWidget(ch.x, ch.y);
}

int CoreView::hitTest(const QPointF &widgetPos) const
{
    if (!m_core)
        return -1;
    const QPointF layout = widgetToLayout(widgetPos);
    const double half = socketSize() / 2.0;
    for (int i = 0; i < m_core->channelCount(); ++i) {
        const rbmk::Channel &ch = m_core->channel(i);
        if (!ch.inside)
            continue;
        if (std::fabs(layout.x() - ch.x) <= half && std::fabs(layout.y() - ch.y) <= half)
            return i;
    }
    return -1;
}

void CoreView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    QRadialGradient glow(rect().center(), qMax(width(), height()) * 0.7);
    glow.setColorAt(0.0, QColor(0x0d, 0x16, 0x24));
    glow.setColorAt(1.0, theme::background());
    p.fillRect(rect(), glow);

    const double s = fitScale();
    p.save();
    p.translate(width() / 2.0, height() / 2.0);
    p.scale(s, s);
    p.translate(-kLayoutCentre, -kLayoutCentre);

    drawShield(p);

    if (m_core) {
        for (int i = 0; i < m_core->channelCount(); ++i) {
            const rbmk::Channel &ch = m_core->channel(i);
            if (ch.inside)
                drawChannel(p, ch);
        }
    }

    p.restore();

    drawCoordinates(p);

    // CRT scanline overlay
    p.setOpacity(0.15);
    p.setPen(QPen(QColor(0, 0, 0, 90), 1));
    for (int y = 0; y < height(); y += 3)
        p.drawLine(0, y, width(), y);
    p.setOpacity(1.0);

    drawHud(p);

    if (m_hover >= 0 && m_core && m_hover != m_dragIndex)
        drawTooltip(p, m_core->channel(m_hover));
}

void CoreView::drawShield(QPainter &p) const
{
    QRadialGradient shield(QPointF(kLayoutCentre, kLayoutCentre), 280.0);
    shield.setColorAt(0.70, QColor(0x1e, 0x29, 0x3b));
    shield.setColorAt(0.90, QColor(0x33, 0x41, 0x55));
    shield.setColorAt(1.00, QColor(0x0f, 0x17, 0x2a));
    p.setBrush(shield);
    p.setPen(QPen(QColor(0x47, 0x55, 0x69), 7.0));
    p.drawEllipse(QPointF(kLayoutCentre, kLayoutCentre), 280.0, 280.0);

    p.setBrush(QColor(0x09, 0x0d, 0x16));
    p.setPen(QPen(theme::border(), 2.5, Qt::DashLine));
    p.drawEllipse(QPointF(kLayoutCentre, kLayoutCentre), 260.0, 260.0);

    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 1.2));
    for (int i = 0; i < 36; ++i) {
        const double a = i * M_PI / 18.0;
        const double r1 = 262.0;
        const double r2 = (i % 3 == 0) ? 272.0 : 267.0;
        p.drawLine(QPointF(kLayoutCentre + r1 * std::cos(a), kLayoutCentre + r1 * std::sin(a)),
                   QPointF(kLayoutCentre + r2 * std::cos(a), kLayoutCentre + r2 * std::sin(a)));
    }

    // column numbers along the bottom, margin-safe is handled in drawCoordinates

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::accent(), 1.4, Qt::DashLine));
    p.drawEllipse(QPointF(kLayoutCentre, kLayoutCentre), 86.0, 86.0);
    p.setPen(QPen(theme::danger(), 1.8));
    p.drawEllipse(QPointF(kLayoutCentre, kLayoutCentre), 30.0, 30.0);
    p.setPen(QPen(theme::border(), 0.8));
    p.drawLine(QPointF(kLayoutCentre, kLayoutCentre - 96), QPointF(kLayoutCentre, kLayoutCentre + 96));
    p.drawLine(QPointF(kLayoutCentre - 96, kLayoutCentre), QPointF(kLayoutCentre + 96, kLayoutCentre));
}

void CoreView::drawChannel(QPainter &p, const rbmk::Channel &ch) const
{
    const double size = socketSize();
    const QRectF base(ch.x - size / 2.0, ch.y - size / 2.0, size, size);

    const bool selected = (ch.index == m_selected);
    const bool grouped = m_multi.contains(ch.index);
    const bool hovered = (ch.index == m_hover);
    const bool hot = ch.type == rbmk::AssemblyType::Fuel && ch.localTemp >= 900.0;

    if (hot) {
        const double phase = 0.5 + 0.5 * std::sin(QDateTime::currentMSecsSinceEpoch() / 220.0);
        QRadialGradient halo(base.center(), size * 1.25);
        QColor red = theme::danger();
        red.setAlphaF(0.28 + 0.42 * phase);
        halo.setColorAt(0.0, red);
        halo.setColorAt(1.0, QColor(255, 0, 0, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(halo);
        p.drawEllipse(base.center(), size * 0.95, size * 0.95);
    }

    switch (ch.type) {
    case rbmk::AssemblyType::Fuel: {
        const QColor c = theme::temperatureColor(ch.localTemp);
        p.setBrush(c);
        p.setPen(QPen(c.darker(165), 0.7));
        p.drawRoundedRect(base, 1.8, 1.8);
        break;
    }
    case rbmk::AssemblyType::Reflector: {
        QColor c = theme::reflector();
        c.setAlphaF(0.85);
        p.setBrush(c);
        p.setPen(QPen(c.darker(150), 0.7));
        p.drawRoundedRect(base, 1.8, 1.8);
        break;
    }
    case rbmk::AssemblyType::Control: {
        p.setBrush(QColor(0x22, 0x1a, 0x0a));
        p.setPen(QPen(theme::warning().darker(120), 1.1));
        p.drawRoundedRect(base, 2.2, 2.2);

        // insertion depth: bar dropping in from the top of the socket
        const double frac = clampd(ch.rodPosition / 100.0, 0.0, 1.0);
        if (frac > 0.01) {
            QRectF fill = base.adjusted(1.5, 1.5, -1.5, -1.5);
            fill.setHeight(fill.height() * frac);
            QColor amber = theme::warning();
            amber.setAlphaF(0.65 + 0.35 * frac);
            p.setPen(Qt::NoPen);
            p.setBrush(amber);
            p.drawRoundedRect(fill, 1.5, 1.5);
            // bright tip so the insertion level is readable at a glance
            p.setPen(QPen(QColor(0xfe, 0xf3, 0xc7), 0.9));
            p.drawLine(QPointF(fill.left() + 1.0, fill.bottom()),
                       QPointF(fill.right() - 1.0, fill.bottom()));
        }
        p.setPen(QPen(theme::warning().lighter(120), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(base, 2.2, 2.2);
        break;
    }
    case rbmk::AssemblyType::Empty: {
        p.setBrush(theme::emptySocket());
        p.setPen(QPen(theme::borderLight(), 0.8, Qt::DashLine));
        p.drawRoundedRect(base, 1.8, 1.8);
        p.setPen(QPen(theme::borderLight(), 0.7));
        const double inset = size * 0.33;
        p.drawLine(QPointF(base.left() + inset, base.top() + inset),
                   QPointF(base.right() - inset, base.bottom() - inset));
        p.drawLine(QPointF(base.right() - inset, base.top() + inset),
                   QPointF(base.left() + inset, base.bottom() - inset));
        break;
    }
    }

    if (hovered) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme::accent(), 1.6));
        p.drawRoundedRect(base.adjusted(-1.0, -1.0, 1.0, 1.0), 3.0, 3.0);
    }
    if (selected) {
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme::textBright(), 1.6));
        p.drawRoundedRect(base.adjusted(-1.6, -1.6, 1.6, 1.6), 3.4, 3.4);
        p.setPen(QPen(theme::accent(), 0.9));
        p.drawRoundedRect(base.adjusted(-3.4, -3.4, 3.4, 3.4), 4.6, 4.6);
    }
    if (grouped) {
        // group member: translucent wash plus a cyan ring and corner pip
        QColor wash = theme::accent();
        wash.setAlphaF(0.16);
        p.setBrush(wash);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(base, 1.8, 1.8);

        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(theme::accent(), 1.3));
        p.drawRoundedRect(base.adjusted(-1.2, -1.2, 1.2, 1.2), 3.0, 3.0);

        p.setPen(Qt::NoPen);
        p.setBrush(theme::accent());
        p.drawEllipse(QPointF(base.right() - 1.0, base.top() + 1.0), 1.7, 1.7);
    }
}

void CoreView::drawHud(QPainter &p) const
{
    QFont f = font();
    f.setPointSizeF(8.0);
    f.setFamily(theme::monoFamily());
    p.setFont(f);

    QFont small = f;
    small.setPointSizeF(7.0);

    p.setPen(QColor(0x47, 0x55, 0x69));
    p.drawText(QRectF(12, 8, width() - 24, 14), Qt::AlignLeft,
               QStringLiteral("IZ-250 UPPER BIOLOGICAL SHIELD  //  TOP-DOWN TELEMETRY"));

    if (m_core && m_selected >= 0 && m_core->isValidChannel(m_selected)) {
        const rbmk::Channel &ch = m_core->channel(m_selected);
        QString info = QStringLiteral("%1  %2").arg(ch.coord, rbmk::assemblyNameQ(ch.type));
        if (ch.isControlRod())
            info += QStringLiteral("   INSERTION %1 %%  %2")
                        .arg(ch.rodPosition, 0, 'f', 1)
                        .arg(ch.followBank ? QStringLiteral("(bank)") : QStringLiteral("(independent)"));
        p.setPen(theme::accent());
        p.drawText(QRectF(12, 24, width() - 24, 16), Qt::AlignLeft, info);
    } else {
        p.setPen(theme::dim());
        p.drawText(QRectF(12, 24, width() - 24, 16), Qt::AlignLeft,
                   QStringLiteral("no channel selected - click a socket to inspect it"));
    }

    if (m_core && !m_multi.isEmpty()) {
        int rods = 0;
        double targetSum = 0.0;
        for (int index : selection()) {
            const rbmk::Channel &ch = m_core->channel(index);
            if (ch.isControlRod()) {
                ++rods;
                targetSum += ch.rodTarget;
            }
        }
        QString summary = QStringLiteral("%1 SOCKETS IN GROUP   ·   ctrl+click to edit")
                              .arg(selectionCount());
        if (rods > 0)
            summary += QStringLiteral("   ·   %1 RODS   avg target %2 %%")
                           .arg(rods)
                           .arg(targetSum / rods, 0, 'f', 1);
        p.setPen(theme::warning());
        p.drawText(QRectF(12, 40, width() - 24, 16), Qt::AlignLeft, summary);
    }

    p.setFont(small);
    p.setPen(theme::dim());
    p.drawText(QRectF(12, 8, width() - 24, 14), Qt::AlignRight,
               QStringLiteral("ZOOM %1 %").arg(m_zoom * 100.0, 0, 'f', 0));

    if (m_core) {
        const rbmk::Telemetry &t = m_core->telemetry();
        if (t.paused) {
            p.setFont(f);
            p.setPen(theme::warning());
            p.drawText(QRectF(0, height() / 2.0 - 14, width(), 28), Qt::AlignCenter,
                       QStringLiteral("SIMULATION PAUSED"));
        }
        if (t.destroyed) {
            p.setBrush(QColor(40, 0, 0, 190));
            p.setPen(QPen(theme::danger(), 2));
            const QRectF banner(width() * 0.12, height() / 2.0 - 46, width() * 0.76, 92);
            p.drawRoundedRect(banner, 8, 8);
            QFont big = f;
            big.setPointSizeF(16.0);
            big.setBold(true);
            p.setFont(big);
            p.setPen(theme::danger());
            p.drawText(banner.adjusted(8, 12, -8, -40), Qt::AlignCenter,
                       QStringLiteral("CORE DESTROYED"));
            p.setFont(small);
            p.setPen(theme::text());
            p.drawText(banner.adjusted(8, 54, -8, -8), Qt::AlignCenter,
                       QStringLiteral("fuel melted - rebuild the core (Ctrl+R)"));
        }
    }
}

void CoreView::drawTooltip(QPainter &p, const rbmk::Channel &ch) const
{
    QStringList lines;
    lines << QStringLiteral("%1  //  %2").arg(ch.coord, rbmk::assemblyNameQ(ch.type));
    if (ch.isControlRod()) {
        lines << QStringLiteral("insertion   %1 %  (target %2 %)")
                     .arg(ch.rodPosition, 0, 'f', 1)
                     .arg(ch.rodTarget, 0, 'f', 1);
        lines << QStringLiteral("drive       %1")
                     .arg(ch.followBank ? QStringLiteral("following bank") : QStringLiteral("independent"));
        lines << QStringLiteral("worth       %1 $")
                     .arg(rbmk::ReactorCore::rodWorthDollars(ch.rodWorth, ch.rodPosition),
                          0, 'f', 3);
    } else if (ch.type == rbmk::AssemblyType::Fuel) {
        lines << QStringLiteral("local temp  %1 degC").arg(ch.localTemp, 0, 'f', 0);
        lines << QStringLiteral("local flux  %1 n/cm2s").arg(formatFlux(ch.localFlux));
        lines << QStringLiteral("burnup      %1 %").arg(ch.burnup * 100.0, 0, 'f', 2);
    } else if (ch.type == rbmk::AssemblyType::Reflector) {
        lines << QStringLiteral("water column - reflects neutrons");
        lines << QStringLiteral("local temp  %1 degC").arg(ch.localTemp, 0, 'f', 0);
    } else {
        lines << QStringLiteral("empty channel - no assembly loaded");
        lines << QStringLiteral("right-click to install fuel, rod or reflector");
    }

    QFont f = font();
    f.setPointSizeF(7.5);
    f.setFamily(theme::monoFamily());
    p.setFont(f);
    const QFontMetrics fm(f);

    int textWidth = 0;
    for (const QString &line : lines)
        textWidth = qMax(textWidth, fm.horizontalAdvance(line));
    const int lineHeight = fm.height();
    const int boxWidth = textWidth + 18;
    const int boxHeight = lines.size() * lineHeight + 14;

    QPointF anchor = layoutToWidget(ch.x, ch.y) + QPointF(socketSize() * fitScale() * 0.8, 4);
    double x = anchor.x();
    double y = anchor.y();
    if (x + boxWidth > width() - 6)
        x = width() - boxWidth - 6;
    if (y + boxHeight > height() - 26)
        y = height() - boxHeight - 26;
    x = qMax(6.0, x);
    y = qMax(6.0, y);

    const QRectF box(x, y, boxWidth, boxHeight);
    p.setBrush(QColor(10, 16, 26, 238));
    p.setPen(QPen(theme::accent(), 1.0));
    p.drawRoundedRect(box, 5, 5);

    double ty = y + 8;
    for (int i = 0; i < lines.size(); ++i) {
        p.setPen(i == 0 ? theme::accent() : theme::text());
        p.drawText(QPointF(x + 9, ty + fm.ascent()), lines.at(i));
        ty += lineHeight;
    }
}

void CoreView::drawCoordinates(QPainter &p) const
{
    const double s = fitScale();
    const double cx = width() / 2.0;
    const double cy = height() / 2.0;
    const double ringRadius = 280.0 * s;

    QFont label = font();
    label.setPointSizeF(7.0);
    label.setFamily(theme::monoFamily());
    p.setFont(label);
    p.setPen(QColor(0x64, 0x74, 0x8b));

    const char *letters = "ABCDEFGHJKLMNOPRST";

    // Row letters just outside the shield on the left.
    const int labelX = static_cast<int>(cx - ringRadius) - 26;
    if (labelX >= 2) {
        for (int row = 0; row < 18; row += 3) {
            const double y = layoutToWidget(0.0, kLayoutCentre - 250.0 + row * kChannelStep
                                                     + kChannelStep / 2.0).y();
            p.drawText(QRectF(labelX, y - 7, 22, 14), Qt::AlignRight | Qt::AlignVCenter,
                       QString(QLatin1Char(letters[row])));
        }
    }

    // Column numbers just under the shield, if the widget has the room.
    const int labelY = static_cast<int>(cy + ringRadius) + 4;
    if (labelY + 12 < height() - 2) {
        for (int col = 2; col < 18; col += 3) {
            const double x = layoutToWidget(kLayoutCentre - 250.0 + col * kChannelStep
                                                + kChannelStep / 2.0, 0.0).x();
            p.drawText(QRectF(x - 12, labelY, 24, 14), Qt::AlignCenter,
                       QString::number(col + 1));
        }
    }
}

// ------------------------------------------------------------------ interaction
void CoreView::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    const int index = hitTest(event->position());
    const bool ctrl = event->modifiers().testFlag(Qt::ControlModifier);
    const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);

    if (event->button() == Qt::LeftButton) {
        if (index >= 0 && ctrl && !shift) {
            toggleSelection(index);
            m_dragIndex = -1;
            event->accept();
            return;
        }
        if (index >= 0 && shift && m_selected >= 0) {
            extendSelectionTo(index);
            m_dragIndex = -1;
            event->accept();
            return;
        }

        const bool inGroup = isSelected(index);
        if (!inGroup && !m_multi.isEmpty()) {
            m_multi.clear();          // a click outside the group starts a new selection
            emit selectionChanged();
        } else if (inGroup && index != m_selected && m_selected >= 0) {
            m_multi.remove(index);    // keep the set intact, only move the primary
            m_multi.insert(m_selected);
        }

        m_selected = index;
        emit channelSelected(index);
        if (index >= 0 && m_core && m_core->channel(index).isControlRod()) {
            m_dragIndex = index;
            m_dragStartY = event->position().y();
            m_dragStartValue = m_core->channel(index).rodTarget;
            m_dragging = false;
            m_dragStarts.clear();
            const QVector<int> rods = selectedRodChannels();
            if (rods.size() > 1) {
                for (int rod : rods)
                    m_dragStarts.insert(rod, m_core->channel(rod).rodTarget);
            }
        } else {
            m_dragIndex = -1;
            m_dragStarts.clear();
        }
        update();
    }
    QWidget::mousePressEvent(event);
}

void CoreView::mouseMoveEvent(QMouseEvent *event)
{
    const int index = hitTest(event->position());
    if (index != m_hover) {
        m_hover = index;
        update();
    }

    if (m_dragIndex >= 0 && (event->buttons() & Qt::LeftButton) && m_core) {
        const double dy = event->position().y() - m_dragStartY;
        if (!m_dragging && std::fabs(dy) > 4.0)
            m_dragging = true;
        if (m_dragging) {
            // Dragging the full height of the widget covers the complete stroke.
            const double pctPerPixel = 100.0 / qMax(120.0, height() * 0.72);
            const double delta = dy * pctPerPixel;
            if (m_dragStarts.size() > 1) {
                double lead = m_dragStartValue;
                for (auto it = m_dragStarts.cbegin(); it != m_dragStarts.cend(); ++it) {
                    const double target = clampd(it.value() + delta, 0.0, 100.0);
                    emit rodInsertionRequested(it.key(), target);
                    if (it.key() == m_dragIndex)
                        lead = target;
                }
                emit statusMessage(QStringLiteral("%1 drives moving together   ·   lead %2: %3 %")
                                       .arg(m_dragStarts.size())
                                       .arg(m_core->channel(m_dragIndex).coord)
                                       .arg(lead, 0, 'f', 0));
            } else {
                const double target = clampd(m_dragStartValue + delta, 0.0, 100.0);
                emit rodInsertionRequested(m_dragIndex, target);
                emit statusMessage(QStringLiteral("%1: drive moving to %2 %")
                                       .arg(m_core->channel(m_dragIndex).coord)
                                       .arg(target, 0, 'f', 0));
            }
            event->accept();
            return;
        }
    }
    QWidget::mouseMoveEvent(event);
}

void CoreView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging && m_core && m_dragIndex >= 0) {
        if (m_dragStarts.size() > 1) {
            emit statusMessage(QStringLiteral("%1 drives trimmed together - all detached from the bank")
                                   .arg(m_dragStarts.size()));
        } else {
            emit statusMessage(QStringLiteral("%1: rod drive at %2 % - detached from bank")
                                   .arg(m_core->channel(m_dragIndex).coord)
                                   .arg(m_core->channel(m_dragIndex).rodTarget, 0, 'f', 0));
        }
    }
    m_dragging = false;
    m_dragIndex = -1;
    m_dragStarts.clear();
    QWidget::mouseReleaseEvent(event);
}

void CoreView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && hitTest(event->position()) < 0) {
        resetZoom();
        emit statusMessage(QStringLiteral("View reset to 100 %."));
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void CoreView::wheelEvent(QWheelEvent *event)
{
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        changeZoom(event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12);
        event->accept();
        return;
    }

    const int index = hitTest(event->position());
    if (index >= 0 && m_core && m_core->channel(index).isControlRod()) {
        const double delta = event->angleDelta().y() > 0 ? -3.0 : 3.0;

        // Wheel over a rod that belongs to a multi-selection trims the whole group.
        QVector<int> rods;
        if (selectionCount() > 1 && isSelected(index))
            rods = selectedRodChannels();
        if (rods.size() > 1) {
            double targetSum = 0.0;
            for (int rod : rods) {
                const double target = clampd(m_core->channel(rod).rodTarget + delta, 0.0, 100.0);
                emit rodInsertionRequested(rod, target);
                targetSum += target;
            }
            emit statusMessage(QStringLiteral("%1 drives trimmed %2 3 %   ·   avg target %3 %")
                                   .arg(rods.size())
                                   .arg(delta < 0 ? QStringLiteral("out by") : QStringLiteral("in by"))
                                   .arg(targetSum / rods.size(), 0, 'f', 0));
            event->accept();
            return;
        }

        const double target = clampd(m_core->channel(index).rodTarget + delta, 0.0, 100.0);
        emit rodInsertionRequested(index, target);
        emit statusMessage(QStringLiteral("%1: %2 %")
                               .arg(m_core->channel(index).coord)
                               .arg(target, 0, 'f', 0));
        event->accept();
        return;
    }

    changeZoom(event->angleDelta().y() > 0 ? 1.12 : 1.0 / 1.12);
    event->accept();
}

void CoreView::contextMenuEvent(QContextMenuEvent *event)
{
    const int index = hitTest(event->pos());
    if (index < 0) {
        event->ignore();
        return;
    }
    // Right-clicking inside the group keeps it (the menu then acts on all of it);
    // right-clicking elsewhere starts a fresh single selection.
    if (m_multi.contains(index)) {
        m_multi.remove(index);
        if (m_selected >= 0 && m_selected != index)
            m_multi.insert(m_selected);
    } else if (!m_multi.isEmpty()) {
        m_multi.clear();
        emit selectionChanged();
    }
    m_selected = index;
    emit channelSelected(index);
    update();
    emit contextMenuRequested(index, event->globalPos());
    event->accept();
}

void CoreView::leaveEvent(QEvent *event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(event);
}

void CoreView::keyPressEvent(QKeyEvent *event)
{
    if (!m_core) {
        QWidget::keyPressEvent(event);
        return;
    }

    const int current = m_selected;
    int next = -1;
    const int row = current / 18;
    const int col = current % 18;

    switch (event->key()) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down: {
        if (current < 0) {
            for (int i = 0; i < m_core->channelCount(); ++i) {
                if (m_core->channel(i).inside) {
                    next = i;
                    break;
                }
            }
        } else {
            const int dr = (event->key() == Qt::Key_Up) ? -1 : (event->key() == Qt::Key_Down ? 1 : 0);
            const int dc = (event->key() == Qt::Key_Left) ? -1 : (event->key() == Qt::Key_Right ? 1 : 0);
            for (int step = 1; step <= 18; ++step) {
                const int r = row + dr * step;
                const int c = col + dc * step;
                if (r < 0 || r >= 18 || c < 0 || c >= 18)
                    break;
                const int candidate = r * 18 + c;
                if (candidate < m_core->channelCount() && m_core->channel(candidate).inside) {
                    next = candidate;
                    break;
                }
            }
        }
        if (next >= 0) {
            const bool hadGroup = !m_multi.isEmpty();
            m_multi.clear();
            m_selected = next;
            emit channelSelected(next);
            if (hadGroup)
                emit selectionChanged();
            update();
            event->accept();
            return;
        }
        break;
    }
    case Qt::Key_Escape: {
        if (!m_multi.isEmpty()) {
            clearMultiSelection();
            event->accept();
            return;
        }
        break;
    }
    case Qt::Key_Plus:
    case Qt::Key_Equal:
    case Qt::Key_Minus:
    case Qt::Key_Underscore: {
        const bool withdraw = (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal);
        const double step = withdraw ? -5.0 : 5.0;
        const QVector<int> rods = selectedRodChannels();
        if (!rods.isEmpty()) {
            double targetSum = 0.0;
            for (int rod : rods) {
                const double target = clampd(m_core->channel(rod).rodTarget + step, 0.0, 100.0);
                emit rodInsertionRequested(rod, target);
                targetSum += target;
            }
            if (rods.size() > 1)
                emit statusMessage(QStringLiteral("%1 drives trimmed %2 5 %   ·   avg target %3 %")
                                       .arg(rods.size())
                                       .arg(step < 0 ? QStringLiteral("out by") : QStringLiteral("in by"))
                                       .arg(targetSum / rods.size(), 0, 'f', 0));
            else
                emit statusMessage(QStringLiteral("%1: %2 %")
                                       .arg(m_core->channel(rods.first()).coord)
                                       .arg(targetSum, 0, 'f', 0));
            event->accept();
            return;
        }
        break;
    }
    default:
        break;
    }
    QWidget::keyPressEvent(event);
}
