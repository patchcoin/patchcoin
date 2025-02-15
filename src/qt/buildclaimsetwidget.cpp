#include <qt/bitcoinunits.h>
#include <qt/buildclaimsetwidget.h>

#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <QTimer>
#include <claimset.h>
#include <claim.h>

BuildClaimSetWidget::BuildClaimSetWidget(const PlatformStyle* platformStyle, QWidget* parent)
    : QWidget(parent)
    , m_claimsModel(new ClaimsTableModel(this))
    , m_refreshTimer(new QTimer(this))
    , m_platformStyle(platformStyle)
    , infoLabel(new QLabel(this))
    , progressBar(new QProgressBar(this))
    , claimsTableView(new QTableView(this))
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    progressBar->setRange(0, 1000);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat("Progress: %p%");
    progressBar->setStyleSheet("QProgressBar { background-color: transparent; text-align: center; color: #002600; border: 1px solid #666666; } QProgressBar::chunk { background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #00db98, stop:0.5 #00ff92, stop:1 #a4ffa3); margin: 0px; }");
    mainLayout->addWidget(progressBar);

    // infoLabel->setText("Total claimed:");
    // mainLayout->addWidget(infoLabel);

    claimsTableView->setModel(m_claimsModel);
    claimsTableView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    claimsTableView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    claimsTableView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    claimsTableView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    claimsTableView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    claimsTableView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    claimsTableView->resizeColumnsToContents();
    claimsTableView->setAlternatingRowColors(true);
    claimsTableView->setSelectionBehavior(QAbstractItemView::SelectRows);
    claimsTableView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    claimsTableView->setSortingEnabled(false);
    claimsTableView->verticalHeader()->hide();
    mainLayout->addWidget(claimsTableView);

    setLayout(mainLayout);

    populateClaimsTableFromModel();
    m_refreshTimer->setInterval(2500);
    connect(m_refreshTimer, &QTimer::timeout, this, &BuildClaimSetWidget::refreshClaimsTable);
    m_refreshTimer->start();
}

BuildClaimSetWidget::~BuildClaimSetWidget()
{
}

void BuildClaimSetWidget::onClaimsIndexUpdated()
{
    populateClaimsTableFromModel();
}

void BuildClaimSetWidget::refreshClaimsTable()
{
    populateClaimsTableFromModel();
}

void BuildClaimSetWidget::populateClaimsTableFromModel()
{
    const CAmount totalClaimableCoins = Params().GetConsensus().genesisValue;
    const QString totalClaimableCoinsStr = BitcoinUnits::format(BitcoinUnit::BTC, totalClaimableCoins, false, BitcoinUnits::SeparatorStyle::ALWAYS).split(".").first();

    if (g_claims.empty()) {
        progressBar->setValue(0);
        progressBar->setFormat(tr("0 / %1 (0%)").arg(totalClaimableCoinsStr));
        return;
    }

    QItemSelectionModel* selectionModel = claimsTableView->selectionModel();
    QModelIndexList selectedIndexes = selectionModel->selectedRows();
    QSet<QString> selectedClaims;

    for (const QModelIndex& index : selectedIndexes) {
        QString claimId = m_claimsModel->data(m_claimsModel->index(index.row(), 1)).toString();
        selectedClaims.insert(claimId);
    }

    std::vector<Claim> allClaims;
    CAmount totalCoinsSent = 0;
    for (const auto& [_, claim] : g_claims) {
        allClaims.emplace_back(claim);
        totalCoinsSent += claim.nTotalReceived;
    }
    std::sort(allClaims.begin(), allClaims.end(),
              [](const Claim& a, const Claim& b) { return a.nTime > b.nTime; });

    m_claimsModel->updateData(allClaims);
    selectionModel->clearSelection();
    QItemSelection newSelection;

    for (int row = 0; row < m_claimsModel->rowCount(); ++row) {
        QString currentId = m_claimsModel->data(m_claimsModel->index(row, 1)).toString();
        if (selectedClaims.contains(currentId)) {
            newSelection.select(m_claimsModel->index(row, 0),
                              m_claimsModel->index(row, m_claimsModel->columnCount() - 1));
        }
    }

    selectionModel->select(newSelection,
                         QItemSelectionModel::Select | QItemSelectionModel::Rows);
    claimsTableView->viewport()->update();

    double progress = 100.00 * (static_cast<double>(totalCoinsSent) / COIN) / (static_cast<double>(totalClaimableCoins) / COIN);
    int scaled_progress = static_cast<int>(std::round(progress * 10));
    progressBar->setValue(std::min(scaled_progress, 1000));
    progressBar->setFormat(
        tr("%1 / %2 (%3%)")
            .arg(BitcoinUnits::format(BitcoinUnit::BTC, totalCoinsSent, false, BitcoinUnits::SeparatorStyle::ALWAYS).split(".").first())
            .arg(totalClaimableCoinsStr)
            .arg(QString::number(progress, 'f', 2))
    );
}

void BuildClaimSetWidget::filterClaims(const QString& searchString)
{
    if (!m_claimsModel) {
        return;
    }

    for (int i = 0; i < m_claimsModel->rowCount(); ++i) {
        bool matches = false;
        for (int j = 0; j < m_claimsModel->columnCount(); ++j) {
            QModelIndex index = m_claimsModel->index(i, j);
            if (index.data().toString().contains(searchString, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }
        claimsTableView->setRowHidden(i, !matches);
    }
}
