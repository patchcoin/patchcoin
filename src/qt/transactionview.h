// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_TRANSACTIONVIEW_H
#define BITCOIN_QT_TRANSACTIONVIEW_H

#include <qt/guiutil.h>

#include <uint256.h>

#include <QWidget>
#include <QKeyEvent>
#include <QNetworkAccessManager>
#include <QTableWidget>
#include <qt/buildclaimsetwidget.h>

class WalletView;
class PlatformStyle;
class TransactionDescDialog;
class TransactionFilterProxy;
class WalletModel;

QT_BEGIN_NAMESPACE
class QComboBox;
class QDateTimeEdit;
class QFrame;
class QLineEdit;
class QMenu;
class QModelIndex;
class QTableView;
QT_END_NAMESPACE

/** Widget showing the transaction list for a wallet, including a filter row.
    Using the filter row, the user can view or export a subset of the transactions.
  */
class TransactionView : public QWidget
{
    Q_OBJECT

public:
    explicit TransactionView(const PlatformStyle *platformStyle, WalletView* walletView, QWidget *parent = nullptr);
    ~TransactionView();

    void setModel(WalletModel *model);

    // Date ranges for filter
    enum DateEnum
    {
        All,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
        ThisYear,
        Range
    };

    enum ColumnWidths {
        STATUS_COLUMN_WIDTH = 30,
        WATCHONLY_COLUMN_WIDTH = 23,
        DATE_COLUMN_WIDTH = 120,
        TYPE_COLUMN_WIDTH = 113,
        AMOUNT_MINIMUM_COLUMN_WIDTH = 120,
        MINIMUM_COLUMN_WIDTH = 23
    };

protected:
    void changeEvent(QEvent* e) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    ClientModel* clientModel{nullptr};
    WalletModel *model{nullptr};
    QTableWidget *snapshotTable{nullptr};
    BuildClaimSetWidget *buildClaimSetWidget{nullptr};
    TransactionFilterProxy *transactionProxyModel{nullptr};
    QTableView *transactionView{nullptr};

    QComboBox *dateWidget;
    QComboBox *typeWidget;
    QComboBox *watchOnlyWidget;
    QLineEdit *search_widget;
    QLineEdit *amountWidget;

    QMenu *contextMenu;

    QFrame *dateRangeWidget;
    QDateTimeEdit *dateFrom;
    QDateTimeEdit *dateTo;
    QAction *abandonAction{nullptr};
    QAction *copyAddressAction{nullptr};
    QAction *copyLabelAction{nullptr};

    QWidget *createDateRangeWidget();
    const PlatformStyle* m_platform_style;

    QList<TransactionDescDialog*> m_opened_dialogs;
    void PopulateSnapshotTable();
    void filterSnapshotTable();
    WalletView* m_walletView{nullptr};
    QNetworkAccessManager* m_nam{nullptr};
    QMenu* snapshotContextMenu{nullptr};
    QAction* claimAddressAction{nullptr};
    QAction* searchAddressAction{nullptr};
    QTimer* waitForSnapshot{nullptr};

private Q_SLOTS:
    void contextualMenu(const QPoint &);
    void dateRangeChanged();
    void showDetails();
    void copyAddress();
    void editLabel();
    void copyLabel();
    void copyAmount();
    void copyTxID();
    void copyTxHex();
    void copyTxPlainText();
    void openThirdPartyTxUrl(QString url);
    void updateWatchOnlyColumn(bool fHaveWatchOnly);
    void abandonTx();
    void snapshotTableContextMenuRequested(const QPoint &pos);
    void claimSnapshotAddress();
    void searchThisSnapshotAddress();

Q_SIGNALS:
    void doubleClicked(const QModelIndex&);

    /**  Fired when a message should be reported to the user */
    void message(const QString &title, const QString &message, unsigned int style);

    void bumpedFee(const uint256& txid);

public Q_SLOTS:
    void chooseDate(int idx);
    void chooseType(int idx);
    void chooseWatchonly(int idx);
    void changedAmount();
    void changedSearch();
    void onGetBalanceFinished(QNetworkReply* reply);
    void exportClicked();
    void closeOpenedDialogs();
    void focusTransaction(const QModelIndex&);
    void focusTransaction(const uint256& txid);
};

#endif // BITCOIN_QT_TRANSACTIONVIEW_H
