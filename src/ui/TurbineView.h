#pragma once

#include "core/ReactorTypes.h"

#include <QWidget>
#include <QElapsedTimer>

class QTimer;

namespace rbmk {
class ReactorCore;
}

/*! Multi-stage turbine cutaway with a common shaft and shaded generator.
 *  Painted depth projection, not a CAD model. Rotation is visually slowed to
 *  avoid sampling a 3000 rpm rotor at display frame rates; speed, steam and
 *  coast-down follow telemetry, and animation stops while simulation is paused.
 */
class TurbineView : public QWidget
{
    Q_OBJECT
public:
    //! Operator-facing state of the machine. The painted lamp and the card text
    //! both come from here, so the two can never disagree about the train.
    enum class State { Tripped, OnGrid, GovernorShut, WaitingForSteam, Running, RollingUp };

    explicit TurbineView(rbmk::ReactorCore *core, QWidget *parent = nullptr);

    static State stateOf(const rbmk::Telemetry &t);
    static QString stateLabel(State state);

    QSize sizeHint() const override { return QSize(340, 168); }
    QSize minimumSizeHint() const override { return QSize(240, 124); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void paintStage(QPainter &p, double x, double radius, double spin) const;
    void paintGenerator(QPainter &p, double left, double top, double right, double bottom) const;

    rbmk::ReactorCore *m_core = nullptr;
    QTimer *m_timer = nullptr;
    double m_phase = 0.0;        //!< rotor angle, radians
    double m_steamPhase = 0.0;   //!< dash offset of the steam pulses
    QElapsedTimer m_clock;
};
