#include "ui/TurbineView.h"

#include "core/ReactorCore.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QTimer>
#include <QVector>
#include <QtMath>

#include <algorithm>

namespace {

constexpr double kDiscTilt = 0.44;   // foreshortening of the bladed disc
constexpr double kDrumDepth = 0.22;  // drum thickness, fraction of the radius
constexpr double kRatedRpm = 3000.0;
constexpr int kBladeCount = 26;

QColor mixColor(const QColor &a, const QColor &b, double t)
{
    const double k = qBound(0.0, t, 1.0);
    return QColor(int(a.red() + (b.red() - a.red()) * k),
                  int(a.green() + (b.green() - a.green()) * k),
                  int(a.blue() + (b.blue() - a.blue()) * k));
}

} // namespace

TurbineView::TurbineView(rbmk::ReactorCore *core, QWidget *parent)
    : QWidget(parent)
    , m_core(core)
{
    setMinimumHeight(124);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip(QStringLiteral("TG-1 turbo-generator. The governor valve, trip and roll-up "
                              "are on the turbine card; the shaft follows the plant."));
    m_lastMs = QDateTime::currentMSecsSinceEpoch();

    m_timer = new QTimer(this);
    m_timer->setInterval(33); // ~30 fps
    connect(m_timer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const double dt = qBound(0.0, (now - m_lastMs) / 1000.0, 0.25);
        m_lastMs = now;

        double spin = 0.0;
        if (m_core)
            spin = qBound(0.0, m_core->telemetry().turbineRpm / kRatedRpm, 1.0);

        m_phase += dt * (0.8 + 10.0 * spin);            // visual blade speed
        m_phase = std::fmod(m_phase, 2.0 * M_PI);
        m_steamPhase += dt * (14.0 + 30.0 * spin);      // steam pulses
        m_steamPhase = std::fmod(m_steamPhase, 64.0);
        update();
    });
    m_timer->start();
}

void TurbineView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    const rbmk::Telemetry t = m_core ? m_core->telemetry() : rbmk::Telemetry();

    QLinearGradient bg(0, 0, 0, height());
    bg.setColorAt(0.0, QColor(0x0b, 0x10, 0x1a));
    bg.setColorAt(1.0, QColor(0x07, 0x09, 0x0e));
    p.fillRect(rect(), bg);

    const double w = width();
    const double h = height();
    const double valve = qBound(0.0, t.turbineValvePct / 100.0, 1.0);
    const bool tripped = t.turbineTripped;

    // ------------------------------------------------------------- base skid
    const QRectF skid(w * 0.05, h * 0.79, w * 0.90, h * 0.10);
    QLinearGradient skidGrad(skid.topLeft(), skid.bottomLeft());
    skidGrad.setColorAt(0.0, QColor(0x1e, 0x29, 0x3b));
    skidGrad.setColorAt(1.0, QColor(0x0b, 0x10, 0x1a));
    p.setBrush(skidGrad);
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 1.0));
    p.drawRoundedRect(skid, 4, 4);

    // -------------------------------------------------------- machine geometry
    // Two reserved text bands keep the labels clear of the machine at any size.
    // They are kept tight so a short card still leaves the machine its height.
    const double topBand = 20.0;
    const double bottomBand = 18.0;
    const double machineTop = topBand;
    const double machineBottom = qMax(machineTop + 40.0, h - bottomBand);
    const double machineH = machineBottom - machineTop;

    const QPointF wheel(w * 0.30, machineTop + machineH * 0.55);
    const double radius = qMin(w * 0.20, machineH * 0.44);

    const QPointF inletStart(w * 0.04, machineTop + 4.0);
    const QPointF inletCorner(wheel.x() - radius * 0.95, machineTop + 4.0);
    const QPointF inletEnd(inletCorner.x(), wheel.y() - radius * 0.62);

    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 6.0, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(inletStart, inletCorner);
    p.drawLine(inletCorner, inletEnd);

    if (valve > 0.01) {
        QColor steam = tripped ? theme::danger().lighter(120) : theme::cyan();
        steam.setAlphaF(0.30 + 0.55 * valve);
        QPen steamPen(steam, 2.4, Qt::CustomDashLine, Qt::RoundCap);
        steamPen.setDashPattern({ 1.6, 2.4 });
        steamPen.setDashOffset(m_steamPhase);
        p.setPen(steamPen);
        p.drawLine(inletStart, inletCorner);
        p.drawLine(inletCorner, inletEnd);
    }

    paintWheel(p, wheel, radius);

    // ---------------------------------------------------------- generator body
    const double shaftY = wheel.y();
    const double genLeft = wheel.x() + radius * 1.42;
    const double genRight = w * 0.95;
    const double genTop = shaftY - radius * 0.52;
    const double genBottom = shaftY + radius * 0.52;

    // coupling shaft between turbine and generator
    QLinearGradient shaftGrad(0, shaftY - radius * 0.12, 0, shaftY + radius * 0.12);
    shaftGrad.setColorAt(0.0, QColor(0x94, 0xa3, 0xb8));
    shaftGrad.setColorAt(0.45, QColor(0x47, 0x55, 0x69));
    shaftGrad.setColorAt(1.0, QColor(0x1e, 0x29, 0x3b));
    p.setBrush(shaftGrad);
    p.setPen(QPen(QColor(0x0b, 0x10, 0x1a), 0.9));
    p.drawRoundedRect(QRectF(wheel.x() + radius * 0.9, shaftY - radius * 0.10,
                             genLeft - (wheel.x() + radius * 0.9), radius * 0.20),
                      2.0, 2.0);

    paintGenerator(p, genLeft, genTop, genRight, genBottom);

    // ------------------------------------------------------------------ labels
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);

    QFont small = mono;
    small.setPointSizeF(7.5);
    p.setFont(small);

    // top band: export / governor on the left, state lamp on the right
    p.setPen(theme::success());
    p.drawText(QRectF(4.0, 0.0, w * 0.55, topBand),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 MWe   ·   GOV %2 %")
                   .arg(t.turbineMW, 0, 'f', 0)
                   .arg(t.turbineValvePct, 0, 'f', 0));

    const bool onGrid = t.turbineSynced && !tripped;
    const QColor lamp = tripped ? theme::danger() : (onGrid ? theme::success() : theme::warning());
    const QString lampText = tripped ? QStringLiteral("TRIPPED")
                                     : (onGrid ? QStringLiteral("ON GRID")
                                               : QStringLiteral("COASTING"));
    const QRectF lampRect(w - 106.0, 2.0, 102.0, topBand - 4.0);
    QColor lampFill = lamp;
    lampFill.setAlphaF(0.16);
    p.setBrush(lampFill);
    p.setPen(QPen(lamp, 1.1));
    p.drawRoundedRect(lampRect, 4, 4);
    p.setBrush(lamp);
    p.setPen(Qt::NoPen);
    p.drawEllipse(QPointF(lampRect.left() + 9.0, lampRect.center().y()), 3.0, 3.0);
    p.setPen(lamp.lighter(115));
    p.drawText(lampRect.adjusted(16, 0, -4, 0), Qt::AlignLeft | Qt::AlignVCenter, lampText);

    // bottom band: machine tag and shaft speed
    p.setFont(small);
    p.setPen(QColor(0x64, 0x74, 0x8b));
    p.drawText(QRectF(4.0, h - bottomBand - 2.0, 42.0, bottomBand),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TG-1"));

    QFont big = mono;
    big.setPointSizeF(10.0);
    big.setBold(true);
    p.setFont(big);
    p.setPen(QColor(0x67, 0xe8, 0xf9));
    p.drawText(QRectF(48.0, h - bottomBand - 4.0, w - 52.0, bottomBand + 2.0),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 rpm").arg(t.turbineRpm, 0, 'f', 0));
}

/*!
 * The rotor wheel, drawn as a bladed disc inclined towards the viewer.
 *
 * Depth ordering is what sells the 3-D read: the far half of the drum is drawn
 * first, then the drum face (which occludes it), then the near half. Blades are
 * shaded by their distance from the eye, the drum is a short cylinder wall and
 * the stator casing closes the assembly with a front arc across the bottom.
 */
void TurbineView::paintWheel(QPainter &p, const QPointF &centre, double radius) const
{
    const rbmk::Telemetry t = m_core ? m_core->telemetry() : rbmk::Telemetry();
    const double spin = qBound(0.0, t.turbineRpm / kRatedRpm, 1.0);
    const bool tripped = t.turbineTripped;

    const double depth = radius * kDrumDepth;
    const double hub = radius * 0.34;

    // project a polar point of the disc: the far side is lifted, giving the drum
    // its thickness and the blades a real front/back ordering
    const auto proj = [&](double a, double r) {
        const double s = std::sin(a);
        return QPointF(centre.x() + r * std::cos(a),
                       centre.y() + r * s * kDiscTilt - s * depth);
    };

    // --- stator casing back plate -------------------------------------------
    QRadialGradient plate(centre, radius * 1.34);
    plate.setColorAt(0.0, QColor(0x14, 0x1d, 0x2b));
    plate.setColorAt(0.78, QColor(0x0f, 0x17, 0x24));
    plate.setColorAt(1.0, QColor(0x07, 0x09, 0x0e));
    p.setBrush(plate);
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 1.2));
    p.drawEllipse(centre, radius * 1.34, radius * 1.34 * kDiscTilt);

    // --- stator vanes --------------------------------------------------------
    p.setPen(QPen(QColor(0x33, 0x41, 0x55), 1.1));
    for (int i = 0; i < 24; ++i) {
        const double a = i * M_PI / 12.0;
        p.drawLine(proj(a, radius * 1.06), proj(a, radius * 1.24));
    }

    // --- drum wall: the far rim shows above the face --------------------------
    QPainterPath farFace;
    farFace.addEllipse(QPointF(centre.x(), centre.y() - depth), hub * 1.05, hub * kDiscTilt);
    QPainterPath face;
    face.addEllipse(centre, hub * 1.05, hub * kDiscTilt);
    p.setBrush(QColor(0x33, 0x41, 0x55));
    p.setPen(Qt::NoPen);
    p.drawPath(farFace.subtracted(face));

    // --- blades --------------------------------------------------------------
    const auto drawBlade = [&](double a) {
        const double halfAng = 0.052;
        const double twist = 0.11;
        QPolygonF blade;
        blade << proj(a - halfAng, hub)
              << proj(a + halfAng, hub)
              << proj(a + halfAng * 0.62 + twist, radius)
              << proj(a - halfAng * 0.62 + twist, radius);

        const double lit = qBound(0.0, 0.40 + 0.60 * std::sin(a), 1.0);
        QColor c = mixColor(QColor(0x47, 0x55, 0x69), QColor(0xd3, 0xdd, 0xe9), lit);
        if (tripped)
            c = mixColor(c, QColor(0x7f, 0x1d, 0x1d), 0.45);
        p.setBrush(c);
        p.setPen(QPen(c.darker(190), 0.5));
        p.drawPolygon(blade);
    };

    QVector<double> farSide;
    QVector<double> nearSide;
    for (int i = 0; i < kBladeCount; ++i) {
        const double a = m_phase + i * 2.0 * M_PI / kBladeCount;
        if (std::sin(a) < 0.0)
            farSide.append(a);
        else
            nearSide.append(a);
    }
    const auto byDepth = [](double a, double b) { return std::sin(a) < std::sin(b); };
    std::sort(farSide.begin(), farSide.end(), byDepth);
    std::sort(nearSide.begin(), nearSide.end(), byDepth);

    for (double a : farSide)
        drawBlade(a);

    // --- drum face (occludes the far blades) ---------------------------------
    QRadialGradient hubFace(centre - QPointF(hub * 0.30, hub * 0.40), hub * 1.6);
    hubFace.setColorAt(0.0, QColor(0x94, 0xa3, 0xb8));
    hubFace.setColorAt(0.55, QColor(0x47, 0x55, 0x69));
    hubFace.setColorAt(1.0, QColor(0x1e, 0x29, 0x3b));
    p.setBrush(hubFace);
    p.setPen(QPen(QColor(0x0b, 0x10, 0x1a), 1.0));
    p.drawEllipse(centre, hub, hub * kDiscTilt);

    // witness marks so the drum itself reads as turning, not just the blades
    for (int i = 0; i < 6; ++i) {
        const double a = m_phase * 1.6 + i * M_PI / 3.0;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x0b, 0x10, 0x1a));
        p.drawEllipse(proj(a, hub * 0.74), 1.6, 1.6);
        p.setBrush(QColor(0x67, 0xe8, 0xf9));
        p.drawEllipse(proj(a + 0.22, hub * 0.74), 1.1, 1.1);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x0b, 0x10, 0x1a));
    p.drawEllipse(centre, hub * 0.22, hub * 0.22 * kDiscTilt);

    for (double a : nearSide)
        drawBlade(a);

    // --- motion smear and the front arc of the casing ------------------------
    if (spin > 0.05) {
        QColor blur = tripped ? theme::danger() : QColor(0xcb, 0xd5, 0xe1);
        blur.setAlphaF(0.04 + 0.12 * spin);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(blur, radius * 0.12));
        p.drawEllipse(QPointF(centre.x(), centre.y() - depth * 0.5),
                      radius * 0.86, radius * 0.86 * kDiscTilt);
    }

    const QRectF casing(centre.x() - radius * 1.28, centre.y() - radius * 1.28 * kDiscTilt,
                        radius * 2.56, radius * 2.56 * kDiscTilt);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x1e, 0x29, 0x3b), radius * 0.12, Qt::SolidLine, Qt::RoundCap));
    p.drawArc(casing, 200 * 16, 140 * 16);
}

/*! The generator barrel: a cylinder with cooling fins, an end cap whose rotor
 *  marks turn with the shaft, and an excitation glow that follows the export. */
void TurbineView::paintGenerator(QPainter &p, double left, double top, double right, double bottom) const
{
    const rbmk::Telemetry t = m_core ? m_core->telemetry() : rbmk::Telemetry();
    const bool tripped = t.turbineTripped;

    const QRectF body(left, top, qMax(24.0, right - left), qMax(30.0, bottom - top));

    QLinearGradient barrel(body.topLeft(), body.bottomLeft());
    barrel.setColorAt(0.0, QColor(0x33, 0x41, 0x55));
    barrel.setColorAt(0.35, QColor(0x1e, 0x29, 0x3b));
    barrel.setColorAt(1.0, QColor(0x0b, 0x10, 0x1a));
    p.setBrush(barrel);
    p.setPen(QPen(QColor(0x47, 0x55, 0x69), 1.1));
    p.drawRoundedRect(body, 6, 6);

    // cooling fins
    p.setPen(QPen(QColor(0x14, 0x1d, 0x2b), 1.4));
    for (int i = 1; i < 8; ++i) {
        const double x = body.left() + body.width() * i / 8.0;
        p.drawLine(QPointF(x, body.top() + 4), QPointF(x, body.bottom() - 4));
    }

    // excitation glow, proportional to the electrical export
    const double glow = tripped ? 0.0 : qBound(0.0, t.turbineMW / 320.0, 1.0);
    if (glow > 0.02) {
        QRadialGradient halo(body.center(), body.width() * 0.55);
        halo.setColorAt(0.0, QColor(56, 189, 248, int(40 + 110 * glow)));
        halo.setColorAt(1.0, QColor(56, 189, 248, 0));
        p.setBrush(halo);
        p.setPen(Qt::NoPen);
        p.drawEllipse(body.center(), body.width() * 0.52, body.height() * 0.72);
    }

    // end cap with a turning rotor witness mark
    const QPointF cap(body.right() - body.width() * 0.16, body.center().y());
    const double capR = qMax(6.0, qMin(body.height() * 0.34, body.width() * 0.14));
    QRadialGradient capGrad(cap - QPointF(capR * 0.3, capR * 0.35), capR * 1.6);
    capGrad.setColorAt(0.0, QColor(0x94, 0xa3, 0xb8));
    capGrad.setColorAt(0.6, QColor(0x33, 0x41, 0x55));
    capGrad.setColorAt(1.0, QColor(0x0b, 0x10, 0x1a));
    p.setBrush(capGrad);
    p.setPen(QPen(QColor(0x0b, 0x10, 0x1a), 1.0));
    p.drawEllipse(cap, capR, capR * 0.9);

    QColor mark = tripped ? theme::danger() : theme::accent();
    p.setPen(QPen(mark, 1.6, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < 3; ++i) {
        const double a = m_phase * 1.4 + i * 2.0 * M_PI / 3.0;
        p.drawLine(cap + QPointF(std::cos(a) * capR * 0.20, std::sin(a) * capR * 0.18),
                   cap + QPointF(std::cos(a) * capR * 0.70, std::sin(a) * capR * 0.62));
    }

    // nameplate
    QFont mono(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSizeF(7.5);
    mono.setBold(true);
    p.setFont(mono);
    p.setPen(QColor(0x94, 0xa3, 0xb8));
    p.drawText(QRectF(body.left() + 6, body.top() + 2, body.width() * 0.55, 16),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("TG-1"));
    p.setPen(QColor(0x64, 0x74, 0x8b));
    p.drawText(QRectF(body.left() + 6, body.bottom() - 18, body.width() * 0.68, 16),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("GENERATOR"));
}
