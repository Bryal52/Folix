#include "FileModel.h"

#include "Tools.h"

#include <QApplication>
#include <QCollator>
#include <QFileIconProvider>
#include <QPalette>

FileModel::FileModel(QObject* parent) : QAbstractTableModel(parent) {}

void FileModel::setEntries(const QList<Entry>& entries, bool withParentRow)
{
    m_all = entries;
    m_hasParent = withParentRow;
    rebuild();
}

void FileModel::setShowHidden(bool show)
{
    if (m_showHidden == show)
        return;
    m_showHidden = show;
    rebuild();
}

void FileModel::rebuild()
{
    beginResetModel();
    m_rows.clear();
    for (const Entry& e : std::as_const(m_all)) {
        if (!m_showHidden && (e.hidden || e.name.startsWith(QLatin1Char('.'))))
            continue;
        m_rows.append(e);
    }
    sortRows();
    if (m_hasParent) {
        Entry up;
        up.name = QStringLiteral("..");
        up.isDir = true;
        m_rows.prepend(up);
    }
    m_types.clear();
    m_types.reserve(m_rows.size());
    for (const Entry& e : std::as_const(m_rows))
        m_types.append(classify(e));
    endResetModel();
}

void FileModel::sortRows()
{
    QCollator coll{QLocale(QLocale::Russian)};
    coll.setNumericMode(true);
    coll.setCaseSensitivity(Qt::CaseInsensitive);

    const int col = m_sortColumn;
    const bool asc = m_sortOrder == Qt::AscendingOrder;
    std::stable_sort(m_rows.begin(), m_rows.end(), [&](const Entry& a, const Entry& b) {
        if (a.isDir != b.isDir)
            return a.isDir;   // folders always first
        int c = 0;
        switch (col) {
        case Type:
            c = QString::compare(extensionOf(a.name), extensionOf(b.name), Qt::CaseInsensitive);
            break;
        case Size:
            c = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case Date:
            c = a.mtime < b.mtime ? -1 : (a.mtime > b.mtime ? 1 : 0);
            break;
        default:
            break;
        }
        if (c == 0)
            c = coll.compare(a.name, b.name);
        return asc ? c < 0 : c > 0;
    });
}

void FileModel::sort(int column, Qt::SortOrder order)
{
    m_sortColumn = column;
    m_sortOrder = order;
    rebuild();
}

int FileModel::rowOfName(const QString& name) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i].name == name)
            return i;
    }
    return -1;
}

bool FileModel::contains(const QString& name) const
{
    for (const Entry& e : m_all) {
        if (e.name.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

void FileModel::setDirSize(const QString& name, qint64 size)
{
    for (Entry& e : m_all) {
        if (e.name == name)
            e.size = size;
    }
    const int row = rowOfName(name);
    if (row < 0)
        return;
    m_rows[row].size = size;
    emit dataChanged(index(row, Size), index(row, Size));
}

void FileModel::applyDirSizes(const QHash<QString, qint64>& sizes)
{
    if (sizes.isEmpty())
        return;
    for (Entry& e : m_all) {
        if (e.isDir) {
            const auto it = sizes.constFind(e.name);
            if (it != sizes.constEnd())
                e.size = *it;
        }
    }
    for (int i = m_hasParent ? 1 : 0; i < m_rows.size(); ++i) {
        Entry& e = m_rows[i];
        if (e.isDir) {
            const auto it = sizes.constFind(e.name);
            if (it != sizes.constEnd())
                e.size = *it;
        }
    }
    if (!m_rows.isEmpty())
        emit dataChanged(index(0, Size), index(int(m_rows.size()) - 1, Size));
}

QList<Entry> FileModel::entries() const
{
    return m_hasParent ? m_rows.mid(1) : m_rows;
}

int FileModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

int FileModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant FileModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Entry& e = m_rows[index.row()];
    const TypeInfo& t = m_types[index.row()];
    const bool parentRow = isParentRow(index.row());

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case Name:
            return e.name;
        case Type:
            return parentRow ? QString() : t.description;
        case Size:
            if (parentRow)
                return QString();
            if (e.isDir) {
                if (e.size == -2)
                    return QStringLiteral("считаю…");
                return e.size >= 0 ? Tools::humanSize(e.size) : QStringLiteral("—");
            }
            return Tools::humanSize(e.size);
        case Date:
            return (parentRow || !e.mtime.isValid()) ? QString()
                                                     : e.mtime.toString(QStringLiteral("dd.MM.yyyy HH:mm"));
        }
        break;
    case Qt::DecorationRole:
        if (index.column() == Name && e.isDir && !m_localDir.isEmpty() && e.name.endsWith(QLatin1String(".app"))) {
            const QString path = joinPath(m_localDir, e.name);
            auto it = m_appIcons.find(path);
            if (it == m_appIcons.end()) {
                static QFileIconProvider provider;
                it = m_appIcons.insert(path, provider.icon(QFileInfo(path)));
            }
            return *it;
        }
        if (index.column() == Name)
            return fileIcon(t.cat);
        break;
    case Qt::TextAlignmentRole:
        if (index.column() == Size || index.column() == Date)
            return int(Qt::AlignRight | Qt::AlignVCenter);
        return int(Qt::AlignLeft | Qt::AlignVCenter);
    case Qt::ForegroundRole: {
        const QPalette pal = QApplication::palette();
        const bool dim = e.hidden || (index.column() == Name && e.name.startsWith(QLatin1Char('.')) && !parentRow);
        if (dim || index.column() == Type || index.column() == Date || (index.column() == Size && e.isDir && e.size < 0))
            return pal.color(QPalette::PlaceholderText);
        break;
    }
    case Qt::ToolTipRole:
        if (index.column() == Name && !parentRow && !e.parts.isEmpty())
            return e.parts.join(QLatin1Char('\n'));
        break;
    }
    return {};
}

QVariant FileModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole) {
        switch (section) {
        case Name: return QStringLiteral("Имя");
        case Type: return QStringLiteral("Формат");
        case Size: return QStringLiteral("Размер");
        case Date: return QStringLiteral("Изменён");
        }
    }
    if (role == Qt::TextAlignmentRole && (section == Size || section == Date))
        return int(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

Qt::ItemFlags FileModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsDropEnabled;
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (!isParentRow(index.row()))
        f |= Qt::ItemIsDragEnabled;
    return f;
}
