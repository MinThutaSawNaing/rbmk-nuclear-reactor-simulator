#include "ui/TrendChart.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

TrendChart::TrendChart(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(128);
}

void TrendChart::addSample(double simSeconds, double powerMWt, double fuelTempC, double xenon)
{
    Sample s;
    s.t = simSeconds;
    s.power = powerMWt;
    s.fuel = fuelTempC;
    s.xenon = xenon;
    m_samples.push_back(s);
    while (m_samples.size() > kMaxSamples)
        m_samples.removeFirst();
    update();
}

void TrendChart::clear()
{
    m_samples.clear();
    update();
}

void TrendChart::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF card = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath cardPath;
    cardPath.addRoundedRect(card, 8.0, 8.0);
    p.fillPath(cardPath, theme::panelDark());
    p.setPen(QPen(theme::border(), 1.0));
    p.drawPath(cardPath);

    const QRectF plot = card.adjusted(42, 26, -10, -20);

    QFont small = font();
    small.setPointSizeF(7.5);
    p.setFont(small);

    // legend
    const struct
    {
        QColor color;
        QString label;
        double value;
        QString unit;
    } legend[3] = {
        { theme::accent(), QStringLiteral("POWER"), m_samples.isEmpty() ? 0.0 : m_samples.last().power, QStringLiteral("MWt") },
        { theme::warning(), QStringLiteral("FUEL"), m_samples.isEmpty() ? 0.0 : m_samples.last().fuel, QStringLiteral("degC") },
        { theme::purple(), QStringLiteral("XENON"), m_samples.isEmpty() ? 0.0 : m_samples.last().xenon, QStringLiteral("rel") },
    };

    double x = card.left() + 12;
    for (const auto &item : legend) {
        p.setPen(Qt::NoPen);
        p.setBrush(item.color);
        p.drawRoundedRect(QRectF(x, 9, 7, 7), 2, 2);
        p.setPen(theme::dim());
        const QString text = QStringLiteral("%1 %2 %3")
                                 .arg(item.label)
                                 .arg(item.value, 0, 'f', item.label == QStringLiteral("XENON") ? 2 : 0)
                                 .arg(item.unit);
        p.drawText(QPointF(x + 11, 15), text);
        x += QFontMetrics(small).horizontalAdvance(text) + 24;
    }

    // grid
    p.setPen(QPen(theme::border(), 1.0, Qt::DotLine));
    for (int i = 0; i <= 4; ++i) {
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(theme::dim());
        p.drawText(QRectF(card.left() + 4, y - 8, 36, 16),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(3200 - i * 800));
        p.setPen(QPen(theme::border(), 1.0, Qt::DotLine));
    }

    if (m_samples.size() < 2) {
        p.setPen(theme::dim());
        p.drawText(plot, Qt::AlignCenter, QStringLiteral("collecting trend data..."));
        return;
    }

    const double tEnd = m_samples.last().t;
    const double tStart = m_samples.first().t;
    const double span = qMax(1.0, tEnd - tStart);

    auto mapPoint = [&](double t, double value, double maxValue) {
        const double x = plot.left() + (t - tStart) / span * plot.width();
        const double y = plot.bottom() - qBound(0.0, value / maxValue, 1.0) * plot.height();
        return QPointF(x, y);
    };

    auto drawSeries = [&](double maxValue, QColor color, double width, auto valueOf) {
        QPainterPath path;
        bool first = true;
        for (const Sample &s : m_samples) {
            const QPointF pt = mapPoint(s.t, valueOf(s), maxValue);
            if (first) {
                path.moveTo(pt);
                first = false;
            } else {
                path.lineTo(pt);
            }
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(color, width));
        p.drawPath(path);
    };

    drawSeries(3200.0, theme::accent(), 1.8, [](const Sample &s) { return s.power; });
    drawSeries(1400.0, theme::warning(), 1.4, [](const Sample &s) { return s.fuel; });
    drawSeries(2.0, theme::purple(), 1.2, [](const Sample &s) { return s.xenon; });

    p.setPen(theme::dim());
    p.drawText(QRectF(plot.left(), plot.bottom() + 3, 80, 14),
               Qt::AlignLeft,
               QStringLiteral("-2 min"));
    p.drawText(QRectF(plot.right() - 60, plot.bottom() + 3, 60, 14),
               Qt::AlignRight,
               QStringLiteral("now"));
}
