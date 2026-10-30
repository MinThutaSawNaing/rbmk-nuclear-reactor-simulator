#pragma once

#include "core/ReactorCore.h"
#include <QDialog>

class QComboBox;

class StartupDialog : public QDialog
{
public:
    explicit StartupDialog(QWidget *parent = nullptr);
    rbmk::ReactorModel model() const;
    rbmk::CorePreset preset() const;

private:
    QComboBox *m_model = nullptr;
    QComboBox *m_preset = nullptr;
};