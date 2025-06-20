// Copyright (c) 2011-2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_SIGNVERIFYMESSAGEDIALOG_H
#define BITCOIN_QT_SIGNVERIFYMESSAGEDIALOG_H

#include <QDialog>
#include <QNetworkReply>

class PlatformStyle;
class ClientModel;
class WalletModel;

namespace Ui {
    class SignVerifyMessageDialog;
}

class SignVerifyMessageDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SignVerifyMessageDialog(ClientModel* clientModel, const PlatformStyle *platformStyle, QWidget *parent);
    ~SignVerifyMessageDialog();

    void setModel(WalletModel *model);
    void setAddress_SM(const QString &address);
    void setAddress_VM(const QString &address);
    void setClaim_VM(const QString &source, const QString &target);

    void showTab_SM(bool fShow);
    void showTab_VM(bool fShow);

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
    void changeEvent(QEvent* e) override;

private:
    Ui::SignVerifyMessageDialog *ui;
    ClientModel* m_client_model{nullptr};
    WalletModel* model{nullptr};
    const PlatformStyle *platformStyle;

private Q_SLOTS:
    /* sign message */
    void on_addressBookButton_SM_clicked();
    void on_pasteButton_SM_clicked();
    void on_signMessageButton_SM_clicked();
    void on_copySignatureButton_SM_clicked();
    void on_clearButton_SM_clicked();
    /* verify message */
    void on_addressBookButton_VM_clicked();
    void onCryptoCheckboxToggled(bool checked);
    bool on_verifyMessageButton_VM_clicked();
    void on_publishClaimButton_SM_clicked();
    void handleVerifyReply(QNetworkReply* reply);
    void displayAmount(const QJsonObject& obj, const QString& messageTemplate);
    void showStatus(const QString& text, bool success);
    void on_clearButton_VM_clicked();
};

#endif // BITCOIN_QT_SIGNVERIFYMESSAGEDIALOG_H
