#pragma once

#include <QColor>
#include <QWidget>

/*! A single telemetry read-out: title, big value, unit and a level bar. */
class GaugeCard : public QWidget
{
    Q_OBJECT
public:
    explicit GaugeCard(const QString &title, const QString &unit, QWidget *parent = nullptr);

    void setReading(const QString &valueText,
                    double fraction,
                    const QColor &color,
                    const QString &subText = QString());
    void setUnit(const QString &unit) { m_unit = unit; update(); }

    QSize sizeHint() const override { return QSize(168, 70); }
    QSize minimumSizeHint() const override { return QSize(140, 66); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    QString m_unit;
    QString m_value = QStringLiteral("--");
    QString m_sub;
    double m_fraction = 0.0;
    QColor m_color;
};
