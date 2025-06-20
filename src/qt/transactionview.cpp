// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include <qt/transactionview.h>

#include <qt/addresstablemodel.h>
#include <qt/bitcoinunits.h>
#include <qt/buildclaimsetwidget.h>
#include <qt/csvmodelwriter.h>
#include <qt/editaddressdialog.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <qt/platformstyle.h>
#include <qt/transactiondescdialog.h>
#include <qt/transactionfilterproxy.h>
#include <qt/transactionrecord.h>
#include <qt/transactiontablemodel.h>
#include <qt/walletmodel.h>

#include <key_io.h>
#include <snapshotmanager.h>
#include <node/interface_ui.h>

#include <chrono>
#include <optional>

#include <QSplitter>
#include <QTableWidget>
#include <QApplication>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDesktopServices>
#include <QDoubleValidator>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QScrollBar>
#include <QSettings>
#include <QTableView>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

TransactionView::TransactionView(const PlatformStyle *platformStyle, WalletView* walletView, QWidget *parent)
    : QWidget(parent), m_platform_style{platformStyle}, m_walletView(walletView), m_nam(new QNetworkAccessManager(this))
{
    // Build filter row
    setContentsMargins(0,0,0,0);

    QHBoxLayout *hlayout = new QHBoxLayout();
    hlayout->setContentsMargins(0,0,0,0);

    if (platformStyle->getUseExtraSpacing()) {
        hlayout->setSpacing(5);
        hlayout->addSpacing(26);
    } else {
        hlayout->setSpacing(0);
        hlayout->addSpacing(23);
    }

    watchOnlyWidget = new QComboBox(this);
    watchOnlyWidget->setFixedWidth(24);
    watchOnlyWidget->addItem("", TransactionFilterProxy::WatchOnlyFilter_All);
    watchOnlyWidget->addItem(QIcon(":/icons/eye_plus"), "", TransactionFilterProxy::WatchOnlyFilter_Yes);
    watchOnlyWidget->addItem(QIcon(":/icons/eye_minus"), "", TransactionFilterProxy::WatchOnlyFilter_No);
    hlayout->addWidget(watchOnlyWidget);

    dateWidget = new QComboBox(this);
    if (platformStyle->getUseExtraSpacing()) {
        dateWidget->setFixedWidth(121);
    } else {
        dateWidget->setFixedWidth(120);
    }
    dateWidget->addItem(tr("All"), All);
    dateWidget->addItem(tr("Today"), Today);
    dateWidget->addItem(tr("This week"), ThisWeek);
    dateWidget->addItem(tr("This month"), ThisMonth);
    dateWidget->addItem(tr("Last month"), LastMonth);
    dateWidget->addItem(tr("This year"), ThisYear);
    dateWidget->addItem(tr("Range…"), Range);
    hlayout->addWidget(dateWidget);

    typeWidget = new QComboBox(this);
    if (platformStyle->getUseExtraSpacing()) {
        typeWidget->setFixedWidth(121);
    } else {
        typeWidget->setFixedWidth(120);
    }

    typeWidget->addItem(tr("All"), TransactionFilterProxy::ALL_TYPES);
    typeWidget->addItem(tr("All but mint"), TransactionFilterProxy::ALL_TYPES ^
                                            TransactionFilterProxy::TYPE(TransactionRecord::StakeMint));
    typeWidget->addItem(tr("Received with"), TransactionFilterProxy::TYPE(TransactionRecord::RecvWithAddress) |
                                             TransactionFilterProxy::TYPE(TransactionRecord::RecvFromOther));
    typeWidget->addItem(tr("Sent to"), TransactionFilterProxy::TYPE(TransactionRecord::SendToAddress) |
                                       TransactionFilterProxy::TYPE(TransactionRecord::SendToOther));
    typeWidget->addItem(tr("To yourself"), TransactionFilterProxy::TYPE(TransactionRecord::SendToSelf));
    typeWidget->addItem(tr("Mined"), TransactionFilterProxy::TYPE(TransactionRecord::Generated));
    typeWidget->addItem(tr("Mint by stake"), TransactionFilterProxy::TYPE(TransactionRecord::StakeMint));
    typeWidget->addItem(tr("Other"), TransactionFilterProxy::TYPE(TransactionRecord::Other));

    hlayout->addWidget(typeWidget);

    search_widget = new QLineEdit(this);
    search_widget->setPlaceholderText(tr("Enter address, transaction id, or label to search"));
    hlayout->addWidget(search_widget);

    amountWidget = new QLineEdit(this);
    amountWidget->setPlaceholderText(tr("Min amount"));
    if (platformStyle->getUseExtraSpacing()) {
        amountWidget->setFixedWidth(97);
    } else {
        amountWidget->setFixedWidth(100);
    }
    QDoubleValidator *amountValidator = new QDoubleValidator(0, 1e20, 8, this);
    QLocale amountLocale(QLocale::C);
    amountLocale.setNumberOptions(QLocale::RejectGroupSeparator);
    amountValidator->setLocale(amountLocale);
    amountWidget->setValidator(amountValidator);
    hlayout->addWidget(amountWidget);

    // Delay before filtering transactions
    static constexpr auto input_filter_delay{500ms};

    QTimer* amount_typing_delay = new QTimer(this);
    amount_typing_delay->setSingleShot(true);
    amount_typing_delay->setInterval(input_filter_delay);

    QTimer* prefix_typing_delay = new QTimer(this);
    prefix_typing_delay->setSingleShot(true);
    prefix_typing_delay->setInterval(input_filter_delay);

    QSplitter* splitter = new QSplitter(this);
    splitter->setOrientation(Qt::Horizontal);

    QWidget* leftWidget = new QWidget(this);
    leftWidget->setContentsMargins(0,0,0,0);
    QVBoxLayout* leftVLayout = new QVBoxLayout(leftWidget);
    leftVLayout->setContentsMargins(0,0,0,0);
    leftVLayout->setSpacing(0);

    transactionView = new QTableView(this);
    transactionView->setObjectName("transactionView");

    leftVLayout->addLayout(hlayout);
    leftVLayout->addWidget(createDateRangeWidget());
    leftVLayout->addWidget(transactionView);
    leftWidget->setLayout(leftVLayout);
    splitter->addWidget(leftWidget);

    QWidget* rightWidget = new QWidget(this);
    rightWidget->setContentsMargins(0,0,0,0);
    QVBoxLayout* rightVLayout = new QVBoxLayout(rightWidget);
    rightVLayout->setContentsMargins(0,0,0,0);
    rightVLayout->setSpacing(0);

    QSplitter* rightInnerSplitter = new QSplitter(Qt::Vertical, this);

    buildClaimSetWidget = new BuildClaimSetWidget(platformStyle, this);
    rightInnerSplitter->addWidget(buildClaimSetWidget);

    snapshotTable = new QTableWidget(this);
    snapshotTable->setColumnCount(3);
    snapshotTable->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    snapshotTable->setHorizontalHeaderLabels(QStringList() << tr("PPC/BTC Address") << tr("PPC/BTC Amount") << tr("Patchcoin Eligible"));
    snapshotTable->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    snapshotTable->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    snapshotTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    snapshotTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    snapshotTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    snapshotTable->resizeColumnsToContents();
    snapshotTable->setAlternatingRowColors(true);
    snapshotTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    snapshotTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    snapshotTable->setSortingEnabled(false);
    snapshotTable->verticalHeader()->hide();
    snapshotTable->setContextMenuPolicy(Qt::CustomContextMenu);
    waitForSnapshot = new QTimer(this);
    connect(snapshotTable, &QTableWidget::customContextMenuRequested, this, &TransactionView::snapshotTableContextMenuRequested);
    connect(waitForSnapshot, &QTimer::timeout, this, &TransactionView::PopulateSnapshotTable);
    snapshotContextMenu = new QMenu(this);
    snapshotContextMenu->setObjectName("snapshotContextMenu");
    claimAddressAction = snapshotContextMenu->addAction(tr("Claim address"), this, &TransactionView::claimSnapshotAddress);
    searchAddressAction = snapshotContextMenu->addAction(tr("Search address"), this, &TransactionView::searchThisSnapshotAddress);
    connect(m_nam, &QNetworkAccessManager::finished, this, &TransactionView::onGetBalanceFinished);

    rightInnerSplitter->addWidget(snapshotTable);
    rightInnerSplitter->setStretchFactor(0, 1);
    rightInnerSplitter->setStretchFactor(1, 2);

    rightVLayout->addWidget(rightInnerSplitter);
    rightWidget->setLayout(rightVLayout);

    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 2);
    splitter->setStretchFactor(1, 3);

    QVBoxLayout* mainVLayout = new QVBoxLayout(this);
    mainVLayout->setContentsMargins(0,0,0,0);
    mainVLayout->setSpacing(0);
    mainVLayout->addWidget(splitter);
    setLayout(mainVLayout);

    int width = transactionView->verticalScrollBar()->sizeHint().width();
    // Cover scroll bar width with spacing
    if (platformStyle->getUseExtraSpacing()) {
        hlayout->addSpacing(width+2);
    } else {
        hlayout->addSpacing(width);
    }
    transactionView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    transactionView->setTabKeyNavigation(false);
    transactionView->setContextMenuPolicy(Qt::CustomContextMenu);
    transactionView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    transactionView->resizeColumnsToContents();
    transactionView->installEventFilter(this);
    transactionView->setAlternatingRowColors(true);
    transactionView->setSelectionBehavior(QAbstractItemView::SelectRows);
    transactionView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    transactionView->setSortingEnabled(true);
    transactionView->verticalHeader()->hide();

    /*
    QSettings settings;
    if (!transactionView->horizontalHeader()->restoreState(settings.value("TransactionViewHeaderState").toByteArray())) {
        transactionView->setColumnWidth(TransactionTableModel::Status, STATUS_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Watchonly, WATCHONLY_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Date, DATE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Type, TYPE_COLUMN_WIDTH);
        transactionView->setColumnWidth(TransactionTableModel::Amount, AMOUNT_MINIMUM_COLUMN_WIDTH);
        transactionView->horizontalHeader()->setMinimumSectionSize(MINIMUM_COLUMN_WIDTH);
        transactionView->horizontalHeader()->setStretchLastSection(true);
    }
    */

    contextMenu = new QMenu(this);
    contextMenu->setObjectName("contextMenu");
    copyAddressAction = contextMenu->addAction(tr("&Copy address"), this, &TransactionView::copyAddress);
    copyLabelAction = contextMenu->addAction(tr("Copy &label"), this, &TransactionView::copyLabel);
    contextMenu->addAction(tr("Copy &amount"), this, &TransactionView::copyAmount);
    contextMenu->addAction(tr("Copy transaction &ID"), this, &TransactionView::copyTxID);
    contextMenu->addAction(tr("Copy &raw transaction"), this, &TransactionView::copyTxHex);
    contextMenu->addAction(tr("Copy full transaction &details"), this, &TransactionView::copyTxPlainText);
    contextMenu->addAction(tr("&Show transaction details"), this, &TransactionView::showDetails);
    contextMenu->addSeparator();
    abandonAction = contextMenu->addAction(tr("A&bandon transaction"), this, &TransactionView::abandonTx);
    contextMenu->addAction(tr("&Edit address label"), this, &TransactionView::editLabel);

    connect(dateWidget, qOverload<int>(&QComboBox::activated), this, &TransactionView::chooseDate);
    connect(typeWidget, qOverload<int>(&QComboBox::activated), this, &TransactionView::chooseType);
    connect(watchOnlyWidget, qOverload<int>(&QComboBox::activated), this, &TransactionView::chooseWatchonly);
    connect(amountWidget, &QLineEdit::textChanged, amount_typing_delay, qOverload<>(&QTimer::start));
    connect(amount_typing_delay, &QTimer::timeout, this, &TransactionView::changedAmount);
    connect(search_widget, &QLineEdit::textChanged, prefix_typing_delay, qOverload<>(&QTimer::start));
    connect(prefix_typing_delay, &QTimer::timeout, this, &TransactionView::changedSearch);

    connect(transactionView, &QTableView::doubleClicked, this, &TransactionView::doubleClicked);
    connect(transactionView, &QTableView::customContextMenuRequested, this, &TransactionView::contextualMenu);

    // Double-clicking on a transaction on the transaction history page shows details
    connect(this, &TransactionView::doubleClicked, this, &TransactionView::showDetails);
    // Highlight transaction after fee bump
    connect(this, &TransactionView::bumpedFee, [this](const uint256& txid) {
      focusTransaction(txid);
    });
}

TransactionView::~TransactionView()
{
    QSettings settings;
    settings.setValue("TransactionViewHeaderState", transactionView->horizontalHeader()->saveState());
    if (waitForSnapshot) {
        waitForSnapshot->stop();
        delete waitForSnapshot;
    }
}

void TransactionView::PopulateSnapshotTable()
{
    if (!snapshotTable) return;

    SnapshotManager& sman = SnapshotManager::Peercoin();

    const auto& validMap = sman.GetScriptPubKeys();
    const auto& incompMap = sman.GetIncompatibleScriptPubKeys();

    if (validMap.empty() && incompMap.empty()) {
        return;
    }

    waitForSnapshot->stop();

    snapshotTable->clearContents();
    snapshotTable->setRowCount(static_cast<int>(validMap.size() + incompMap.size() + /* incompatible header */ 1));

    int row = 0;
    const QBrush greyBrush(Qt::gray);

    auto addRowToTable = [&](const auto& scriptPubKey, CAmount balance, bool isCompatible) {
        CTxDestination dest;
        ExtractDestination(scriptPubKey, dest);
        std::string address = EncodeDestination(dest);

        QTableWidgetItem* addressItem = new QTableWidgetItem(QString::fromStdString(address));
        QString valStr = BitcoinUnits::format(BitcoinUnit::BTC, balance, false, BitcoinUnits::SeparatorStyle::ALWAYS);
        QTableWidgetItem* valItem = new QTableWidgetItem(valStr);
        valItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        CAmount eligible = 0;
        sman.CalculateEligible(balance, eligible);
        QString elStr = BitcoinUnits::format(BitcoinUnit::BTC, eligible, false, BitcoinUnits::SeparatorStyle::ALWAYS);
        QTableWidgetItem* elItem = new QTableWidgetItem(elStr);
        elItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsEditable);
        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
        elItem->setFlags(elItem->flags() & ~Qt::ItemIsEditable);

        // if (!isCompatible) {
        //     addressItem->setForeground(greyBrush);
        //     valItem->setForeground(greyBrush);
        //     elItem->setForeground(greyBrush);
        //
        //     addressItem->setFlags(addressItem->flags() & ~Qt::ItemIsSelectable);
        //     valItem->setFlags(valItem->flags() & ~Qt::ItemIsSelectable);
        //     elItem->setFlags(elItem->flags() & ~Qt::ItemIsSelectable);
        // }

        snapshotTable->setItem(row, 0, addressItem);
        snapshotTable->setItem(row, 1, valItem);
        snapshotTable->setItem(row, 2, elItem);

        row++;
    };

    for (const auto& [scriptPubKey, balance] : validMap) {
        addRowToTable(scriptPubKey, balance, true);
    }

    if (!incompMap.empty()) {
        QTableWidgetItem* headerItem = new QTableWidgetItem(tr("Claim supported via dummy tx"));
        headerItem->setTextAlignment(Qt::AlignCenter);
        headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEditable);

        headerItem->setForeground(greyBrush);

        snapshotTable->setItem(row, 0, headerItem);
        snapshotTable->setSpan(row, 0, 1, 3);
        row++;
    }

    for (const auto& [scriptPubKey, outs] : incompMap) {
        CAmount balance = 0;
        for (const auto& [out, coin] : outs) {
            balance += coin.out.nValue;
        }
        addRowToTable(scriptPubKey, balance, false);
    }
}

void TransactionView::filterSnapshotTable()
{
    if (!snapshotTable) return;

    QString searchString = search_widget->text();
    for (int i = 0; i < snapshotTable->rowCount(); ++i) {
        bool matches = false;
        for (int j = 0; j < snapshotTable->columnCount(); ++j) {
            QTableWidgetItem* item = snapshotTable->item(i, j);
            if (item && item->text().contains(searchString, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }
        snapshotTable->setRowHidden(i, !matches);
    }
}

void TransactionView::searchThisSnapshotAddress()
{
    QList<QTableWidgetItem*> selectedItems = snapshotTable->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }

    int row = selectedItems.first()->row();
    QString peercoinAddress = snapshotTable->item(row, 0)->text();

    search_widget->setText(peercoinAddress);
}

void TransactionView::snapshotTableContextMenuRequested(const QPoint &pos)
{
    QTableWidgetItem* item = snapshotTable->itemAt(pos);
    if (!item || item->foreground() == QBrush(Qt::gray)) {
        return;
    }

    /*
    if (snapshotTable->currentColumn() != 0) {
        return;
    }
    */

    claimAddressAction->setEnabled(true);
    searchAddressAction->setEnabled(true);

    snapshotContextMenu->exec(snapshotTable->viewport()->mapToGlobal(pos));
}

void TransactionView::claimSnapshotAddress()
{
    if (!m_walletView) return;

    QList<QTableWidgetItem*> selectedItems = snapshotTable->selectedItems();
    if (selectedItems.isEmpty()) {
        return;
    }
    int row = selectedItems.first()->row();
    QTableWidgetItem* addressItem = snapshotTable->item(row, 0);

    if (addressItem->foreground() == QBrush(Qt::gray)) {
        return;
    }

    QString peercoinAddress = addressItem->text();

    m_walletView->gotoVerifyMessageTabWithClaim(peercoinAddress);
}

void TransactionView::setModel(WalletModel *_model)
{
    this->model = _model;
    if(_model)
    {
        transactionProxyModel = new TransactionFilterProxy(this);
        transactionProxyModel->setSourceModel(_model->getTransactionTableModel());
        transactionProxyModel->setDynamicSortFilter(true);
        transactionProxyModel->setSortCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
        transactionProxyModel->setSortRole(Qt::EditRole);
        transactionView->setModel(transactionProxyModel);
        transactionView->sortByColumn(TransactionTableModel::Date, Qt::DescendingOrder);
        // patchcoin todo
        transactionView->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        transactionView->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        transactionView->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        transactionView->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        transactionView->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        transactionView->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Stretch);
        transactionView->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);


        if (_model->getOptionsModel())
        {
            // Add third party transaction URLs to context menu
            QStringList listUrls = GUIUtil::SplitSkipEmptyParts(_model->getOptionsModel()->getThirdPartyTxUrls(), "|");
            bool actions_created = false;
            for (int i = 0; i < listUrls.size(); ++i)
            {
                QString url = listUrls[i].trimmed();
                QString host = QUrl(url, QUrl::StrictMode).host();
                if (!host.isEmpty())
                {
                    if (!actions_created) {
                        contextMenu->addSeparator();
                        actions_created = true;
                    }
                    /*: Transactions table context menu action to show the
                        selected transaction in a third-party block explorer.
                        %1 is a stand-in argument for the URL of the explorer. */
                    contextMenu->addAction(tr("Show in %1").arg(host), [this, url] { openThirdPartyTxUrl(url); });
                }
            }
        }

        // show/hide column Watch-only
        updateWatchOnlyColumn(_model->wallet().haveWatchOnly());

        // Watch-only signal
        connect(_model, &WalletModel::notifyWatchonlyChanged, this, &TransactionView::updateWatchOnlyColumn);

        waitForSnapshot->start(100);
    }
}

void TransactionView::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        watchOnlyWidget->setItemIcon(
            TransactionFilterProxy::WatchOnlyFilter_Yes,
            m_platform_style->SingleColorIcon(QStringLiteral(":/icons/eye_plus")));
        watchOnlyWidget->setItemIcon(
            TransactionFilterProxy::WatchOnlyFilter_No,
            m_platform_style->SingleColorIcon(QStringLiteral(":/icons/eye_minus")));
    }

    QWidget::changeEvent(e);
}

void TransactionView::chooseDate(int idx)
{
    if (!transactionProxyModel) return;
    QDate current = QDate::currentDate();
    dateRangeWidget->setVisible(false);
    switch(dateWidget->itemData(idx).toInt())
    {
    case All:
        transactionProxyModel->setDateRange(
                std::nullopt,
                std::nullopt);
        break;
    case Today:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(current),
                std::nullopt);
        break;
    case ThisWeek: {
        // Find last Monday
        QDate startOfWeek = current.addDays(-(current.dayOfWeek()-1));
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(startOfWeek),
                std::nullopt);

        } break;
    case ThisMonth:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1)),
                std::nullopt);
        break;
    case LastMonth:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1).addMonths(-1)),
                GUIUtil::StartOfDay(QDate(current.year(), current.month(), 1)));
        break;
    case ThisYear:
        transactionProxyModel->setDateRange(
                GUIUtil::StartOfDay(QDate(current.year(), 1, 1)),
                std::nullopt);
        break;
    case Range:
        dateRangeWidget->setVisible(true);
        dateRangeChanged();
        break;
    }
}

void TransactionView::chooseType(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setTypeFilter(
        typeWidget->itemData(idx).toInt());
}

void TransactionView::chooseWatchonly(int idx)
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setWatchOnlyFilter(
        static_cast<TransactionFilterProxy::WatchOnlyFilter>(watchOnlyWidget->itemData(idx).toInt()));
}

void TransactionView::changedSearch()
{
    if (!transactionProxyModel)
        return;

    QString text = search_widget->text();
    transactionProxyModel->setSearchString(text);

    for (int i = 0; i < snapshotTable->rowCount(); ++i) {
        bool matches = false;
        for (int j = 0; j < snapshotTable->columnCount(); ++j) {
            auto *item = snapshotTable->item(i, j);
            if (item && item->text().contains(text, Qt::CaseInsensitive)) {
                matches = true;
                break;
            }
        }
        snapshotTable->setRowHidden(i, !matches);
    }
    buildClaimSetWidget->filterClaims(text);

    std::string addr = text.toStdString();
    if (IsValidDestinationString(addr, *Params().BitcoinMain())) {
        const QUrl url(QStringLiteral("http://bitcoin-verify.patchcoin.org/balance/%1")
                       .arg(text));
        m_nam->get(QNetworkRequest(url));
    }
}

void TransactionView::onGetBalanceFinished(QNetworkReply* reply)
{
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "Network error fetching balance:" << reply->errorString();
        return;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "Invalid JSON in balance reply:" << parseError.errorString();
        return;
    }
    QJsonObject obj = doc.object();

    bool found = obj.value("found").toBool();
    qint64 sats = obj.value("balance").toVariant().toLongLong();
    QString addr = reply->url().path().section('/', -1);

    int row = -1;
    for (int i = 0; i < snapshotTable->rowCount(); ++i) {
        QTableWidgetItem* it = snapshotTable->item(i, 0);
        if (it && it->text() == addr) {
            row = i;
            break;
        }
    }
    if (row < 0) {
        row = snapshotTable->rowCount();
        snapshotTable->insertRow(row);
        snapshotTable->setItem(row, 0, new QTableWidgetItem(addr));
    }

    int balanceAlign = Qt::AlignLeft | Qt::AlignVCenter;
    if (snapshotTable->rowCount() > 0) {
        if (auto *proto = snapshotTable->item(0, 1)) {
            balanceAlign = proto->textAlignment();
        }
    }

    QTableWidgetItem* balanceItem = snapshotTable->item(row, 1);
    if (!balanceItem) {
        balanceItem = new QTableWidgetItem;
        snapshotTable->setItem(row, 1, balanceItem);
    }

    if (found) {
        double btc = sats / 1e8;
        balanceItem->setText(QString::number(btc, 'f', 8));

        int eligibleAlign = balanceAlign;
        if (snapshotTable->rowCount() > 0) {
            if (auto *proto2 = snapshotTable->item(0, 2)) {
                eligibleAlign = proto2->textAlignment();
            }
        }

        QTableWidgetItem* eligibleItem = snapshotTable->item(row, 2);
        if (!eligibleItem) {
            eligibleItem = new QTableWidgetItem;
            snapshotTable->setItem(row, 2, eligibleItem);
        }

        CAmount eligible = 0;
        SnapshotManager::CalculateEligibleBTC(sats, eligible);
        double ptc = eligible / 1e6;
        eligibleItem->setText(QString::number(ptc, 'f', 6));

        balanceItem->setTextAlignment(balanceAlign);
        eligibleItem->setTextAlignment(eligibleAlign);
    } else {
        QString err = obj.contains("error")
                          ? obj.value("error").toString()
                          : QStringLiteral("Address not found");
        balanceItem->setText(err);
        balanceItem->setTextAlignment(balanceAlign);

        QTableWidgetItem* eligibleItem = snapshotTable->item(row, 2);
        if (!eligibleItem) {
            eligibleItem = new QTableWidgetItem;
            snapshotTable->setItem(row, 2, eligibleItem);
        }
        eligibleItem->setText(QString());
        if (snapshotTable->rowCount() > 0) {
            if (auto *proto2 = snapshotTable->item(0, 2)) {
                eligibleItem->setTextAlignment(proto2->textAlignment());
            }
        }
    }

    snapshotTable->setRowHidden(row, false);
}

void TransactionView::changedAmount()
{
    if(!transactionProxyModel)
        return;
    CAmount amount_parsed = 0;
    if (BitcoinUnits::parse(model->getOptionsModel()->getDisplayUnit(), amountWidget->text(), &amount_parsed)) {
        transactionProxyModel->setMinAmount(amount_parsed);
    }
    else
    {
        transactionProxyModel->setMinAmount(0);
    }
}

void TransactionView::exportClicked()
{
    if (!model || !model->getOptionsModel()) {
        return;
    }

    // CSV is currently the only supported format
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Export Transaction History"), QString(),
        /*: Expanded name of the CSV file format.
            See: https://en.wikipedia.org/wiki/Comma-separated_values. */
        tr("Comma separated file") + QLatin1String(" (*.csv)"), nullptr);

    if (filename.isNull())
        return;

    CSVModelWriter writer(filename);

    // name, column, role
    writer.setModel(transactionProxyModel);
    writer.addColumn(tr("Confirmed"), 0, TransactionTableModel::ConfirmedRole);
    if (model->wallet().haveWatchOnly())
        writer.addColumn(tr("Watch-only"), TransactionTableModel::Watchonly);
    writer.addColumn(tr("Date"), 0, TransactionTableModel::DateRole);
    writer.addColumn(tr("Type"), TransactionTableModel::Type, Qt::EditRole);
    writer.addColumn(tr("Label"), 0, TransactionTableModel::LabelRole);
    writer.addColumn(tr("Address"), 0, TransactionTableModel::AddressRole);
    writer.addColumn(BitcoinUnits::getAmountColumnTitle(model->getOptionsModel()->getDisplayUnit()), 0, TransactionTableModel::FormattedAmountRole);
    writer.addColumn(tr("ID"), 0, TransactionTableModel::TxHashRole);

    if(!writer.write()) {
        Q_EMIT message(tr("Exporting Failed"), tr("There was an error trying to save the transaction history to %1.").arg(filename),
            CClientUIInterface::MSG_ERROR);
    }
    else {
        Q_EMIT message(tr("Exporting Successful"), tr("The transaction history was successfully saved to %1.").arg(filename),
            CClientUIInterface::MSG_INFORMATION);
    }
}

void TransactionView::contextualMenu(const QPoint &point)
{
    QModelIndex index = transactionView->indexAt(point);
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if (selection.empty())
        return;

    // check if transaction can be abandoned, disable context menu action in case it doesn't
    uint256 hash;
    hash.SetHex(selection.at(0).data(TransactionTableModel::TxHashRole).toString().toStdString());
    abandonAction->setEnabled(model->wallet().transactionCanBeAbandoned(hash));
    copyAddressAction->setEnabled(GUIUtil::hasEntryData(transactionView, 0, TransactionTableModel::AddressRole));
    copyLabelAction->setEnabled(GUIUtil::hasEntryData(transactionView, 0, TransactionTableModel::LabelRole));

    if (index.isValid()) {
        GUIUtil::PopupMenu(contextMenu, transactionView->viewport()->mapToGlobal(point));
    }
}

void TransactionView::abandonTx()
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);

    // get the hash from the TxHashRole (QVariant / QString)
    uint256 hash;
    QString hashQStr = selection.at(0).data(TransactionTableModel::TxHashRole).toString();
    hash.SetHex(hashQStr.toStdString());

    // Abandon the wallet transaction over the walletModel
    model->wallet().abandonTransaction(hash);
}

void TransactionView::copyAddress()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::AddressRole);
}

void TransactionView::copyLabel()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::LabelRole);
}

void TransactionView::copyAmount()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::FormattedAmountRole);
}

void TransactionView::copyTxID()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHashRole);
}

void TransactionView::copyTxHex()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxHexRole);
}

void TransactionView::copyTxPlainText()
{
    GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
}

void TransactionView::editLabel()
{
    if(!transactionView->selectionModel() ||!model)
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        AddressTableModel *addressBook = model->getAddressTableModel();
        if(!addressBook)
            return;
        QString address = selection.at(0).data(TransactionTableModel::AddressRole).toString();
        if(address.isEmpty())
        {
            // If this transaction has no associated address, exit
            return;
        }
        // Is address in address book? Address book can miss address when a transaction is
        // sent from outside the UI.
        int idx = addressBook->lookupAddress(address);
        if(idx != -1)
        {
            // Edit sending / receiving address
            QModelIndex modelIdx = addressBook->index(idx, 0, QModelIndex());
            // Determine type of address, launch appropriate editor dialog type
            QString type = modelIdx.data(AddressTableModel::TypeRole).toString();

            auto dlg = new EditAddressDialog(
                type == AddressTableModel::Receive
                ? EditAddressDialog::EditReceivingAddress
                : EditAddressDialog::EditSendingAddress, this);
            dlg->setModel(addressBook);
            dlg->loadRow(idx);
            GUIUtil::ShowModalDialogAsynchronously(dlg);
        }
        else
        {
            // Add sending address
            auto dlg = new EditAddressDialog(EditAddressDialog::NewSendingAddress,
                this);
            dlg->setModel(addressBook);
            dlg->setAddress(address);
            GUIUtil::ShowModalDialogAsynchronously(dlg);
        }
    }
}

void TransactionView::showDetails()
{
    if(!transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows();
    if(!selection.isEmpty())
    {
        TransactionDescDialog *dlg = new TransactionDescDialog(selection.at(0));
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_opened_dialogs.append(dlg);
        connect(dlg, &QObject::destroyed, [this, dlg] {
            m_opened_dialogs.removeOne(dlg);
        });
        dlg->show();
    }
}

void TransactionView::openThirdPartyTxUrl(QString url)
{
    if(!transactionView || !transactionView->selectionModel())
        return;
    QModelIndexList selection = transactionView->selectionModel()->selectedRows(0);
    if(!selection.isEmpty())
         QDesktopServices::openUrl(QUrl::fromUserInput(url.replace("%s", selection.at(0).data(TransactionTableModel::TxHashRole).toString())));
}

QWidget *TransactionView::createDateRangeWidget()
{
    dateRangeWidget = new QFrame();
    dateRangeWidget->setFrameStyle(static_cast<int>(QFrame::Panel) | static_cast<int>(QFrame::Raised));
    dateRangeWidget->setContentsMargins(1,1,1,1);
    QHBoxLayout *layout = new QHBoxLayout(dateRangeWidget);
    layout->setContentsMargins(0,0,0,0);
    layout->addSpacing(23);
    layout->addWidget(new QLabel(tr("Range:")));

    dateFrom = new QDateTimeEdit(this);
    dateFrom->setDisplayFormat("dd/MM/yy");
    dateFrom->setCalendarPopup(true);
    dateFrom->setMinimumWidth(100);
    dateFrom->setDate(QDate::currentDate().addDays(-7));
    layout->addWidget(dateFrom);
    layout->addWidget(new QLabel(tr("to")));

    dateTo = new QDateTimeEdit(this);
    dateTo->setDisplayFormat("dd/MM/yy");
    dateTo->setCalendarPopup(true);
    dateTo->setMinimumWidth(100);
    dateTo->setDate(QDate::currentDate());
    layout->addWidget(dateTo);
    layout->addStretch();

    // Hide by default
    dateRangeWidget->setVisible(false);

    // Notify on change
    connect(dateFrom, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);
    connect(dateTo, &QDateTimeEdit::dateChanged, this, &TransactionView::dateRangeChanged);

    return dateRangeWidget;
}

void TransactionView::dateRangeChanged()
{
    if(!transactionProxyModel)
        return;
    transactionProxyModel->setDateRange(
            GUIUtil::StartOfDay(dateFrom->date()),
            GUIUtil::StartOfDay(dateTo->date()).addDays(1));
}

void TransactionView::focusTransaction(const QModelIndex &idx)
{
    if(!transactionProxyModel)
        return;
    QModelIndex targetIdx = transactionProxyModel->mapFromSource(idx);
    transactionView->scrollTo(targetIdx);
    transactionView->setCurrentIndex(targetIdx);
    transactionView->setFocus();
}

void TransactionView::focusTransaction(const uint256& txid)
{
    if (!transactionProxyModel)
        return;

    const QModelIndexList results = this->model->getTransactionTableModel()->match(
        this->model->getTransactionTableModel()->index(0,0),
        TransactionTableModel::TxHashRole,
        QString::fromStdString(txid.ToString()), -1);

    transactionView->setFocus();
    transactionView->selectionModel()->clearSelection();
    for (const QModelIndex& index : results) {
        const QModelIndex targetIndex = transactionProxyModel->mapFromSource(index);
        transactionView->selectionModel()->select(
            targetIndex,
            QItemSelectionModel::Rows | QItemSelectionModel::Select);
        // Called once per destination to ensure all results are in view, unless
        // transactions are not ordered by (ascending or descending) date.
        transactionView->scrollTo(targetIndex);
        // scrollTo() does not scroll far enough the first time when transactions
        // are ordered by ascending date.
        if (index == results[0]) transactionView->scrollTo(targetIndex);
    }
}

// Need to override default Ctrl+C action for amount as default behaviour is just to copy DisplayRole text
bool TransactionView::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_C && ke->modifiers().testFlag(Qt::ControlModifier))
        {
             GUIUtil::copyEntryData(transactionView, 0, TransactionTableModel::TxPlainTextRole);
             return true;
        }
    }
    if (event->type() == QEvent::EnabledChange) {
        if (!isEnabled()) {
            closeOpenedDialogs();
        }
    }
    return QWidget::eventFilter(obj, event);
}

// show/hide column Watch-only
void TransactionView::updateWatchOnlyColumn(bool fHaveWatchOnly)
{
    watchOnlyWidget->setVisible(fHaveWatchOnly);
    transactionView->setColumnHidden(TransactionTableModel::Watchonly, !fHaveWatchOnly);
}

void TransactionView::closeOpenedDialogs()
{
    // close all dialogs opened from this view
    for (QDialog* dlg : m_opened_dialogs) {
        dlg->close();
    }
    m_opened_dialogs.clear();
}
