#pragma once

#include "Backend.h"

enum class Guard { None, Warn, Block };

struct GuardResult {
    Guard level = Guard::None;
    QString reason;
};

// Deleting, moving or renaming `path` (or anything containing a protected path).
GuardResult guardForModify(DeviceKind kind, const QString& path);
// Copying something into `dir`.
GuardResult guardForWriteInto(DeviceKind kind, const QString& dir);
