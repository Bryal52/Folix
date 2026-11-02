#pragma once

#include "Backend.h"

#include <QSet>

class JobState;

struct TransferRequest {
    BackendPtr src;
    QString srcDir;
    QList<Entry> items;
    BackendPtr dst;
    QString dstDir;
    bool move = false;
    QSet<QString> overwrite;   // names that already exist at the destination and may be replaced
};

void runTransfer(JobState& st, const TransferRequest& r);
void runDelete(JobState& st, const BackendPtr& b, const QString& dir, const QList<Entry>& items, bool toTrash);
