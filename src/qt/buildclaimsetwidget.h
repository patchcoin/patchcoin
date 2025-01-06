#ifndef PATCHCOIN_BUILDCLAIMSETWIDGET_H
#define PATCHCOIN_BUILDCLAIMSETWIDGET_H

#include <QWidget>
#include <QTimer>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <qt/claimstablemodel.h>

class QTableView;
class QLabel;

namespace Ui {
    class BuildClaimSetWidget;
} // namespace Ui

class BuildClaimSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BuildClaimSetWidget(const PlatformStyle *platformStyle, QWidget *parent = nullptr);
    ~BuildClaimSetWidget();

    void filterClaims(const QString& searchString);
    void setModel(WalletModel *model);

private Q_SLOTS:
    void onClaimsIndexUpdated();
    void refreshClaimsTable();
    void populateClaimsTableFromModel();

private:
    WalletModel* m_walletModel;
    ClaimsTableModel* m_claimsModel;
    QTimer* m_refreshTimer;
    const PlatformStyle* m_platformStyle;
    QLabel* infoLabel;
    QTableView* claimsTableView;
};

#endif // PATCHCOIN_BUILDCLAIMSETWIDGET_H
