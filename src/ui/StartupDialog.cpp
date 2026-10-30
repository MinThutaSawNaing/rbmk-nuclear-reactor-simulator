#include "ui/StartupDialog.h"
#include "ui/AppIcon.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

StartupDialog::StartupDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New reactor session"));
    setWindowIcon(appicon::windowIcon());
    setMinimumWidth(480);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(18);
    auto *title = new QLabel(QStringLiteral("CONFIGURE YOUR CONTROL ROOM"));
    title->setStyleSheet(QStringLiteral("font-size:16px; font-weight:bold; color:#f1f5f9;"));
    layout->addWidget(title);
    auto *intro = new QLabel(QStringLiteral("Choose a reactor and its initial operating state."));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout;
    form->setSpacing(14);
    m_model = new QComboBox;
    m_model->setObjectName(QStringLiteral("StartupReactor"));
    m_model->addItem(QStringLiteral("RBMK-1000"), int(rbmk::ReactorModel::Rbmk1000));
    m_model->addItem(QStringLiteral("RBMKP-2400 — scaled approximation"),
                     int(rbmk::ReactorModel::Rbmkp2400));
    m_model->setMinimumHeight(36);
    form->addRow(QStringLiteral("Reactor"), m_model);
    m_preset = new QComboBox;
    m_preset->setObjectName(QStringLiteral("StartupState"));
    m_preset->addItem(QStringLiteral("Hot — operating at nominal power"),
                      int(rbmk::CorePreset::NominalHot));
    m_preset->addItem(QStringLiteral("Cold — shutdown, turbine stopped"),
                      int(rbmk::CorePreset::ColdShutdown));
    m_preset->setMinimumHeight(36);
    form->addRow(QStringLiteral("Starting state"), m_preset);
    layout->addLayout(form);

    auto *rating = new QLabel;
    rating->setWordWrap(true);
    rating->setMinimumHeight(52);
    rating->setStyleSheet(QStringLiteral("color:#94a3b8;"));
    const auto updateRating = [this, rating] {
        rating->setText(model() == rbmk::ReactorModel::Rbmkp2400
            ? QStringLiteral("Large illustrative 24 × 24 core · nominal 7,500 MWt / 2,400 MWe. "
                             "Scaled educational model, not a validated plant design.")
            : QStringLiteral("Standard 18 × 18 core · nominal 1,000 MWt / 320 MWe "
                             "in this educational simulator."));
    };
    connect(m_model, &QComboBox::currentIndexChanged, this, updateRating);
    updateRating();
    layout->addWidget(rating);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    auto *start = buttons->addButton(QStringLiteral("Start simulation"), QDialogButtonBox::AcceptRole);
    start->setDefault(true);
    start->setMinimumHeight(36);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    m_model->setFocus();
}

rbmk::ReactorModel StartupDialog::model() const
{
    return rbmk::ReactorModel(m_model->currentData().toInt());
}

rbmk::CorePreset StartupDialog::preset() const
{
    return rbmk::CorePreset(m_preset->currentData().toInt());
}