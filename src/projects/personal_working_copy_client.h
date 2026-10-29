#pragma once

#include "config/personal_repository_storage.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QJsonObject>
#include <QStringList>

#include <functional>

// Draft identities are local to the personal repository. This client NEVER
// falls back to a project-source profile and NEVER changes a committed HEAD.
struct PersonalWorkingCopyChange
{
    QString uuid;
    bool deleted = false;
    quint16 kind = 0;
    QStringList references;
    QByteArray payload;
};

struct PersonalWorkingCopyInfo
{
    QString id;
    QString project_id;
    QString branch_id;
    QString base_revision; // empty == explicit unborn HEAD
    QString session_id;
    QString generation; // decimal, not a lossy JSON number
    bool dirty = false;
    QList<PersonalWorkingCopyChange> changes; // populated by reopen(), not list()
};

struct PersonalWorkingCopyLease
{
    QString token;
    QString generation;
    QString expires_at_ms;
};

class PersonalWorkingCopyClient final : public QObject
{
public:
    using DraftCompletion = std::function<void(const PersonalWorkingCopyInfo &, const QString &)>;
    using ListCompletion = std::function<void(const QList<PersonalWorkingCopyInfo> &, const QString &)>;
    using LeaseCompletion = std::function<void(const PersonalWorkingCopyLease &, const QString &)>;
    using Completion = std::function<void(const QString &)>;

    explicit PersonalWorkingCopyClient(QObject *parent = nullptr);

    void list(const PersonalRepositoryTarget &target, ListCompletion completion);
    void create(const PersonalRepositoryTarget &target, const QString &project_id,
                const QString &branch, const QString &base_revision,
                DraftCompletion completion);
    void reopen(const PersonalRepositoryTarget &target, const QString &draft_id,
                DraftCompletion completion);
    // Every write is explicitly scoped to the personal repository. A save
    // acknowledges only the exact next generation; uncertain network failures
    // must be resolved by reopen(), never by blindly replaying a batch.
    void acquireLease(const PersonalRepositoryTarget &target, const QString &draft_id,
                      const QString &session_id, int seconds, LeaseCompletion completion);
    void saveChanges(const PersonalRepositoryTarget &target, const QString &draft_id,
                     const QString &session_id, const QString &fencing_token,
                     const QString &expected_generation,
                     const QList<PersonalWorkingCopyChange> &changes,
                     DraftCompletion completion);
    void releaseLease(const PersonalRepositoryTarget &target, const QString &draft_id,
                      const QString &session_id, const QString &fencing_token,
                      Completion completion);

    // Committed REST branch only. Transfers a verified full UUIDv7 bundle into
    // the personal repository; does not create a draft or commit.
    using CheckoutCompletion = std::function<void(const QString &)>;
    void checkoutCommittedBranch(const PersonalRepositoryTarget &target,
        const QString &source_endpoint, const QString &source_repository_id,
        const QString &project_id, const QString &project_name,
        const QString &branch_name, const QString &expected_head,
        CheckoutCompletion completion);

    static QString newSessionUuidV7();

private:
    using Response = std::function<void(const QJsonObject &, const QString &)>;
    void request(const PersonalRepositoryTarget &target, const QByteArray &method,
                 const QString &path, const QJsonObject &body, Response completion);
    static bool parseDraft(const QJsonObject &object, PersonalWorkingCopyInfo &draft);
    QNetworkAccessManager manager;
};
