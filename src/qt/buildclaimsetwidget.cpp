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
#include <primitives/claim.h>

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

    progressBar->setRange(0, 100);
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

    m_refreshTimer->setInterval(10000);
    connect(m_refreshTimer, &QTimer::timeout, this, &BuildClaimSetWidget::refreshClaimsTable);
    m_refreshTimer->start();
}

BuildClaimSetWidget::~BuildClaimSetWidget()
{
}

void BuildClaimSetWidget::setModel(WalletModel* model)
{
    m_walletModel = model;

    if (m_walletModel) {
        connect(m_walletModel, &WalletModel::claimsIndexUpdated, this, &BuildClaimSetWidget::onClaimsIndexUpdated);
        onClaimsIndexUpdated();
    }
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
    if (!m_walletModel) {
        // infoLabel->setText(tr("No wallet model. Cannot build claimset."));
        progressBar->setValue(0);
        progressBar->setFormat("No data available");
        return;
    }

    const CAmount totalClaimableCoins = Params().GetConsensus().genesisValue;
    const QString totalClaimableCoinsStr = BitcoinUnits::format(BitcoinUnit::BTC, totalClaimableCoins, false, BitcoinUnits::SeparatorStyle::ALWAYS);

    if (g_claims.empty()) {
        // infoLabel->setText(tr("No claims found in index."));
        progressBar->setValue(0);
        progressBar->setFormat(tr("0 / %1 (0%)")
            .arg(totalClaimableCoinsStr)
    );
        return;
    }

    std::vector<CClaim> allClaims;
    allClaims.reserve(g_claims.size());
    CAmount totalCoinsSent = 0;

    for (const auto& [_, claim] : g_claims) {
        allClaims.emplace_back(claim);
        totalCoinsSent += claim.nTotalReceived;
    }
    std::sort(allClaims.begin(), allClaims.end(),
              [](const CClaim& a, const CClaim& b) {
                  return a.nTime > b.nTime;
              });

    m_claimsModel->updateData(allClaims);

    // infoLabel->setText(
    //     tr("Built a ClaimSet with %1 claims.").arg(allClaims.size())
    // );

    int progress = std::min(100, static_cast<int>(100.0 * totalCoinsSent / totalClaimableCoins));

    progressBar->setValue(progress);
    progressBar->setFormat(
        tr("%1 / %2 (%3%)")
            .arg(BitcoinUnits::format(BitcoinUnit::BTC, totalCoinsSent, false, BitcoinUnits::SeparatorStyle::ALWAYS))
            .arg(totalClaimableCoinsStr)
            .arg(progress)
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
