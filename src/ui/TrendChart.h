#pragma once

#include <QVector>
#include <QWidget>

/*! Rolling strip-chart of thermal power, fuel temperature and xenon load. */
class TrendChart : public QWidget
{
    Q_OBJECT
public:
    explicit TrendChart(QWidget *parent = nullptr);

    void addSample(double simSeconds, double powerMWt, double fuelTempC, double xenon);
    void clear();

    QSize sizeHint() const override { return QSize(420, 156); }
    QSize minimumSizeHint() const override { return QSize(260, 128); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    struct Sample
    {
        double t = 0.0;
        double power = 0.0;
        double fuel = 0.0;
        double xenon = 0.0;
    };

    QVector<Sample> m_samples;
    static constexpr int kMaxSamples = 480; // 0.25 s cadence -> 2 minutes
};
