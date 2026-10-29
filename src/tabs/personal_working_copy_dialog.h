#pragma once

#include "config/personal_repository_storage.h"
#include "projects/personal_working_copy_client.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;

// First VC-04 Projects-tab surface: create/reopen a durable private draft.
// Editing and Commit are intentionally not presented as available yet.
class PersonalWorkingCopyDialog final : public QDialog
{
public:
    PersonalWorkingCopyDialog(const QString &project_id, const QString &branch_id,
                              const QString &branch_name, const QString &head,
                              const QString &source_repository_id, QWidget *parent = nullptr);

private:
    void refresh();
    void createDraft();
    void inspectDraft();
    void setBusy(bool busy);
    void showDraft(const PersonalWorkingCopyInfo &draft);

    PersonalRepositoryTarget target;
    PersonalWorkingCopyClient client;
    QString project_id;
    QString branch_id;
    QString branch_name;
    QString head;
    QString source_repository_id;
    QLabel *status = nullptr;
    QLabel *details = nullptr;
    QListWidget *drafts = nullptr;
    QPushButton *refresh_button = nullptr;
    QPushButton *create_button = nullptr;
    QPushButton *reopen_button = nullptr;
    bool busy = false;
};
