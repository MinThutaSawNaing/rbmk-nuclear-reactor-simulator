#pragma once

#include "core/ReactorTypes.h"

#include <QWidget>

class QTimer;

namespace rbmk {
class ReactorCore;
}

/*! Animated 3-D view of the TG-1 turbo-generator train.
 *
 *  The rotor is drawn as an inclined bladed disc with real depth ordering: the
 *  blades on the far side of the wheel are painted first, then the drum, then
 *  the blades on the near side, with a foreshortened axis, a cylinder wall, a
 *  stator casing and a generator barrel behind it. Blade phase, steam admission
 *  dashes and the coupling glow are all driven by ReactorCore telemetry, so the
 *  machine spins up, coasts down and trips with the plant.
 *
 *  This is a painted pseudo-3-D projection (no Qt3D / OpenGL dependency).
 */
class TurbineView : public QWidget
{
    Q_OBJECT
public:
    //! Operator-facing state of the machine. The painted lamp and the card text
    //! both come from here, so the two can never disagree about the train.
    enum class State { Tripped, OnGrid, GovernorShut, RollingUp };

    explicit TurbineView(rbmk::ReactorCore *core, QWidget *parent = nullptr);

    static State stateOf(const rbmk::Telemetry &t);
    static QString stateLabel(State state);

    QSize sizeHint() const override { return QSize(340, 168); }
    QSize minimumSizeHint() const override { return QSize(240, 124); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintWheel(QPainter &p, const QPointF &centre, double radius) const;
    void paintGenerator(QPainter &p, double left, double top, double right, double bottom) const;

    rbmk::ReactorCore *m_core = nullptr;
    QTimer *m_timer = nullptr;
    double m_phase = 0.0;        //!< rotor angle, radians
    double m_steamPhase = 0.0;   //!< dash offset of the steam pulses
    qint64 m_lastMs = 0;
};
