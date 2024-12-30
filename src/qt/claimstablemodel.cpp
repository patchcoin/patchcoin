#include "bitcoinunits.h"
#include "guiutil.h"

#include <qt/claimstablemodel.h>
#include <QDateTime>
#include <key_io.h>
#include <primitives/claim.h>
#include <script/standard.h>
#include <qt/walletmodel.h>

#include <QLocale>

static int column_alignments[] = {
    Qt::AlignCenter|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignLeft|Qt::AlignVCenter,
    Qt::AlignRight|Qt::AlignVCenter,
    Qt::AlignRight|Qt::AlignVCenter
};

ClaimsTableModel::ClaimsTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int ClaimsTableModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return static_cast<int>(m_claims.size());
}

int ClaimsTableModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 6; // Queued, Source Address, Target Address, Time, Eligible, Original
}

QVariant ClaimsTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    if (index.row() >= static_cast<int>(m_claims.size()) || index.row() < 0)
        return QVariant();

    const ClaimData &claim = m_claims.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case 0: return claim.queued;
            case 1: return claim.sourceAddress;
            case 2: return claim.targetAddress;
            case 3: return claim.time;
            case 4: return BitcoinUnits::format(BitcoinUnit::BTC, claim.eligible, false, BitcoinUnits::SeparatorStyle::ALWAYS);
            case 5: return BitcoinUnits::format(BitcoinUnit::BTC, claim.original, false, BitcoinUnits::SeparatorStyle::ALWAYS);
            default: return QVariant();
        }
    }

    if (role == Qt::TextAlignmentRole) {
        return column_alignments[index.column()];
    }

    return QVariant();
}

QVariant ClaimsTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if(orientation == Qt::Horizontal)
    {
        if(role == Qt::DisplayRole)
        {
            switch (section)
            {
                case 0: return tr("  ");
                case 1: return tr("PPC Address");
                case 2: return tr("PTC Address");
                case 3: return tr("Date");
                case 4: return tr("Eligible");
                case 5: return tr("Original");
            default:;
            }
        }
        else if (role == Qt::TextAlignmentRole)
        {
            return column_alignments[section];
        } else if (role == Qt::ToolTipRole)
        {
            // patchcoin todo
        }
    }
    return QVariant();
}

Qt::ItemFlags ClaimsTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    return QAbstractTableModel::flags(index);
}

void ClaimsTableModel::updateData(const std::vector<CClaim>& claims)
{
    beginResetModel();
    m_claims.clear();

    for (const auto& c : claims) {
        ClaimData data;
        data.queued = QString::fromStdString(c.seen ? "✔️" : "❌"); // lol
        CTxDestination srcDest;
        if (ExtractDestination(c.sourceScriptPubKey, srcDest)) {
            data.sourceAddress = QString::fromStdString(EncodeDestination(srcDest));
        } else {
            data.sourceAddress = "unrecognized";
        }

        CTxDestination tgtDest;
        if (ExtractDestination(c.targetScriptPubKey, tgtDest)) {
            data.targetAddress = QString::fromStdString(EncodeDestination(tgtDest));
        } else {
            data.targetAddress = "unrecognized";
        }

        QDateTime dt = QDateTime::fromSecsSinceEpoch(c.nTime);
        data.time = GUIUtil::dateTimeStr(dt);

        data.eligible = c.nEligible;
        data.original = c.nTotalReceived; // patchcoin todo

        m_claims.push_back(data);
    }

    endResetModel();
}
