#include "settings/personal_repository_settings_widget.h"

#include "config/project_server_profile_storage.h"

#include <QFormLayout>
#include <QLabel>
#include <QHBoxLayout>
#include <QFont>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PersonalRepositorySettingsWidget::PersonalRepositorySettingsWidget(QWidget *parent)
    : QWidget(parent), monitor(this)
{
    setObjectName(QStringLiteral("personalRepositorySettings"));
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);

    QLabel *heading = new QLabel(tr("Personal working repository"), this);
    QFont title_font = heading->font();
    title_font.setBold(true);
    heading->setFont(title_font);
    layout->addWidget(heading);

    QLabel *description = new QLabel(tr(
        "This is separate from the shared and read-only databases in Projects. "
        "Draft editing is disabled until the private repository has been verified. "
        "Configuration and identity verification do not authenticate the server."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
    QLabel *embedded = new QLabel(tr(
        "Standalone uses its dedicated embedded DVMDB at: %1. "
        "Native repository role/identity verification and draft checkout are not enabled yet.")
        .arg(defaultLocalDvmdbPath()), this);
    embedded->setObjectName(QStringLiteral("personalRepositoryStandaloneStatus"));
    embedded->setWordWrap(true);
    layout->addWidget(embedded);
#else
    QFormLayout *form = new QFormLayout();
    this->endpoint = new QLineEdit(this);
    this->endpoint->setObjectName(QStringLiteral("personalRepositoryEndpoint"));
    this->endpoint->setPlaceholderText(QStringLiteral("http://127.0.0.1:8755"));
    form->addRow(tr("Private DVMDB URL:"), this->endpoint);
    this->repository_id = new QLineEdit(this);
    this->repository_id->setObjectName(QStringLiteral("personalRepositoryExpectedId"));
    this->repository_id->setPlaceholderText(tr("UUIDv7 from GET /dvmdb/v1/repository"));
    form->addRow(tr("Expected repository UUID:"), this->repository_id);
    layout->addLayout(form);

    QHBoxLayout *actions = new QHBoxLayout();
    this->save_button = new QPushButton(tr("Save and verify"), this);
    this->save_button->setObjectName(QStringLiteral("personalRepositorySave"));
    actions->addWidget(this->save_button);
    QPushButton *recheck = new QPushButton(tr("Check again"), this);
    recheck->setObjectName(QStringLiteral("personalRepositoryRecheck"));
    actions->addWidget(recheck);
    QPushButton *remove = new QPushButton(tr("Remove configuration"), this);
    remove->setObjectName(QStringLiteral("personalRepositoryRemove"));
    actions->addWidget(remove);
    layout->addLayout(actions);

    connect(this->save_button, &QPushButton::clicked, this, [this] { save(); });
    connect(recheck, &QPushButton::clicked, this, [this] { verify(); });
    connect(remove, &QPushButton::clicked, this, [this] { clear(); });
#endif

    this->status = new QLabel(this);
    this->status->setWordWrap(true);
    this->status->setObjectName(QStringLiteral("personalRepositoryStatus"));
    layout->addWidget(this->status);

    QLabel *warning = new QLabel(tr(
        "Security: a repository UUID and URL are not authentication. "
        "Use an isolated private service; do not expose writable HTTP DVMDB to untrusted users. "
        "Credentials must not be stored in GUI settings."), this);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    layout->addStretch();
    restore();
}

PersonalRepositoryMonitor::State PersonalRepositorySettingsWidget::verificationState() const
{
    return this->monitor.currentState();
}

void PersonalRepositorySettingsWidget::restore()
{
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
    updateStatus(PersonalRepositoryMonitor::State::Unconfigured,
                 tr("Embedded repository identity verification is not yet available."));
#else
    const std::optional<PersonalRepositoryProfile> profile = loadPersonalRepositoryProfile();
    if (!profile)
    {
        updateStatus(PersonalRepositoryMonitor::State::Unconfigured, QString());
        return;
    }
    this->endpoint->setText(profile->endpoint);
    this->repository_id->setText(profile->repository_id);
    verify();
#endif
}

void PersonalRepositorySettingsWidget::save()
{
#ifndef AOWIS_STANDALONE_DVMDB_ENABLED
    const PersonalRepositoryProfile profile{
        this->endpoint->text().trimmed(), this->repository_id->text().trimmed()};
    QString error;
    if (!validatePersonalRepositoryProfile(profile, &error))
    {
        this->monitor.stop();
        updateStatus(PersonalRepositoryMonitor::State::Unavailable, error);
        return;
    }
    if (!savePersonalRepositoryProfile(profile))
    {
        this->monitor.stop();
        updateStatus(PersonalRepositoryMonitor::State::Unavailable,
                     tr("Could not save the personal repository configuration."));
        return;
    }
    verify();
#endif
}

void PersonalRepositorySettingsWidget::clear()
{
#ifndef AOWIS_STANDALONE_DVMDB_ENABLED
    if (!clearPersonalRepositoryProfile())
    {
        updateStatus(PersonalRepositoryMonitor::State::Unavailable,
                     tr("Could not remove the personal repository configuration."));
        return;
    }
    this->monitor.stop();
    this->endpoint->clear();
    this->repository_id->clear();
    updateStatus(PersonalRepositoryMonitor::State::Unconfigured, QString());
#endif
}

void PersonalRepositorySettingsWidget::verify()
{
#ifndef AOWIS_STANDALONE_DVMDB_ENABLED
    // Recheck the SAVED profile, never unsaved edits or the active source.
    const std::optional<PersonalRepositoryProfile> profile = loadPersonalRepositoryProfile();
    if (!profile)
    {
        this->monitor.stop();
        updateStatus(PersonalRepositoryMonitor::State::Unconfigured, QString());
        return;
    }
    this->monitor.start(*profile,
        [this](PersonalRepositoryMonitor::State state, const QString &detail)
        {
            updateStatus(state, detail);
        });
#endif
}

void PersonalRepositorySettingsWidget::updateStatus(
    PersonalRepositoryMonitor::State state, const QString &detail)
{
    QString text;
    switch (state)
    {
        case PersonalRepositoryMonitor::State::Unconfigured:
            text = tr("Not configured — private draft editing unavailable."); break;
        case PersonalRepositoryMonitor::State::Checking:
            text = tr("Checking private repository identity and role…"); break;
        case PersonalRepositoryMonitor::State::Verified:
            text = tr("Private repository identity verified (not authenticated). "
                      "Draft editing will be enabled in VC-04."); break;
        case PersonalRepositoryMonitor::State::WrongIdentity:
            text = tr("Wrong repository — personal editing disabled."); break;
        case PersonalRepositoryMonitor::State::WrongRole:
            text = tr("Repository is not marked private — personal editing disabled."); break;
        case PersonalRepositoryMonitor::State::Unavailable:
            text = tr("Private repository unavailable — personal editing disabled."); break;
    }
    if (!detail.isEmpty()) text += QStringLiteral(" ") + detail;
    this->status->setText(text);
}
