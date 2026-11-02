#pragma once

#include "Backend.h"
#include "FileTypes.h"

#include <QAbstractTableModel>

class FileModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Name, Type, Size, Date, ColumnCount };

    explicit FileModel(QObject* parent = nullptr);

    void setEntries(const QList<Entry>& entries, bool withParentRow);
    void setShowHidden(bool show);
    // Folder on the Mac being shown (empty for devices); enables real icons for .app bundles.
    void setLocalDir(const QString& dir) { m_localDir = dir; }
    bool showHidden() const { return m_showHidden; }

    const Entry& entryAt(int row) const { return m_rows[row]; }
    bool isParentRow(int row) const { return m_hasParent && row == 0; }
    int rowOfName(const QString& name) const;
    void setDirSize(const QString& name, qint64 size);
    // Sets sizes of several folders at once (name -> size).
    void applyDirSizes(const QHash<QString, qint64>& sizes);
    QList<Entry> entries() const;   // visible entries without ".."
    bool contains(const QString& name) const;

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    void sort(int column, Qt::SortOrder order) override;

private:
    void rebuild();
    void sortRows();

    QList<Entry> m_all;
    QList<Entry> m_rows;
    QList<TypeInfo> m_types;
    QString m_localDir;
    mutable QHash<QString, QIcon> m_appIcons;
    bool m_hasParent = false;
    bool m_showHidden = false;
    int m_sortColumn = Name;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};
