#pragma once

#include "config/personal_repository_storage.h"
#include "projects/personal_working_copy_client.h"

#include <QDialog>

#include <optional>

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
                              const QString &source_repository_id,
                              const QString &source_endpoint, const QString &project_name,
                              QWidget *parent = nullptr);

    // Available only after a generation-checked canonical network load.
    // The caller may display it, but editing is NOT bound to autosave yet.
    const std::optional<NetworkHydraulic> &selectedPreviewNetwork() const;
    QString selectedPreviewDraftId() const;
    QString selectedPreviewGeneration() const;

private:
    void refresh();
    void createDraft();
    void inspectDraft();
    void checkoutBranch();
    void initializeNetwork();
    void viewDraftNetwork();
    void setBusy(bool busy);
    void showDraft(const PersonalWorkingCopyInfo &draft);

    PersonalRepositoryTarget target;
    PersonalWorkingCopyClient client;
    QString project_id;
    QString branch_id;
    QString branch_name;
    QString head;
    QString source_repository_id;
    QString source_endpoint;
    QString project_name;
    QLabel *status = nullptr;
    QLabel *details = nullptr;
    QListWidget *drafts = nullptr;
    QPushButton *refresh_button = nullptr;
    QPushButton *create_button = nullptr;
    QPushButton *checkout_button = nullptr;
    QPushButton *reopen_button = nullptr;
    QPushButton *initialize_button = nullptr;
    QPushButton *view_button = nullptr;
    std::optional<PersonalWorkingCopyInfo> active_draft;
    std::optional<NetworkHydraulic> preview_network;
    QString preview_generation;
    QString preview_draft_id;
    QString accepted_draft_id;
    QString accepted_generation;
    bool busy = false;
};
