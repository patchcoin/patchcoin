#ifndef PATCHCOIN_BUILDCLAIMSETWIDGET_H
#define PATCHCOIN_BUILDCLAIMSETWIDGET_H

#include <QProgressBar>
#include <QWidget>
#include <QTimer>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>
#include <qt/claimstablemodel.h>

class ClaimsTableModel;
class QProgressBar;
class QSortFilterProxyModel;
class QTableView;
class QLabel;
class PlatformStyle;

namespace Ui {
    class BuildClaimSetWidget;
} // namespace Ui

class BuildClaimSetWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BuildClaimSetWidget(const PlatformStyle* platformStyle, QWidget* parent = nullptr);
    ~BuildClaimSetWidget();

public Q_SLOTS:
    void filterClaims(const QString& searchString);

private Q_SLOTS:
    void refreshClaimsTable();
    void onClaimsIndexUpdated();

private:
    void populateClaimsTableFromModel();

    const PlatformStyle* m_platformStyle;
    ClaimsTableModel* m_claimsModel;
    QSortFilterProxyModel* m_proxyModel;
    QTimer* m_refreshTimer;
    QProgressBar* progressBar;
    QTableView* claimsTableView;
};

#endif // PATCHCOIN_BUILDCLAIMSETWIDGET_H
