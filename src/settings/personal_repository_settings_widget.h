#pragma once

#include "services/personal_repository_monitor.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

// Explicit, per-user private DVMDB configuration. Source database selection
// and project navigation do not modify this setting.
class PersonalRepositorySettingsWidget final : public QWidget
{
public:
    explicit PersonalRepositorySettingsWidget(QWidget *parent = nullptr);
    PersonalRepositoryMonitor::State verificationState() const;

private:
    void restore();
    void save();
    void clear();
    void verify();
    void updateStatus(PersonalRepositoryMonitor::State state, const QString &detail);

    QLineEdit *endpoint = nullptr;
    QLineEdit *repository_id = nullptr;
    QLabel *status = nullptr;
    QPushButton *save_button = nullptr;
    PersonalRepositoryMonitor monitor;
};
