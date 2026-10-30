#include "ui/TurbineView.h"

#include "core/ReactorCore.h"
#include "ui/Theme.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QtMath>
#include <cmath>

namespace {
constexpr double kRatedRpm = 3000.0;
constexpr double kAxisY = 105.0;

// All machined surfaces share a single upper-left light source.
QLinearGradient metal(double top, double bottom, bool dark = false)
{
    QLinearGradient gradient(0, top, 0, bottom);
    gradient.setColorAt(0.0, dark ? theme::borderLight() : QColor(148, 163, 184));
    gradient.setColorAt(0.28, dark ? QColor(71, 85, 105) : QColor(203, 213, 225));
    gradient.setColorAt(0.52, dark ? theme::border() : QColor(100, 116, 139));
    gradient.setColorAt(1.0, theme::panelDeep());
    return gradient;
}
}

TurbineView::TurbineView(rbmk::ReactorCore *core, QWidget *parent)
    : QWidget(parent), m_core(core)
{
    setMinimumHeight(124);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAccessibleName(QStringLiteral("Turbine generator cutaway"));
    setToolTip(QStringLiteral("Illustrative HP / LP turbine cutaway and generator on one shaft. "
                              "Rotation is visually slowed; RPM is shown in the readout. "
                              "Governor controls export, not reactor power in this model."));
    m_clock.start();
    m_timer = new QTimer(this);
    m_timer->setInterval(33);
    connect(m_timer, &QTimer::timeout, this, [this] {
        const double dt = qBound(0.0, m_clock.restart() / 1000.0, 0.1);
        if (!m_core || !isVisible() || m_core->isPaused())
            return;
        const auto &t = m_core->telemetry();
        const double spin = qBound(0.0, t.turbineRpm / kRatedRpm, 1.1);
        // No idle motion: a cold shaft is still. All stages use one rotor phase.
        const double elapsed = dt * m_core->timeScale();
        m_phase = std::fmod(m_phase + elapsed * spin * 4.0, 2.0 * M_PI);
        const double steam = !t.turbineTripped && t.pressureAtm > 12.0
            ? t.turbineValvePct / 100.0 : 0.0;
        m_steamPhase = std::fmod(m_steamPhase + elapsed * steam * 18.0, 12.0);
        update();
    });
    m_timer->start();
}

TurbineView::State TurbineView::stateOf(const rbmk::Telemetry &t)
{
    if (t.turbineTripped)
        return State::Tripped;
    if (t.turbineSynced && t.turbineMW > 1.0)
        return State::OnGrid;
    if (t.turbineValvePct < 1.0)
        return State::GovernorShut;
    return State::RollingUp;
}

QString TurbineView::stateLabel(State state)
{
    switch (state) {
    case State::Tripped:      return QStringLiteral("TRIPPED");
    case State::OnGrid:       return QStringLiteral("ON GRID");
    case State::GovernorShut: return QStringLiteral("GOVERNOR SHUT");
    case State::RollingUp:    return QStringLiteral("ROLLING UP");
    }
    return QString();
}

void TurbineView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    p.fillRect(rect(), theme::panelDeep());
    // Uniform scaling preserves circular geometry and keeps label bands clear.
    const double scale = qMin(width() / 480.0, height() / 190.0);
    p.translate((width() - 480.0 * scale) / 2.0, (height() - 190.0 * scale) / 2.0);
    p.scale(scale, scale);
    p.setClipRect(QRectF(0, 0, 480, 190));
    const auto t = m_core ? m_core->telemetry() : rbmk::Telemetry();
    const double spin = qBound(0.0, t.turbineRpm / kRatedRpm, 1.0);
    const auto state = stateOf(t);
    const QColor status = state == State::Tripped ? theme::danger()
        : state == State::OnGrid ? theme::success() : theme::text();
    QFont label(theme::monoFamily());
    label.setPixelSize(11);
    p.setFont(label);
    p.setPen(theme::text());
    p.drawText(QRectF(14, 7, 230, 18), Qt::AlignVCenter, QStringLiteral("TG-1 / SHAFT CUTAWAY"));
    const QString badge = stateLabel(state);
    const double badgeWidth = QFontMetricsF(label).horizontalAdvance(badge) + 24;
    const QRectF badgeRect(466 - badgeWidth, 6, badgeWidth, 20);
    p.setPen(QPen(status.darker(180), 1));
    QColor badgeFill = status;
    badgeFill.setAlpha(20);
    p.setBrush(badgeFill);
    p.drawRoundedRect(badgeRect, 4, 4);
    p.setPen(status);
    p.drawText(badgeRect, Qt::AlignCenter, badge);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 100));
    p.drawEllipse(QRectF(32, 146, 416, 20));
    p.setPen(QPen(theme::borderLight(), 1));
    p.setBrush(metal(148, 161, true));
    p.drawRoundedRect(QRectF(32, 148, 416, 13), 3, 3);
    for (double x : { 51.0, 158.0, 289.0, 433.0 }) {
        p.setBrush(metal(113, 149, true));
        p.drawRoundedRect(QRectF(x - 8, 113, 16, 35), 2, 2);
        p.setBrush(metal(96, 115));
        p.drawRoundedRect(QRectF(x - 10, 96, 20, 19), 4, 4);
    }

    QPainterPath inlet;
    inlet.moveTo(24, 45);
    inlet.lineTo(96, 45);
    inlet.quadTo(104, 45, 104, 53);
    inlet.lineTo(104, 69);
    QPainterPath crossover;
    crossover.moveTo(143, 79);
    crossover.lineTo(156, 79);
    crossover.lineTo(156, 46);
    crossover.lineTo(211, 46);
    crossover.lineTo(211, 61);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(theme::borderLight(), 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(inlet);
    p.drawPath(crossover);
    if (!t.turbineTripped && t.turbineValvePct > 0.1 && t.pressureAtm > 12.0) {
        QPen flow(theme::cyan(), 2, Qt::CustomDashLine, Qt::RoundCap);
        flow.setDashPattern({ 3.0, 3.0 });
        flow.setDashOffset(-m_steamPhase);
        p.setPen(flow);
        p.drawPath(inlet);
        p.drawPath(crossover);
    }

    // Rear casings and common shaft, followed by rotors and front casing lips.
    for (const QRectF &shell : { QRectF(70, 72, 80, 66), QRectF(177, 58, 99, 88) }) {
        p.setBrush(metal(shell.top(), shell.bottom(), true));
        p.setPen(QPen(theme::borderLight(), 1));
        p.drawRoundedRect(shell, 12, 12);
        p.fillRect(shell.adjusted(6, 6, -6, -6), theme::panelDeep());
    }
    p.setPen(Qt::NoPen);
    p.setBrush(metal(101, 109));
    p.drawRect(QRectF(40, 101, 400, 8));
    for (int i = 0; i < 5; ++i)
        paintStage(p, 82 + i * 13.0, 23 + i * 1.6, spin);
    for (int i = 0; i < 6; ++i)
        paintStage(p, 190 + i * 14.0, 29 + i * 2.6, spin);
    for (const QRectF &front : { QRectF(70, 108, 80, 30), QRectF(177, 108, 99, 38) }) {
        p.setBrush(metal(front.top() - 12, front.bottom(), true));
        p.setPen(QPen(theme::borderLight(), 1));
        p.drawRoundedRect(front, 5, 5);
        p.setPen(QPen(QColor(100, 116, 139), 1));
        p.drawLine(front.topLeft() + QPointF(3, 1), front.topRight() + QPointF(-3, 1));
        for (double x = front.left() + 7; x < front.right(); x += 14) {
            p.setPen(Qt::NoPen);
            p.setBrush(theme::dim());
            p.drawEllipse(QPointF(x, front.top() + 5), 1.5, 1.5);
        }
    }

    p.setPen(QPen(theme::borderLight(), 1));
    p.setBrush(metal(96, 114));
    p.drawRoundedRect(QRectF(301, 96, 18, 18), 3, 3);
    p.setPen(QPen(theme::accent(), 2));
    const double stripe = std::sin(m_phase) * 6;
    p.drawLine(QPointF(303, kAxisY + stripe), QPointF(317, kAxisY + stripe));
    paintGenerator(p, 327, 77, 427, 133);

    p.setPen(theme::text());
    p.drawText(QRectF(65, 166, 90, 17), Qt::AlignCenter, QStringLiteral("HP TURBINE"));
    p.drawText(QRectF(177, 166, 99, 17), Qt::AlignCenter, QStringLiteral("LP TURBINE"));
    p.drawText(QRectF(325, 166, 112, 17), Qt::AlignCenter, QStringLiteral("GENERATOR"));
}

void TurbineView::paintStage(QPainter &p, double x, double radius, double spin) const
{
    constexpr int blades = 18;
    const auto project = [x](double angle, double r) {
        return QPointF(x + std::sin(angle) * r * 0.24, kAxisY + std::cos(angle) * r);
    };
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(148, 163, 184, int(35 + 55 * spin)), 2.0));
    p.drawEllipse(QPointF(x, kAxisY), radius * 0.24, radius);
    for (int i = 0; i < blades; ++i) {
        const double a = m_phase + i * 2.0 * M_PI / blades;
        QPolygonF blade;
        blade << project(a, radius * 0.25) << project(a + 0.08, radius)
              << project(a + 0.23, radius) << project(a + 0.20, radius * 0.25);
        const int light = int(112 + 65 * std::sin(a));
        p.setBrush(QColor(light, light + 10, qMin(255, light + 24)));
        p.setPen(QPen(theme::border(), 0.45));
        p.drawPolygon(blade);
    }
    p.setPen(QPen(theme::borderLight(), 0.7));
    p.setBrush(metal(kAxisY - radius * 0.28, kAxisY + radius * 0.28));
    p.drawEllipse(QPointF(x, kAxisY), 3.5, radius * 0.28);
}

void TurbineView::paintGenerator(QPainter &p, double left, double top,
                                 double right, double bottom) const
{
    const QRectF body(left, top, right - left, bottom - top);
    p.setPen(QPen(theme::dim(), 1));
    p.setBrush(metal(top, bottom, true));
    p.drawRoundedRect(body, 10, 10);
    for (double x = left + 12; x < right - 13; x += 6) {
        p.setPen(QPen(theme::panelDeep(), 1));
        p.drawLine(QPointF(x, top + 8), QPointF(x, bottom - 8));
    }
    p.setBrush(metal(top + 4, bottom - 4));
    p.setPen(QPen(theme::borderLight(), 1));
    p.drawEllipse(QRectF(right - 13, top + 4, 19, body.height() - 8));
    // The generator housing is stationary, not another spinning exposed wheel.
    p.setBrush(theme::panelDark());
    p.drawRoundedRect(QRectF(left + 27, top - 8, 36, 12), 2, 2);
    const bool exporting = m_core && m_core->telemetry().turbineMW > 1.0
        && !m_core->telemetry().turbineTripped;
    p.setBrush(exporting ? theme::success() : theme::dim());
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(left + 45, top - 2), 2, 2);
}