#include "ui/GaugeCard.h"

#include "ui/Theme.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

GaugeCard::GaugeCard(const QString &title, const QString &unit, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_unit(unit)
    , m_color(theme::accent())
{
    setMinimumHeight(66);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void GaugeCard::setReading(const QString &valueText,
                           double fraction,
                           const QColor &color,
                           const QString &subText)
{
    m_value = valueText;
    m_fraction = qBound(0.0, fraction, 1.0);
    m_color = color;
    m_sub = subText;
    update();
}

void GaugeCard::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF card = rect().adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(card, 8.0, 8.0);
    p.fillPath(path, theme::panelDark());
    p.setPen(QPen(theme::border(), 1.0));
    p.drawPath(path);

    QFont titleFont = font();
    titleFont.setPointSizeF(7.5);
    titleFont.setBold(true);
    titleFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(titleFont);
    p.setPen(theme::dim());
    p.drawText(QRectF(10, 6, width() - 20, 14), Qt::AlignLeft | Qt::AlignVCenter, m_title.toUpper());

    QFont valueFont = font();
    valueFont.setFamily(theme::monoFamily());
    valueFont.setPointSizeF(14.0);
    valueFont.setBold(true);
    p.setFont(valueFont);
    p.setPen(m_color);
    const QRectF valueRect(10, 18, width() - 20, 22);
    p.drawText(valueRect, Qt::AlignLeft | Qt::AlignVCenter, m_value);

    QFont unitFont = font();
    unitFont.setPointSizeF(8.0);
    p.setFont(unitFont);
    p.setPen(theme::dim());
    const int valueWidth = QFontMetrics(valueFont).horizontalAdvance(m_value);
    p.drawText(QRectF(10 + valueWidth + 6, 22, 70, 16),
               Qt::AlignLeft | Qt::AlignVCenter,
               m_unit);

    // level bar
    const QRectF barBg(10, height() - 13, width() - 20, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(theme::border());
    p.drawRoundedRect(barBg, 2.0, 2.0);
    if (m_fraction > 0.0) {
        QRectF fill = barBg;
        fill.setWidth(barBg.width() * m_fraction);
        p.setBrush(m_color);
        p.drawRoundedRect(fill, 2.0, 2.0);
    }

    if (!m_sub.isEmpty()) {
        QFont subFont = font();
        subFont.setPointSizeF(7.5);
        p.setFont(subFont);
        p.setPen(theme::dim());
        p.drawText(QRectF(10, height() - 28, width() - 20, 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   m_sub);
    }
}
