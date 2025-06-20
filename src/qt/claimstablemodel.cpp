#include "bitcoinunits.h"
#include "guiutil.h"

#include <qt/claimstablemodel.h>
#include <QDateTime>
#include <key_io.h>
#include <claim.h>
#include <qt/walletmodel.h>

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
    return 6; // Queued, Source Address, Target Address, Time, Received, Eligible
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
            case 4: return BitcoinUnits::format(BitcoinUnit::BTC, claim.received, false, BitcoinUnits::SeparatorStyle::ALWAYS);
            case 5: return BitcoinUnits::format(BitcoinUnit::BTC, claim.eligible, false, BitcoinUnits::SeparatorStyle::ALWAYS);
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
                case 1: return tr("PPC/BTC Address");
                case 2: return tr("PTC Address");
                case 3: return tr("Date");
                case 4: return tr("Received");
                case 5: return tr("Eligible");
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

void ClaimsTableModel::updateData(const std::vector<Claim>& claims)
{
    std::vector<ClaimData> claims_data{};
    for (const auto& c : claims) {
        ClaimData data;
        data.queued = QString::fromStdString(c.m_seen ? "✔️" : "❌"); // lol
        data.sourceAddress = QString::fromStdString(c.m_is_btc ? c.GetBtcSourceAddress() : c.GetSourceAddress());
        data.targetAddress = QString::fromStdString(c.GetTargetAddress());

        QDateTime dt = QDateTime::fromSecsSinceEpoch(c.nTime);
        data.time = GUIUtil::dateTimeStr(dt);

        data.received = c.nTotalReceived;
        data.original = c.m_is_btc ? c.GetBitcoinBalance() : c.GetPeercoinBalance(); // patchcoin todo
        data.eligible = c.GetEligible();
        if (c.m_is_btc && !c.m_electrum_result) {
            data.eligible = c.nTotalReceived;
        }

        claims_data.push_back(data);
    }

    if (m_claims == claims_data) {
        return;
    }

    beginResetModel();
    m_claims = std::move(claims_data);
    endResetModel();
}
