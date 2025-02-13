// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientmodel.h"

#include <qt/signverifymessagedialog.h>
#include <qt/forms/ui_signverifymessagedialog.h>

#include <qt/addressbookpage.h>
#include <qt/guiutil.h>
#include <qt/platformstyle.h>
#include <qt/walletmodel.h>

#include <key_io.h>
#include <util/message.h> // For MessageSign(), MessageVerify()
#include <wallet/wallet.h>

#include <vector>

#include <QClipboard>
#include <claimset.h>
#include <netmessagemaker.h>
#include <index/claimindex.h>
#include <interfaces/node.h>

SignVerifyMessageDialog::SignVerifyMessageDialog(ClientModel* client_model, const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::SignVerifyMessageDialog),
    m_client_model(client_model),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    ui->addressBookButton_SM->setIcon(QIcon(":/icons/address-book"));
    ui->pasteButton_SM->setIcon(QIcon(":/icons/editpaste"));
    ui->copySignatureButton_SM->setIcon(QIcon(":/icons/editcopy"));
    ui->signMessageButton_SM->setIcon(QIcon(":/icons/edit"));
    ui->clearButton_SM->setIcon(QIcon(":/icons/remove"));
    ui->addressBookButton_VM->setIcon(QIcon(":/icons/address-book"));
    ui->verifyMessageButton_VM->setIcon(QIcon(":/icons/transaction_0"));
    ui->publishClaimButton_SM->setIcon(QIcon(":/icons/send"));
    ui->publishClaimButton_SM->setEnabled(m_client_model->node().context()->connman->GetNodeCount(ConnectionDirection::Both) > 0);
    ui->clearButton_VM->setIcon(QIcon(":/icons/remove"));

    GUIUtil::setupAddressWidget(ui->addressIn_SM, this);
    GUIUtil::setupAddressWidget(ui->addressIn_VM, this);

    ui->addressIn_SM->installEventFilter(this);
    ui->messageIn_SM->installEventFilter(this);
    ui->signatureOut_SM->installEventFilter(this);
    ui->addressIn_VM->installEventFilter(this);
    ui->messageIn_VM->installEventFilter(this);
    ui->signatureIn_VM->installEventFilter(this);

    ui->signatureOut_SM->setFont(GUIUtil::fixedPitchFont());
    ui->signatureIn_VM->setFont(GUIUtil::fixedPitchFont());

    GUIUtil::handleCloseWindowShortcut(this);
}

SignVerifyMessageDialog::~SignVerifyMessageDialog()
{
    delete ui;
}

void SignVerifyMessageDialog::setModel(WalletModel *_model)
{
    this->model = _model;
}

void SignVerifyMessageDialog::setAddress_SM(const QString &address)
{
    ui->addressIn_SM->setText(address);
    ui->messageIn_SM->setFocus();
}

void SignVerifyMessageDialog::setAddress_VM(const QString &address)
{
    ui->addressIn_VM->setText(address);
    ui->messageIn_VM->setFocus();
}

void SignVerifyMessageDialog::setClaim_VM(const QString &source, const QString &target)
{
    ui->addressIn_VM->setText(source);
    ui->messageIn_VM->document()->setPlainText(target);
    ui->peercoinMessageCheckbox_SM->setChecked(true);
    ui->signatureIn_VM->setFocus();
}

void SignVerifyMessageDialog::showTab_SM(bool fShow)
{
    ui->tabWidget->setCurrentIndex(0);
    if (fShow)
        this->show();
}

void SignVerifyMessageDialog::showTab_VM(bool fShow)
{
    ui->tabWidget->setCurrentIndex(1);
    if (fShow)
        this->show();
}

void SignVerifyMessageDialog::on_addressBookButton_SM_clicked()
{
    if (model && model->getAddressTableModel())
    {
        model->refresh(/*pk_hash_only=*/true);
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::ReceivingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
        {
            setAddress_SM(dlg.getReturnValue());
        }
    }
}

void SignVerifyMessageDialog::on_pasteButton_SM_clicked()
{
    setAddress_SM(QApplication::clipboard()->text());
}

void SignVerifyMessageDialog::on_signMessageButton_SM_clicked()
{
    if (!model)
        return;

    /* Clear old signature to ensure users don't get confused on error with an old signature displayed */
    ui->signatureOut_SM->clear();

    CTxDestination destination = DecodeDestination(ui->addressIn_SM->text().toStdString());
    if (!IsValidDestination(destination)) {
        ui->statusLabel_SM->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel_SM->setText(tr("The entered address is invalid.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }
    const PKHash* pkhash = std::get_if<PKHash>(&destination);
    if (!pkhash) {
        ui->addressIn_SM->setValid(false);
        ui->statusLabel_SM->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel_SM->setText(tr("The entered address does not refer to a key.") + QString(" ") + tr("Please check the address and try again."));
        return;
    }

    WalletModel::UnlockContext ctx(model->requestUnlock());
    if (!ctx.isValid())
    {
        ui->statusLabel_SM->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel_SM->setText(tr("Wallet unlock was cancelled."));
        return;
    }

    const std::string& message = ui->messageIn_SM->document()->toPlainText().toStdString();
    std::string signature;
    SigningResult res = model->wallet().signMessage(message, *pkhash, signature);

    QString error;
    switch (res) {
        case SigningResult::OK:
            error = tr("No error");
            break;
        case SigningResult::PRIVATE_KEY_NOT_AVAILABLE:
            error = tr("Private key for the entered address is not available.");
            break;
        case SigningResult::SIGNING_FAILED:
            error = tr("Message signing failed.");
            break;
        // no default case, so the compiler can warn about missing cases
    }

    if (res != SigningResult::OK) {
        ui->statusLabel_SM->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel_SM->setText(QString("<nobr>") + error + QString("</nobr>"));
        return;
    }

    ui->statusLabel_SM->setStyleSheet("QLabel { color: green; }");
    ui->statusLabel_SM->setText(QString("<nobr>") + tr("Message signed.") + QString("</nobr>"));

    ui->signatureOut_SM->setText(QString::fromStdString(signature));
}

void SignVerifyMessageDialog::on_copySignatureButton_SM_clicked()
{
    GUIUtil::setClipboard(ui->signatureOut_SM->text());
}

void SignVerifyMessageDialog::on_clearButton_SM_clicked()
{
    ui->addressIn_SM->clear();
    ui->messageIn_SM->clear();
    ui->signatureOut_SM->clear();
    ui->statusLabel_SM->clear();

    ui->addressIn_SM->setFocus();
}

void SignVerifyMessageDialog::on_addressBookButton_VM_clicked()
{
    if (model && model->getAddressTableModel())
    {
        AddressBookPage dlg(platformStyle, AddressBookPage::ForSelection, AddressBookPage::SendingTab, this);
        dlg.setModel(model->getAddressTableModel());
        if (dlg.exec())
        {
            setAddress_VM(dlg.getReturnValue());
        }
    }
}

bool SignVerifyMessageDialog::on_verifyMessageButton_VM_clicked()
{
    const std::string& address = ui->addressIn_VM->text().toStdString();
    const std::string& signature = ui->signatureIn_VM->text().toStdString();
    const std::string& message = ui->messageIn_VM->document()->toPlainText().toStdString();
    const std::string& magic = ui->peercoinMessageCheckbox_SM->checkState() == Qt::Checked ? PEERCOIN_MESSAGE_MAGIC : MESSAGE_MAGIC;

    const auto result = MessageVerify(address, signature, message, magic);

    if (result == MessageVerificationResult::OK) {
        ui->statusLabel_VM->setStyleSheet("QLabel { color: green; }");
    } else {
        ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
    }

    switch (result) {
    case MessageVerificationResult::OK:
        ui->statusLabel_VM->setText(
            QString("<nobr>") + tr("Message verified.") + QString("</nobr>")
        );
        return true;
    case MessageVerificationResult::ERR_INVALID_ADDRESS:
        ui->statusLabel_VM->setText(
            tr("The entered address is invalid.") + QString(" ") +
            tr("Please check the address and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_INVALID_TARGET_ADDRESS:
        ui->statusLabel_VM->setText(
            tr("The entered target address is invalid.") + QString(" ") +
            tr("Please check the address and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_ADDRESS_NO_KEY:
        ui->addressIn_VM->setValid(false);
        ui->statusLabel_VM->setText(
            tr("The entered address does not refer to a key.") + QString(" ") +
            tr("Please check the address and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_TARGET_ADDRESS_NO_KEY:
        ui->statusLabel_VM->setText(
            tr("The entered target address does not refer to a key.") + QString(" ") +
            tr("Please check the address and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_MALFORMED_SIGNATURE:
        ui->signatureIn_VM->setValid(false);
        ui->statusLabel_VM->setText(
            tr("The signature could not be decoded.") + QString(" ") +
            tr("Please check the signature and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_PUBKEY_NOT_RECOVERED:
        ui->signatureIn_VM->setValid(false);
        ui->statusLabel_VM->setText(
            tr("The signature did not match the message digest.") + QString(" ") +
            tr("Please check the signature and try again.")
        );
        return false;
    case MessageVerificationResult::ERR_NOT_SIGNED:
        ui->statusLabel_VM->setText(
            QString("<nobr>") + tr("Message verification failed.") + QString("</nobr>")
        );
        return false;
    default:
        return false;
    }
}

std::map<const std::string, uint64_t> debounce_input;
std::map<const CScript, uint64_t> debounce;

void SignVerifyMessageDialog::on_publishClaimButton_SM_clicked()
{
    if (m_client_model->node().context()->connman->GetNodeCount(ConnectionDirection::Both) == 0) {
        ui->statusLabel_VM->setStyleSheet("QLabel { color: orange; }");
        ui->statusLabel_VM->setText(
            QString("<nobr>") + tr("Not connected.") + QString("</nobr>")
        );
        return;
    }
    const std::string& source_address = TrimString(ui->addressIn_VM->text().toStdString());
    if (GetTimeMillis() - debounce_input[source_address] < 2000)
        return;
    const std::string& signature = TrimString(ui->signatureIn_VM->text().toStdString());
    const std::string& target_address = TrimString(ui->messageIn_VM->document()->toPlainText().toStdString());
    ui->addressIn_VM->setText(source_address.data());
    ui->signatureIn_VM->setText(signature.data());
    ui->messageIn_VM->document()->setPlainText(target_address.data());
    ui->peercoinMessageCheckbox_SM->setCheckState(Qt::Checked);
    if (!on_verifyMessageButton_VM_clicked())
        return;

    debounce_input[source_address] = GetTimeMillis();
    try {
        Claim claim(source_address, signature, target_address);
        // bool a = claim.IsSourceTargetAddress();
        // bool b = claim.IsSourceTarget();
        // bool x = claim.Commit();
        // bool d = claim.IsUnique();
        if (claim.IsSourceTargetAddress() || claim.IsSourceTarget()) {
            ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
            ui->statusLabel_VM->setText(
                QString("<nobr>") + tr("Source address cannot match output address.") + QString("</nobr>"));
            return;
        }
        if (!claim.IsValid()) {
            ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
            ui->statusLabel_VM->setText(
                QString("<nobr>") + tr("Claim invalid.") + QString("</nobr>"));
            return;
        }
        {
            LOCK2(cs_main, g_claims_mutex);
            if (claim.IsUniqueSource()) {
                // g_claims should be imperative over claimindex, as such overwrite it whenever
                if (!(claim.Insert() && g_claimindex->AddClaim(claim))) {
                    ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
                    ui->statusLabel_VM->setText(
                        QString("<nobr>") + tr("Database error.") + QString("</nobr>"));
                    return;
                }
            } else {
                const auto& it = g_claims.find(claim.GetSource());
                if (it != g_claims.end() && it->second.seen) {
                    ui->statusLabel_VM->setStyleSheet("QLabel { color: green; }");
                    ui->statusLabel_VM->setText(
                        QString("<nobr>") + tr("Already accepted.") + QString("</nobr>"));
                    return;
                }
            }
        }
        if (GetTime() - debounce[claim.GetSource()] < 2 * 60) {
            int64_t timeLeft = 2 * 60 - (GetTime() - debounce[claim.GetSource()]);
            int minutes = timeLeft / 60;
            int seconds = timeLeft % 60;

            ui->statusLabel_VM->setStyleSheet("QLabel { color: orange; }");
            QString timeText;

            if (minutes > 0) {
                timeText = QString("<nobr>") +
                           tr("Please wait %1 minute%2 and %3 second%4...")
                               .arg(minutes)
                               .arg(minutes == 1 ? "" : "s")
                               .arg(seconds)
                               .arg(seconds == 1 ? "" : "s") +
                           QString("</nobr>");
            } else {
                timeText = QString("<nobr>") +
                           tr("Please wait %1 second%2...")
                               .arg(seconds)
                               .arg(seconds == 1 ? "" : "s") +
                           QString("</nobr>");
            }
            timeText += QString("<br>") + tr("Or try a different claim.");

            ui->statusLabel_VM->setText(timeText);
            return;
        }
        debounce[claim.GetSource()] = GetTime();
        // Claim& dbClaim = claim;
        // patchcoin todo check claimset
        // if (!g_claimindex->FindClaim(claim.GetHash(), dbClaim))
        //     g_claimindex->AddClaim(claim); // patchcoin this might fail as well
        // if (dbClaim.IsValid() && dbClaim.seen) {
        //    ui->statusLabel_VM->setStyleSheet("QLabel { color: green; }");
        //    ui->statusLabel_VM->setText(
        //        QString("<nobr>") + tr("Already accepted.") + QString("</nobr>"));
        //    return;
        //}
        m_client_model->node().context()->connman->ForEachNode([&](CNode* pnode) {
            if (pnode->fDisconnect) return;
            const CNetMsgMaker msgMaker(pnode->GetCommonVersion());
            m_client_model->node().context()->connman->PushMessage(pnode, msgMaker.Make(NetMsgType::CLAIM, claim));
        });
        ui->statusLabel_VM->setStyleSheet("QLabel { color: green; }");
        ui->statusLabel_VM->setText(
            QString("<nobr>") + tr("Claim published.") + QString("</nobr>")
        );
    } catch (const std::exception& e) {
        std::cerr << "Error initializing Claim: " << e.what() << std::endl;
    }
}

void SignVerifyMessageDialog::on_clearButton_VM_clicked()
{
    ui->addressIn_VM->clear();
    ui->signatureIn_VM->clear();
    ui->messageIn_VM->clear();
    ui->statusLabel_VM->clear();
    ui->peercoinMessageCheckbox_SM->setCheckState(Qt::Unchecked);

    ui->addressIn_VM->setFocus();
}

bool SignVerifyMessageDialog::eventFilter(QObject *object, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)
    {
        if (ui->tabWidget->currentIndex() == 0)
        {
            /* Clear status message on focus change */
            ui->statusLabel_SM->clear();

            /* Select generated signature */
            if (object == ui->signatureOut_SM)
            {
                ui->signatureOut_SM->selectAll();
                return true;
            }
        }
        else if (ui->tabWidget->currentIndex() == 1)
        {
            /* Clear status message on focus change */
            ui->statusLabel_VM->clear();
        }
    }
    return QDialog::eventFilter(object, event);
}

void SignVerifyMessageDialog::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::PaletteChange) {
        ui->addressBookButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/address-book")));
        ui->pasteButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/editpaste")));
        ui->copySignatureButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/editcopy")));
        ui->signMessageButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/edit")));
        ui->clearButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/remove")));
        ui->addressBookButton_VM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/address-book")));
        ui->verifyMessageButton_VM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/transaction_0")));
        ui->publishClaimButton_SM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/send")));
        ui->clearButton_VM->setIcon(platformStyle->SingleColorIcon(QStringLiteral(":/icons/remove")));
    }

    QDialog::changeEvent(e);
}
