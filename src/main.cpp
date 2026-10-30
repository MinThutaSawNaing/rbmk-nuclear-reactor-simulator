#include "core/ReactorCore.h"
#include "ui/AppIcon.h"
#include "ui/CoreView.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>
#include <QDoubleSpinBox>
#include <QFont>
#include <QIcon>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QTimer>

#include <cmath>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <shobjidl_core.h> // SetCurrentProcessExplicitAppUserModelID
#endif

using namespace rbmk;

namespace {

//! Give Windows its own taskbar identity. Without this the shell groups the
//! process under the host executable's identity, so a pinned shortcut would
//! show the wrong icon and stop grouping windows with it.
void registerTaskbarIdentity()
{
#ifdef Q_OS_WIN
    const wchar_t *id = L"MinThutaSawNaing.RBMKControlRoom.Simulator";
    SetCurrentProcessExplicitAppUserModelID(id);
#endif
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("RBMK Control Room"));
    QApplication::setApplicationVersion(QStringLiteral("1.1.0"));
    QApplication::setOrganizationName(QStringLiteral("RBMK Simulation"));

    registerTaskbarIdentity();

    app.setStyle(QStringLiteral("Fusion"));
    QFont font(theme::monoFamily());
    font.setPointSizeF(9.0);
    font.setStyleHint(QFont::TypeWriter);
    app.setFont(font);
    app.setStyleSheet(theme::styleSheet());
    app.setWindowIcon(appicon::windowIcon());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "RBMK-1000 core physics sandbox. Every channel can be inserted or removed, "
        "every control rod drive can be moved on its own."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption selftestOption(
        QStringLiteral("selftest"),
        QStringLiteral("Run an automated UI smoke test, then exit."));
    QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a window screenshot to <path> (implies --selftest)."),
        QStringLiteral("path"));
    QCommandLineOption presetOption(
        QStringLiteral("preset"),
        QStringLiteral("Initial core state: nominal (default), low or cold."),
        QStringLiteral("state"), QStringLiteral("nominal"));
    QCommandLineOption demoOption(
        QStringLiteral("demo"),
        QStringLiteral("Run a demonstration scenario instead of a plain session. "
                       "Supported: meltdown (pull every rod out)."),
        QStringLiteral("name"));
    QCommandLineOption holdOption(
        QStringLiteral("hold"),
        QStringLiteral("Start with the automatic power hold engaged at <mwt> megawatts "
                       "thermal."),
        QStringLiteral("mwt"));
    parser.addOption(selftestOption);
    parser.addOption(screenshotOption);
    parser.addOption(presetOption);
    parser.addOption(demoOption);
    parser.addOption(holdOption);
    parser.process(app);

    CorePreset preset = CorePreset::NominalHot;
    const QString presetArg = parser.value(presetOption).toLower();
    if (presetArg.startsWith(QStringLiteral("low")))
        preset = CorePreset::LowPower;
    else if (presetArg.startsWith(QStringLiteral("cold")))
        preset = CorePreset::ColdShutdown;

    const QString demo = parser.value(demoOption).toLower();

    MainWindow window;
    window.show();
    window.startSimulation(preset);

    if (parser.isSet(holdOption)) {
        bool ok = false;
        const double mwt = parser.value(holdOption).toDouble(&ok);
        if (ok && mwt > 0.0)
            window.core()->setPowerTarget(mwt);
    }
    const bool holdRequested = parser.isSet(holdOption);

    const bool automated = parser.isSet(selftestOption) || parser.isSet(screenshotOption)
                           || !demo.isEmpty();
    int selftestFailures = 0;
    if (automated) {
        if (parser.isSet(selftestOption)) {
            MainWindow second(nullptr, ReactorModel::Rbmkp2400);
            second.startSimulation();
            if (second.core() == window.core()
                || std::fabs(second.core()->telemetry().powerMWt - 7500.0) > 0.1
                || std::fabs(second.core()->powerTarget() - 7500.0) > 0.1) {
                qWarning("FAIL independent RBMKP-2400 unit");
                ++selftestFailures;
            }
            second.close();
        }
        int shotDelay = 1800;
        if (demo == QStringLiteral("meltdown")) {
            // Educational failure demo: pull every drive out of the core and
            // accelerate time, so the prompt-critical excursion plays out.
            shotDelay = 12000;
            QTimer::singleShot(300, &window, [&window] {
                ReactorCore *core = window.core();
                core->setTimeScale(5.0);
                for (int i = 0; i < core->channelCount(); ++i) {
                    if (core->channel(i).isControlRod())
                        core->extractAssembly(i);
                }
                window.selectChannel(-1);
            });
        } else {
            // Exercise the operator controls so the screenshot shows a live session:
            // build a Ctrl+click style rod group, trim the group, trip one pump.
            QTimer::singleShot(350, &window, [&window, preset, holdRequested, &selftestFailures] {
                ReactorCore *core = window.core();
                CoreView *view = window.coreView();

                // The app must open with the power controller holding a running core,
                // and must leave a cold, shut-down core alone.
                const bool expectHold = holdRequested || preset != CorePreset::ColdShutdown;
                if (core->autoHoldEngaged() != expectHold) {
                    qWarning("FAIL auto hold state at startup");
                    ++selftestFailures;
                }

                QVector<int> rods;
                for (int i = 0; i < core->channelCount(); ++i) {
                    if (core->channel(i).isControlRod())
                        rods.append(i);
                }

                if (!rods.isEmpty()) {
                    // Same code path as Ctrl+click on the core map.
                    const int groupSize = qMin(6, rods.size());
                    view->setSelectedIndex(rods.first());
                    for (int r = 1; r < groupSize; ++r)
                        view->toggleSelection(rods.at(r));

                    qInfo().nospace() << "Multi-selection: " << view->selectionCount()
                                      << " sockets selected (expected " << groupSize << ")";
                    if (view->selectionCount() != groupSize) {
                        qWarning("FAIL multi-selection count");
                        ++selftestFailures;
                    }

                    // Second Ctrl+click through the real mouse event path: verifies the
                    // modifier handling in CoreView::mousePressEvent, not just the slot.
                    for (int i = 0; i < core->channelCount(); ++i) {
                        if (!core->channel(i).isControlRod() || view->isSelected(i))
                            continue;
                        const QPointF pos = view->channelCentre(i);
                        QMouseEvent press(QEvent::MouseButtonPress, pos,
                                          view->mapToGlobal(pos.toPoint()), Qt::LeftButton,
                                          Qt::LeftButton, Qt::ControlModifier);
                        QApplication::sendEvent(view, &press);
                        break;
                    }
                    const int expected = qMin(groupSize + 1, rods.size());
                    if (view->selectionCount() != expected) {
                        qWarning().nospace() << "FAIL ctrl+click selection (" << view->selectionCount()
                                             << " selected, expected " << expected << ")";
                        ++selftestFailures;
                    }

                    // Trim the whole group and confirm every drive detached and moved.
                    const QVector<int> group = view->selection();
                    for (int index : group)
                        core->nudgeRod(index, -3.0);
                    int moved = 0;
                    for (int index : group) {
                        if (!core->channel(index).followBank)
                            ++moved;
                    }
                    qInfo().nospace() << "Group trim: " << moved << " of " << group.size()
                                      << " drives detached and trimmed";
                    if (moved != group.size()) {
                        qWarning("FAIL group trim");
                        ++selftestFailures;
                    }
                }

                // AUTO HOLD / POWER TARGET must drive the core through the real
                // widget signals, not only through the core API.
                auto *holdButton = window.findChild<QPushButton *>(QStringLiteral("AutoHoldButton"));
                auto *targetSpin = window.findChild<QDoubleSpinBox *>();
                if (!holdButton || !targetSpin) {
                    qWarning("FAIL AUTO HOLD controls missing from the header");
                    ++selftestFailures;
                } else if (expectHold) {
                    holdButton->click(); // release
                    if (core->autoHoldEngaged()) {
                        qWarning("FAIL AUTO HOLD did not release on the button");
                        ++selftestFailures;
                    }
                    holdButton->click(); // engage again
                    if (!core->autoHoldEngaged()) {
                        qWarning("FAIL AUTO HOLD did not engage on the button");
                        ++selftestFailures;
                    }

                    targetSpin->setValue(720.0);
                    if (std::fabs(core->powerTarget() - 720.0) > 0.5) {
                        qWarning().nospace() << "FAIL power target not applied ("
                                             << core->powerTarget() << " MWt)";
                        ++selftestFailures;
                    }
                    qInfo().nospace() << "AUTO HOLD: target " << core->powerTarget()
                                      << " MWt, state " << core->telemetry().autoHoldStatus;
                } else if (holdButton->isChecked()) {
                    qWarning("FAIL AUTO HOLD button does not follow the released controller");
                    ++selftestFailures;
                }

                if (preset == CorePreset::NominalHot)
                    core->setPump(3, false);
            });
        }

        QTimer::singleShot(shotDelay, &window, [&window, path = parser.value(screenshotOption),
                                                &selftestFailures] {
            if (!path.isEmpty()) {
                const QPixmap shot = window.grab();
                if (shot.save(path))
                    qInfo() << "Screenshot written to" << path;
                else {
                    qWarning() << "Failed to write screenshot to" << path;
                    ++selftestFailures;
                }
            }
            window.core()->stop();
            QCoreApplication::exit(selftestFailures == 0 ? 0 : 3);
        });
    }

    return app.exec();
}
