#pragma once

#include "Backend.h"

#include <QIcon>

class QPainter;
class QRectF;

enum class FileCat {
    Folder, GameFolder, Up,
    Android, Windows, Archive, Video, Image, Audio, Text, Pdf, Code,
    Vita, Switch, Disc, Package, License, Plugin, Save,
    Unknown,
};

struct TypeInfo {
    FileCat cat = FileCat::Unknown;
    QString description;
};

QString extensionOf(const QString& name);
TypeInfo classify(const Entry& e);
bool isGameFolderName(const QString& name);

// Icons are painted in code so they look the same everywhere. GUI thread only.
QIcon fileIcon(FileCat cat);
QIcon deviceIcon(DeviceKind kind);
QIcon placeIcon(const QString& place);   // home, desktop, downloads, documents, volume, add
void paintFileIcon(QPainter& p, const QRectF& box, FileCat cat);
