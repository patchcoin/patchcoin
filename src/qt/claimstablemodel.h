#ifndef PATCHCOIN_CLAIMSTABLEMODEL_H
#define PATCHCOIN_CLAIMSTABLEMODEL_H

#include <QAbstractTableModel>
#include <vector>

class Claim;

struct ClaimData {
    QString queued;
    QString sourceAddress;
    QString targetAddress;
    QString time;
    double received;
    double eligible;
    double original;
};

class ClaimsTableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    explicit ClaimsTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void updateData(const std::vector<Claim>& claims);

private:
    std::vector<ClaimData> m_claims;
};

#endif // PATCHCOIN_CLAIMSTABLEMODEL_H
