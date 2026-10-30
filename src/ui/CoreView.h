#pragma once

#include "core/ReactorTypes.h"

#include <QHash>
#include <QSet>
#include <QVector>
#include <QWidget>

namespace rbmk {
class ReactorCore;
}

/*! Painted top-down view of the IZ-250 shield and every core socket.
 *
 *  Interaction:
 *   - left click selects/inspects a channel
 *   - Ctrl + left click adds/removes a socket from a multi-selection
 *   - Shift + left click selects the rectangle between the anchor and that socket
 *   - drag a control rod up/down to withdraw/insert that drive; if the drive is
 *     part of a multi-selection, every selected drive moves by the same amount
 *   - mouse wheel over a control rod trims it (or the whole selection),
 *     elsewhere it zooms
 *   - Ctrl + wheel always zooms, double click on empty space resets the view
 *   - right click opens the insert/extract menu for that socket
 *   - arrow keys move the selection, +/- trim the selected rods, Esc clears
 */
class CoreView : public QWidget
{
    Q_OBJECT
public:
    explicit CoreView(rbmk::ReactorCore *core, QWidget *parent = nullptr);

    int selectedIndex() const { return m_selected; }
    void setSelectedIndex(int index);
    void resetZoom();

    /*! Widget position of a channel centre (used by tooltips and UI smoke tests). */
    QPointF channelCentre(int index) const;

    /*! Every selected socket, primary first in sort order (ascending index). */
    QVector<int> selection() const;
    int selectionCount() const;
    bool isSelected(int index) const;
    void clearMultiSelection();
    void selectAllControlRods();
    void toggleSelection(int index);
    void extendSelectionTo(int index);
    double zoom() const { return m_zoom; }

    void setShowCoordinates(bool show);
    bool showCoordinates() const { return m_showCoordinates; }

public slots:
    void zoomIn() { changeZoom(1.25); }
    void zoomOut() { changeZoom(1.0 / 1.25); }

signals:
    void channelSelected(int index);
    void selectionChanged();
    void contextMenuRequested(int index, const QPoint &globalPos);
    void rodInsertionRequested(int index, double pct);
    void zoomChanged(double zoom);
    void statusMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    double fitScale() const;
    double socketSize() const;
    QPointF layoutToWidget(double x, double y) const;
    QPointF widgetToLayout(const QPointF &pos) const;
    int hitTest(const QPointF &widgetPos) const;
    void drawShield(QPainter &p) const;
    void drawCoreMarker(QPainter &p) const;
    void drawCoordinates(QPainter &p) const;
    void drawChannel(QPainter &p, const rbmk::Channel &ch) const;
    void drawHud(QPainter &p) const;
    void drawTooltip(QPainter &p, const rbmk::Channel &ch) const;
    void changeZoom(double factor);
    void notifySelectionChanged();
    QVector<int> selectedRodChannels() const;

    rbmk::ReactorCore *m_core = nullptr;
    int m_selected = -1;
    QSet<int> m_multi;
    int m_hover = -1;
    double m_zoom = 1.0;
    bool m_showCoordinates = false;

    bool m_dragging = false;
    int m_dragIndex = -1;
    double m_dragStartY = 0.0;
    double m_dragStartValue = 0.0;
    QHash<int, double> m_dragStarts;
};
