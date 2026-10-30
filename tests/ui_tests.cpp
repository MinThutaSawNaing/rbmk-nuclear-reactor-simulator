#include "core/ReactorCore.h"
#include "ui/CoreView.h"
#include "ui/StartupDialog.h"
#include "ui/TurbineView.h"
#include "ui/Theme.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QtTest>

class UiTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qApp->setStyleSheet(theme::styleSheet());
    }

    void startupChoices_data()
    {
        QTest::addColumn<int>("modelIndex");
        QTest::addColumn<int>("stateIndex");
        QTest::newRow("1000-hot") << 0 << 0;
        QTest::newRow("1000-cold") << 0 << 1;
        QTest::newRow("2400-hot") << 1 << 0;
        QTest::newRow("2400-cold") << 1 << 1;
    }

    void startupChoices()
    {
        QFETCH(int, modelIndex);
        QFETCH(int, stateIndex);
        StartupDialog dialog;
        auto *model = dialog.findChild<QComboBox *>(QStringLiteral("StartupReactor"));
        auto *state = dialog.findChild<QComboBox *>(QStringLiteral("StartupState"));
        QVERIFY(model);
        QVERIFY(state);
        QCOMPARE(model->currentIndex(), 0);
        QCOMPARE(state->currentIndex(), 0);
        model->setCurrentIndex(modelIndex);
        state->setCurrentIndex(stateIndex);
        rbmk::ReactorCore core;
        core.setModel(dialog.model());
        core.initialize(dialog.preset());
        core.stop();
        QCOMPARE(core.gridSize(), modelIndex ? 24 : 18);
        QCOMPARE(core.preset(), stateIndex ? rbmk::CorePreset::ColdShutdown
                                          : rbmk::CorePreset::NominalHot);
        QCOMPARE(core.telemetry().turbineRpm, stateIndex ? 0.0 : 3000.0);
        if (!stateIndex)
            QCOMPARE(core.telemetry().powerMWt, modelIndex ? 7500.0 : 1000.0);
        dialog.show();
        QVERIFY(!dialog.grab().isNull());
        if (modelIndex == 1 && stateIndex == 1)
            QVERIFY(dialog.grab().save(QStringLiteral("startup-preview.png")));
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        for (auto *button : buttons->buttons()) {
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) {
                button->click();
                break;
            }
        }
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void startupCancel()
    {
        StartupDialog dialog;
        auto *buttons = dialog.findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        dialog.show();
        buttons->button(QDialogButtonBox::Cancel)->click();
        QCOMPARE(dialog.result(), int(QDialog::Rejected));
        QVERIFY(!dialog.isVisible());
    }

    void turbineAnimationFollowsPlant()
    {
        rbmk::ReactorCore core;
        core.initialize(rbmk::CorePreset::ColdShutdown);
        core.stop();
        TurbineView view(&core);
        view.resize(480, 190);
        view.show();
        QTest::qWait(80);
        const auto cold = view.grab().toImage();
        QTest::qWait(180);
        QCOMPARE(view.grab().toImage(), cold); // no phantom cold rotation

        core.loadPreset(rbmk::CorePreset::NominalHot);
        core.stop();
        const auto hot = view.grab().toImage();
        QVERIFY(hot.save(QStringLiteral("turbine-preview.png")));
        QTest::qWait(180);
        QVERIFY(view.grab().toImage() != hot);
        core.setPaused(true);
        const auto paused = view.grab().toImage();
        QTest::qWait(180);
        QCOMPARE(view.grab().toImage(), paused);
        core.tripTurbine();
        core.advance(30.0);
        QCOMPARE(TurbineView::stateOf(core.telemetry()), TurbineView::State::Tripped);
        QCOMPARE(core.telemetry().turbineMW, 0.0);
        QVERIFY(view.grab().save(QStringLiteral("turbine-tripped-preview.png")));
        view.resize(240, 124);
        QVERIFY(!view.grab().isNull());
    }

    void renderBothCoreSizes()
    {
        for (const auto model : { rbmk::ReactorModel::Rbmk1000, rbmk::ReactorModel::Rbmkp2400 }) {
            rbmk::ReactorCore core;
            core.setModel(model);
            core.initialize();
            core.stop();
            CoreView view(&core);
            view.resize(600, 600);
            view.show();
            QVERIFY(!view.grab().isNull());
            QVERIFY(view.grab().save(model == rbmk::ReactorModel::Rbmk1000
                ? QStringLiteral("core-1000-preview.png") : QStringLiteral("core-2400-preview.png")));
        }
    }
};

QTEST_MAIN(UiTest)
#include "ui_tests.moc"