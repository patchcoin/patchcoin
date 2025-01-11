#include <qt/buildclaimsetwidget.h>

#include <QHeaderView>
#include <QLabel>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
#include <QTimer>
#include <claimset.h>
#include <index/claimindex.h>
#include <primitives/claim.h>

BuildClaimSetWidget::BuildClaimSetWidget(const PlatformStyle* platformStyle, QWidget* parent)
    : QWidget(parent)
    , m_claimsModel(new ClaimsTableModel(this))
    , m_refreshTimer(new QTimer(this))
    , m_platformStyle(platformStyle)
    , infoLabel(new QLabel(this))
    , claimsTableView(new QTableView(this))
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    infoLabel->setText("Total claimed:"); // patchcoin todo
    // infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    mainLayout->addWidget(infoLabel);

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

void BuildClaimSetWidget::setModel(WalletModel *model)
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
        infoLabel->setText(tr("No wallet model. Cannot build claimset."));
        return;
    }

    std::vector<CClaim> allClaims;
    for (const auto& [_, claim] : g_claims) {
        allClaims.emplace_back(claim);
    }

    if (allClaims.empty()) {
        infoLabel->setText(tr("No claims found in index."));
        return;
    }
    /*
    CClaimSet claimset;
    try {
        // patchcoin this is calling verify each time its rendered. this shouldn't be needed
        claimset = BuildClaimSet(allClaims);
    } catch (const std::runtime_error& e) {
        infoLabel->setText(tr("Failed to build/sign claimset: %1").arg(e.what()));
        return;
    }
    */

    m_claimsModel->updateData(allClaims); // patchcoin todo sorting shouldnt be a thing here, please fix it

    infoLabel->setText(
        tr("Built a ClaimSet with %1 claims.")
            .arg(allClaims.size())
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
