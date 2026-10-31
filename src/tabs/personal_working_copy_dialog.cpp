#include "tabs/personal_working_copy_dialog.h"
#include "projects/dvmdb_identity.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QInputDialog>
#include <QSignalBlocker>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

PersonalWorkingCopyDialog::PersonalWorkingCopyDialog(
        const QString &project_id, const QString &branch_id,
        const QString &branch_name, const QString &head,
        const QString &source_repository_id, const QString &source_endpoint,
        const QString &project_name, QWidget *parent)
    : QDialog(parent), target(resolvePersonalRepositoryTarget()), client(this),
      project_id(project_id), branch_id(branch_id), branch_name(branch_name),
      head(head), source_repository_id(source_repository_id),
      source_endpoint(source_endpoint), project_name(project_name)
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
            "Checkout the selected branch before creating its private draft."), this);
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
    QLabel *scope = new QLabel(tr("Checkout and first-network initialization change only your personal DVMDB "
                                 "draft, never the committed branch. Verified networks can be viewed "
                                 "in Distribution Monitor. Editor Save and local Commit are not yet connected; "
                                 "changes made in other editing views are NOT autosaved to this draft."), this);
    scope->setWordWrap(true);
    layout->addWidget(scope);
    QHBoxLayout *actions = new QHBoxLayout();
    this->refresh_button = new QPushButton(tr("Refresh drafts"), this);
    this->refresh_button->setObjectName(QStringLiteral("projectsWorkingCopiesRefresh"));
    this->checkout_button = new QPushButton(tr("Checkout branch privately"), this);
    this->checkout_button->setObjectName(QStringLiteral("projectsWorkingCopiesCheckout"));
    this->create_button = new QPushButton(tr("Create draft at HEAD"), this);
    this->create_button->setObjectName(QStringLiteral("projectsWorkingCopiesCreate"));
    this->reopen_button = new QPushButton(tr("Reopen selected draft"), this);
    this->reopen_button->setObjectName(QStringLiteral("projectsWorkingCopiesReopen"));
    this->initialize_button = new QPushButton(tr("Initialize empty network"), this);
    this->initialize_button->setObjectName(QStringLiteral("projectsWorkingCopiesInitializeNetwork"));
    this->view_button = new QPushButton(tr("View in Distribution Monitor"), this);
    this->view_button->setObjectName(QStringLiteral("projectsWorkingCopiesViewNetwork"));
    actions->addWidget(this->checkout_button);
    actions->addWidget(this->refresh_button);
    actions->addWidget(this->create_button);
    actions->addWidget(this->reopen_button);
    layout->addLayout(actions);
    QHBoxLayout *network_actions = new QHBoxLayout();
    network_actions->addWidget(this->initialize_button);
    network_actions->addWidget(this->view_button);
    layout->addLayout(network_actions);
    QDialogButtonBox *close = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(close);
    connect(this->refresh_button, &QPushButton::clicked, this,
            [this] { refresh(); });
    connect(this->checkout_button, &QPushButton::clicked, this,
            [this] { checkoutBranch(); });
    connect(this->create_button, &QPushButton::clicked, this,
            [this] { createDraft(); });
    connect(this->reopen_button, &QPushButton::clicked, this,
            [this] { inspectDraft(); });
    connect(this->initialize_button, &QPushButton::clicked, this,
            [this] { initializeNetwork(); });
    connect(this->view_button, &QPushButton::clicked, this,
            [this] { viewDraftNetwork(); });
    connect(this->drafts, &QListWidget::currentRowChanged, this, [this](int)
    {
        // A previously inspected network must never be opened after switching
        // to another draft, even if the asynchronous response arrives later.
        this->active_draft.reset();
        this->preview_network.reset();
        setBusy(this->busy);
    });
    setBusy(false);
    if (this->target.kind != PersonalRepositoryTarget::Kind::Http)
    {
        this->status->setText(this->target.kind == PersonalRepositoryTarget::Kind::Embedded
            ? tr("Embedded personal DVMDB draft operations are not connected to this GUI yet.")
            : tr("Configure a personal DVMDB in Settings → Personal Repository first."));
        this->create_button->setEnabled(false);
        this->refresh_button->setEnabled(false);
        this->checkout_button->setEnabled(false);
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
    this->checkout_button->setEnabled(!busy &&
        !this->source_endpoint.isEmpty() &&
        this->source_repository_id != this->target.repository_id &&
        this->target.kind == PersonalRepositoryTarget::Kind::Http);
    this->create_button->setEnabled(!busy);
    this->reopen_button->setEnabled(!busy && this->drafts->currentItem() != nullptr);
    this->initialize_button->setEnabled(!busy && this->active_draft.has_value()
        && this->active_draft->base_revision.isEmpty()
        && this->active_draft->changes.isEmpty() && !this->active_draft->dirty
        && this->target.kind == PersonalRepositoryTarget::Kind::Http);
    this->view_button->setEnabled(!busy && this->active_draft.has_value()
        && this->preview_network.has_value()
        && this->preview_draft_id == this->active_draft->id
        && this->preview_generation == this->active_draft->generation);
}

void PersonalWorkingCopyDialog::refresh()
{
    this->active_draft.reset();
    this->preview_network.reset();
    setBusy(true);
    this->status->setText(tr("Reading personal DVMDB drafts…"));
    this->client.list(this->target, [this](const QList<PersonalWorkingCopyInfo> &items,
                                           const QString &error)
    {
        setBusy(false);
        if (!error.isEmpty()) { this->status->setText(error); return; }
        this->active_draft.reset();
        this->preview_network.reset();
        this->preview_draft_id.clear();
        this->preview_generation.clear();
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
        setBusy(false);
    });
}

void PersonalWorkingCopyDialog::createDraft()
{
    this->active_draft.reset();
    this->preview_network.reset();
    setBusy(true);
    this->status->setText(tr("Creating durable private draft…"));
    this->client.create(this->target, this->project_id, this->branch_id, this->head,
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
    });
}

void PersonalWorkingCopyDialog::inspectDraft()
{
    if (this->drafts->currentItem() == nullptr) return;
    this->active_draft.reset();
    this->preview_network.reset();
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
    this->active_draft = draft;
    this->preview_network.reset();
    this->preview_draft_id.clear();
    this->preview_generation.clear();
    // Update the local list after Create/Initialize; a refresh is not needed
    // to learn a server-acknowledged draft or its new generation.
    QListWidgetItem *found = nullptr;
    for (int index = 0; index < this->drafts->count(); ++index)
    {
        QListWidgetItem *item = this->drafts->item(index);
        if (item->data(Qt::UserRole).toString() == draft.id) { found = item; break; }
    }
    if (found == nullptr)
    {
        found = new QListWidgetItem(this->drafts);
        found->setData(Qt::UserRole, draft.id);
    }
    found->setText(tr("%1 · generation %2%3").arg(draft.id, draft.generation,
        draft.dirty ? tr(" · modified") : QString()));
    {
        const QSignalBlocker block(this->drafts);
        this->drafts->setCurrentItem(found);
    }
    setBusy(false);
    this->status->setText(tr("Personal draft record loaded successfully."));
    this->details->setText(tr("Draft UUIDv7: %1\nSession: %2\nBase revision: %3\n"
                              "Generation: %4\nSaved changes: %5\n"
                              "Loaded candidate records: %6")
        .arg(draft.id, draft.session_id,
             draft.base_revision.isEmpty() ? tr("Unborn") : draft.base_revision,
             draft.generation, draft.dirty ? tr("Yes") : tr("None"),
             QString::number(draft.changes.size())));
    // Until full network discovery and editor Save are integrated, only
    // candidate roots can be previewed here. No edit capture is enabled.
    QString network_id;
    for (const PersonalWorkingCopyChange &change : draft.changes)
    {
        if (change.kind != 100 || change.deleted) continue;
        if (!network_id.isEmpty())
        {
            this->details->setText(this->details->text() +
                tr("\nMultiple networks are present. Choose a network in the future editor workflow."));
            return;
        }
        network_id = change.uuid;
    }
    if (network_id.isEmpty()) return;
    this->client.loadDraftNetwork(this->target, draft.id, network_id,
        [this, draft](const NetworkHydraulic &network,
                      const QString &generation, const QString &error)
    {
        if (!error.isEmpty())
        {
            this->status->setText(tr("Draft loaded, but hydraulic network hydration failed: %1").arg(error));
            return;
        }
        if (generation != draft.generation || !this->active_draft.has_value()
            || this->active_draft->id != draft.id
            || this->active_draft->generation != generation)
        {
            this->status->setText(tr("Draft changed while loading the hydraulic model. Reopen it to get a consistent view."));
            return;
        }
        this->preview_network = network;
        this->preview_draft_id = draft.id;
        this->preview_generation = generation;
        setBusy(false);
        this->details->setText(this->details->text() +
            tr("\n\nVerified hydraulic network: %1\nNetwork UUIDv7: %2\n"
               "Junctions: %3 · Pipes: %4 · Reservoirs: %5 · Tanks: %6")
            .arg(network.id, network.uuid.toString(QUuid::WithoutBraces),
                 QString::number(network.nodes_junctions.size()),
                 QString::number(network.links_pipes.size()),
                 QString::number(network.nodes_reservoirs.size()),
                 QString::number(network.nodes_tanks.size())));
        this->status->setText(tr("Hydraulic model verified. You can view it in Distribution Monitor; edits are not yet saved to this draft."));
    });
}

const std::optional<NetworkHydraulic> &PersonalWorkingCopyDialog::selectedPreviewNetwork() const
{
    return this->preview_network;
}

QString PersonalWorkingCopyDialog::selectedPreviewDraftId() const
{
    return this->accepted_draft_id;
}

QString PersonalWorkingCopyDialog::selectedPreviewGeneration() const
{
    return this->accepted_generation;
}

void PersonalWorkingCopyDialog::viewDraftNetwork()
{
    if (this->busy || !this->active_draft.has_value() || !this->preview_network.has_value()
        || this->active_draft->id != this->preview_draft_id
        || this->active_draft->generation != this->preview_generation)
        return;
    this->accepted_draft_id = this->preview_draft_id;
    this->accepted_generation = this->preview_generation;
    accept();
}

void PersonalWorkingCopyDialog::initializeNetwork()
{
    if (this->busy || !this->active_draft.has_value()) return;
    const PersonalWorkingCopyInfo draft = *this->active_draft;
    if (!draft.base_revision.isEmpty() || draft.dirty || !draft.changes.isEmpty()
        || !DvmdbIdentity::isUuidV7(draft.session_id)) return;
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("Initialize first hydraulic network"),
        tr("Network name:"), QLineEdit::Normal, tr("Network"), &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;
    const QString network_id = DvmdbIdentity::newUuidV7();
    setBusy(true);
    this->status->setText(tr("Acquiring private draft writer lease…"));
    this->client.acquireLease(this->target, draft.id, draft.session_id, 90,
        [this, draft, network_id, name](const PersonalWorkingCopyLease &lease, const QString &error)
    {
        if (!error.isEmpty())
        {
            setBusy(false);
            this->status->setText(tr("Unable to acquire private draft lease: %1").arg(error));
            return;
        }
        if (lease.generation != draft.generation)
        {
            this->client.releaseLease(this->target, draft.id, draft.session_id, lease.token,
                [this](const QString &)
            {
                setBusy(false);
                this->status->setText(tr("Draft generation changed. Reopen it before initializing a network."));
            });
            return;
        }
        this->status->setText(tr("Initializing first hydraulic network in private draft…"));
        this->client.initializeEmptyNetwork(this->target, draft.id, draft.session_id,
            lease.token, lease.generation, network_id, name,
            [this, draft, lease](const PersonalWorkingCopyInfo &updated, const QString &init_error)
        {
            // Release even if the HTTP acknowledgement was lost. Never resend
            // initialization after an uncertain result; reopen to reconcile.
            this->client.releaseLease(this->target, draft.id, draft.session_id, lease.token,
                [this, draft, updated, init_error](const QString &release_error)
            {
                setBusy(false);
                if (!init_error.isEmpty())
                {
                    this->status->setText(tr("Network initialization was not confirmed: %1 "
                        "Reopen the draft to check whether it succeeded. %2")
                        .arg(init_error, release_error));
                    return;
                }
                if (updated.project_id != draft.project_id || updated.branch_id != draft.branch_id
                    || !updated.base_revision.isEmpty())
                {
                    this->status->setText(tr("Unexpected initialized draft identity. Reopen before proceeding."));
                    return;
                }
                showDraft(updated);
                if (!release_error.isEmpty())
                    this->status->setText(tr("Network initialized, but lease release is unconfirmed: %1")
                        .arg(release_error));
            });
        });
    });
}

void PersonalWorkingCopyDialog::checkoutBranch()
{
    this->active_draft.reset();
    this->preview_network.reset();
    setBusy(true);
    this->status->setText(this->head.isEmpty()
        ? tr("Checking out unborn branch into personal DVMDB…")
        : tr("Transferring committed branch into personal DVMDB…"));
    this->client.checkoutBranchPrivately(this->target, this->source_endpoint,
        this->source_repository_id, this->project_id, this->project_name,
        this->branch_id, this->head, [this](const QString &error)
    {
        setBusy(false);
        if (!error.isEmpty())
        {
            this->status->setText(tr("Checkout failed: %1").arg(error));
            return;
        }
        this->status->setText(tr("Branch checked out to personal DVMDB. "
                                 "You can now create a private draft at this HEAD."));
    });
}
