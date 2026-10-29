#include "tabs/personal_working_copy_dialog.h"
#include "projects/dvmdb_identity.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

PersonalWorkingCopyDialog::PersonalWorkingCopyDialog(
        const QString &project_id, const QString &branch_id,
        const QString &branch_name, const QString &head,
        const QString &source_repository_id, QWidget *parent)
    : QDialog(parent), target(resolvePersonalRepositoryTarget()), client(this),
      project_id(project_id), branch_id(branch_id), branch_name(branch_name),
      head(head), source_repository_id(source_repository_id)
{
    setObjectName(QStringLiteral("projectsWorkingCopiesDialog"));
    setWindowTitle(tr("Personal working copies"));
    resize(580, 440);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *context = new QLabel(tr("Project: %1\nBranch: %2\nPinned base: %3")
        .arg(this->project_id, this->branch_name,
             this->head.isEmpty() ? tr("No commits yet") : this->head), this);
    context->setWordWrap(true);
    context->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(context);
    if (this->target.kind == PersonalRepositoryTarget::Kind::Http
        && this->source_repository_id != this->target.repository_id)
    {
        QLabel *different = new QLabel(tr("Selected database and personal DVMDB are different repositories. "
            "Draft creation requires an already checked-out project and matching branch "
            "in the personal repository; this operation does not import or clone it."), this);
        different->setWordWrap(true);
        layout->addWidget(different);
    }
    this->status = new QLabel(this);
    this->status->setObjectName(QStringLiteral("projectsWorkingCopiesStatus"));
    this->status->setWordWrap(true);
    layout->addWidget(this->status);
    this->drafts = new QListWidget(this);
    this->drafts->setObjectName(QStringLiteral("projectsWorkingCopiesList"));
    layout->addWidget(this->drafts, 1);
    this->details = new QLabel(this);
    this->details->setObjectName(QStringLiteral("projectsWorkingCopyDetails"));
    this->details->setWordWrap(true);
    this->details->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(this->details);
    QLabel *scope = new QLabel(tr("VC-04 draft metadata only: model editor Save and Commit locally "
                                 "are not yet connected. A project and branch must already exist "
                                 "in the personal DVMDB; shared projects are not cloned implicitly."), this);
    scope->setWordWrap(true);
    layout->addWidget(scope);
    QHBoxLayout *actions = new QHBoxLayout();
    this->refresh_button = new QPushButton(tr("Refresh drafts"), this);
    this->refresh_button->setObjectName(QStringLiteral("projectsWorkingCopiesRefresh"));
    this->create_button = new QPushButton(tr("Create draft at HEAD"), this);
    this->create_button->setObjectName(QStringLiteral("projectsWorkingCopiesCreate"));
    this->reopen_button = new QPushButton(tr("Reopen selected draft"), this);
    this->reopen_button->setObjectName(QStringLiteral("projectsWorkingCopiesReopen"));
    actions->addWidget(this->refresh_button);
    actions->addWidget(this->create_button);
    actions->addWidget(this->reopen_button);
    layout->addLayout(actions);
    QDialogButtonBox *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);
    connect(this->refresh_button, &QPushButton::clicked, this,
            [this] { refresh(); });
    connect(this->create_button, &QPushButton::clicked, this,
            [this] { createDraft(); });
    connect(this->reopen_button, &QPushButton::clicked, this,
            [this] { inspectDraft(); });
    connect(this->drafts, &QListWidget::currentRowChanged, this,
            [this](int) { this->reopen_button->setEnabled(!this->busy && this->drafts->currentItem()); });
    setBusy(false);
    if (this->target.kind != PersonalRepositoryTarget::Kind::Http)
    {
        this->status->setText(this->target.kind == PersonalRepositoryTarget::Kind::Embedded
            ? tr("Embedded personal DVMDB draft operations are not connected to this GUI yet.")
            : tr("Configure a personal DVMDB in Settings → Personal Repository first."));
        this->create_button->setEnabled(false);
        this->refresh_button->setEnabled(false);
    }
    else
    {
        refresh();
    }
}

void PersonalWorkingCopyDialog::setBusy(bool busy)
{
    this->busy = busy;
    this->refresh_button->setEnabled(!busy);
    this->create_button->setEnabled(!busy);
    this->reopen_button->setEnabled(!busy && this->drafts->currentItem() != nullptr);
}

void PersonalWorkingCopyDialog::refresh()
{
    setBusy(true);
    this->status->setText(tr("Reading personal DVMDB drafts…"));
    this->client.list(this->target, [this](const QList<PersonalWorkingCopyInfo> &items,
                                           const QString &error)
    {
        setBusy(false);
        if (!error.isEmpty()) { this->status->setText(error); return; }
        this->drafts->clear();
        for (const PersonalWorkingCopyInfo &draft : items)
        {
            if (draft.project_id != this->project_id || draft.branch_id != this->branch_id)
                continue;
            QListWidgetItem *item = new QListWidgetItem(
                tr("%1 · generation %2%3").arg(draft.id, draft.generation,
                    draft.dirty ? tr(" · modified") : QString()), this->drafts);
            item->setData(Qt::UserRole, draft.id);
        }
        this->status->setText(tr("%1 saved draft(s) for this branch in your personal DVMDB.")
                              .arg(this->drafts->count()));
        if (!this->drafts->count())
            this->details->setText(tr("No private draft for this branch. "
                "Create one only after this project and branch exist in the personal DVMDB."));
        this->reopen_button->setEnabled(false);
    });
}

void PersonalWorkingCopyDialog::createDraft()
{
    setBusy(true);
    this->status->setText(tr("Creating durable private draft…"));
    this->client.create(this->target, this->project_id, this->branch_name, this->head,
                        [this](const PersonalWorkingCopyInfo &draft, const QString &error)
    {
        setBusy(false);
        if (!error.isEmpty())
        {
            this->status->setText(tr("Draft creation failed: %1. "
                "The project/branch must already exist in your personal DVMDB "
                "with exactly the selected HEAD. If the request timed out, refresh "
                "the draft list before retrying: the server may have completed it.").arg(error));
            return;
        }
        if (draft.project_id != this->project_id || draft.branch_id != this->branch_id
            || draft.base_revision != this->head)
        {
            this->status->setText(tr("Personal DVMDB created a draft for a different project, "
                                     "branch or base. Do not edit it; refresh the repository."));
            return;
        }
        showDraft(draft);
        refresh();
    });
}

void PersonalWorkingCopyDialog::inspectDraft()
{
    if (this->drafts->currentItem() == nullptr) return;
    const QString id = this->drafts->currentItem()->data(Qt::UserRole).toString();
    if (!DvmdbIdentity::isUuidV7(id)) return;
    setBusy(true);
    this->status->setText(tr("Reopening private draft…"));
    this->client.reopen(this->target, id,
                        [this](const PersonalWorkingCopyInfo &draft, const QString &error)
    {
        setBusy(false);
        if (!error.isEmpty()) { this->status->setText(error); return; }
        if (draft.project_id != this->project_id || draft.branch_id != this->branch_id)
        {
            this->status->setText(tr("Reopened draft belongs to a different project or branch."));
            return;
        }
        showDraft(draft);
    });
}

void PersonalWorkingCopyDialog::showDraft(const PersonalWorkingCopyInfo &draft)
{
    this->status->setText(tr("Personal draft record loaded successfully."));
    this->details->setText(tr("Draft UUIDv7: %1\nSession: %2\nBase revision: %3\n"
                              "Generation: %4\nSaved changes: %5")
        .arg(draft.id, draft.session_id,
             draft.base_revision.isEmpty() ? tr("Unborn") : draft.base_revision,
             draft.generation, draft.dirty ? tr("Yes") : tr("None")));
}
