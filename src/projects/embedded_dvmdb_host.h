#pragma once

#include <QString>

// Standalone-only transport adapter. A single in-process DVMDB ProjectService
// owns the catalogue lock for each local source; the GUI uses its existing
// validated HTTP draft protocol over a process-owned loopback listener.
// Never persist the ephemeral loopback endpoint as a server profile.
#ifdef AOWIS_STANDALONE_DVMDB_ENABLED
QString embeddedDvmdbEndpoint(const QString &storage_path, bool personal,
                              QString *repository_id, QString *error);
#endif
