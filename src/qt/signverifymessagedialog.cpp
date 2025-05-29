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
#include <QJsonDocument>
#include <QJsonObject>
#include <claimset.h>
#include <netmessagemaker.h>
#include <index/claimindex.h>
#include <interfaces/node.h>
#include <node/context.h>

static QString ClaimErrorMessage(const Claim::ClaimVerificationResult result)
{
    switch (result) {
    case Claim::ClaimVerificationResult::ERR_NOT_INITIALIZED:
        return "Claim was not properly initialized.";
    case Claim::ClaimVerificationResult::ERR_SNAPSHOT_MISMATCH:
        return "Snapshot hash does not match consensus.";
    case Claim::ClaimVerificationResult::ERR_SIZE_MISMATCH:
        return "Claim size is invalid.";
    case Claim::ClaimVerificationResult::ERR_EMPTY_FIELDS:
        return "Required fields are empty.";
    case Claim::ClaimVerificationResult::ERR_SOURCE_EQUALS_TARGET:
        return "Source address cannot match target address.";
    case Claim::ClaimVerificationResult::ERR_SOURCE_SCRIPT_NOT_FOUND:
        return "Source script not found in snapshot.";
    case Claim::ClaimVerificationResult::ERR_DECODE_SCRIPT_FAILURE:
        return "Cannot decode source/target script.";
    case Claim::ClaimVerificationResult::ERR_SIGNATURE_VERIFICATION_FAILED:
        return "Signature verification failed.";
    case Claim::ClaimVerificationResult::ERR_TX_VERIFICATION_FAILED:
        return "Incompatible claim transaction failed.";
    case Claim::ClaimVerificationResult::ERR_BALANCE_OUT_OF_RANGE:
        return "Claim balance is out of valid range.";
    case Claim::ClaimVerificationResult::ERR_RECEIVED_ABOVE_ELIGIBLE:
        return "Received more than eligible amount.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_INPUT_SIZE:
        return "Dummy tx must have exactly one input.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_OUTPUT_SIZE:
        return "Dummy tx must have exactly one output.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_VALUE_RANGE:
        return "Dummy transaction output is out of range.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_ADDRESS_MISMATCH:
        return "Dummy tx output address mismatch.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_PREVOUT_NOT_FOUND:
        return "Dummy tx previous output was not found.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_INPUT_VALUE_MISMATCH:
        return "Input value does not match output.";
    case Claim::ClaimVerificationResult::ERR_DUMMY_SIG_FAIL:
        return "Failed to verify dummy tx signature.";
    case Claim::ClaimVerificationResult::ERR_MESSAGE_INVALID_ADDRESS:
        return "Invalid address in signature message.";
    case Claim::ClaimVerificationResult::ERR_MESSAGE_DECODE_SIGNATURE:
        return "Failed to decode signature data.";
    case Claim::ClaimVerificationResult::ERR_MESSAGE_PUBKEY_RECOVERY:
        return "Could not recover public key from signature.";
    case Claim::ClaimVerificationResult::ERR_MESSAGE_NOT_SIGNED:
        return "Message was not signed by the private key.";
    default:
        return "Unknown claim error occurred.";
    }
}

SignVerifyMessageDialog::SignVerifyMessageDialog(ClientModel* client_model, const PlatformStyle *_platformStyle, QWidget *parent) :
    QDialog(parent, GUIUtil::dialog_flags),
    ui(new Ui::SignVerifyMessageDialog),
    m_client_model(client_model),
    platformStyle(_platformStyle)
{
    ui->setupUi(this);

    connect(ui->peercoinMessageCheckbox_SM, &QCheckBox::toggled,
        this, &SignVerifyMessageDialog::onCryptoCheckboxToggled);
    connect(ui->bitcoinMessageCheckbox_SM, &QCheckBox::toggled,
            this, &SignVerifyMessageDialog::onCryptoCheckboxToggled);

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
    if (IsValidDestinationString(source.toStdString(), *Params().BitcoinMain())) {
        ui->bitcoinMessageCheckbox_SM->setChecked(true);
    } else {
        ui->peercoinMessageCheckbox_SM->setChecked(true);
    }
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

void SignVerifyMessageDialog::onCryptoCheckboxToggled(bool checked)
{
    QCheckBox* sender = qobject_cast<QCheckBox*>(QObject::sender());

    if (checked && sender != nullptr) {
        if (sender == ui->peercoinMessageCheckbox_SM) {
            ui->bitcoinMessageCheckbox_SM->blockSignals(true);
            ui->bitcoinMessageCheckbox_SM->setChecked(false);
            ui->bitcoinMessageCheckbox_SM->blockSignals(false);
        } else if (sender == ui->bitcoinMessageCheckbox_SM) {
            ui->peercoinMessageCheckbox_SM->blockSignals(true);
            ui->peercoinMessageCheckbox_SM->setChecked(false);
            ui->peercoinMessageCheckbox_SM->blockSignals(false);
        }
    }
}

bool SignVerifyMessageDialog::on_verifyMessageButton_VM_clicked()
{
    const std::string& address = ui->addressIn_VM->text().toStdString();
    const std::string& signature = ui->signatureIn_VM->text().toStdString();
    const std::string& message = ui->messageIn_VM->document()->toPlainText().toStdString();
    const std::string& magic = (ui->peercoinMessageCheckbox_SM->checkState() == Qt::Checked)
        ? PEERCOIN_MESSAGE_MAGIC
        : MESSAGE_MAGIC;

    if (ui->bitcoinMessageCheckbox_SM->checkState() == Qt::Checked) {
        QNetworkAccessManager* nam = new QNetworkAccessManager(this);
        connect(nam, &QNetworkAccessManager::finished, this, &SignVerifyMessageDialog::handleVerifyReply);

        QJsonObject payload;
        payload["address"] = ui->addressIn_VM->text();
        payload["signature"] = ui->signatureIn_VM->text();
        payload["message"] = ui->messageIn_VM->document()->toPlainText();
        payload["submit"] = false;
        const QJsonDocument doc(payload);

        QNetworkRequest request(QUrl("http://bitcoin-verify.patchcoin.org/verify"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        nam->post(request, doc.toJson());
        return false;
    }

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

    debounce_input[source_address] = GetTimeMillis();
    Claim claim(source_address, target_address, signature);

    if ((!claim.m_init || claim.m_compatible) && !on_verifyMessageButton_VM_clicked()) {
        return;
    }
    // bool a = claim.IsSourceTargetAddress();
    // bool b = claim.IsSourceTarget();
    // bool x = claim.Commit();
    // bool d = claim.IsUnique();
    // if (claim.IsSourceTargetAddress() || claim.IsSourceTarget()) {
    //     ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
    //     ui->statusLabel_VM->setText(
    //         QString("<nobr>") + tr("Source address cannot match output address.") + QString("</nobr>"));
    //     return;
    // }

    ScriptError serror = SCRIPT_ERR_OK;
    const auto cvr = claim.IsValid(&serror);
    if (cvr != Claim::ClaimVerificationResult::OK) {
        ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");

        switch (cvr) {
        case Claim::ClaimVerificationResult::ERR_DECODE_SCRIPT_FAILURE:
            ui->addressIn_VM->setValid(false);
            ui->statusLabel_VM->setText(
                tr("Cannot decode source/target script.") + QString(" ") +
                tr("Please check the claim data and try again.")
            );
            return;

        case Claim::ClaimVerificationResult::ERR_SOURCE_SCRIPT_NOT_FOUND:
            ui->addressIn_VM->setValid(false);
            ui->statusLabel_VM->setText(
                tr("Source script not found in snapshot.") + QString(" ") +
                tr("Please check the claim data and try again.")
            );
            return;

        default:
            ui->statusLabel_VM->setText(
                ClaimErrorMessage(cvr) + QString(" ") +
                (serror == SCRIPT_ERR_OK
                    ? tr("Please check the claim data and try again.")
                    : QString::fromStdString(ScriptErrorString(serror)))
            );
            return;
        }
    }

    {
        LOCK2(cs_main, g_claims_mutex);
        if (claim.IsUniqueSource()) {
            if (claim.m_is_btc) {
                claim.nTime = GetTime();
            }
            // g_claims should be imperative over claimindex, as such overwrite it whenever
            if (!(claim.Insert() && g_claimindex->AddClaim(claim))) {
                ui->statusLabel_VM->setStyleSheet("QLabel { color: red; }");
                ui->statusLabel_VM->setText(
                    QString("<nobr>") + tr("Database error.") + QString("</nobr>"));
                return;
            }
        } else {
            const auto& it = g_claims.find(claim.GetSource());
            if (it != g_claims.end() && it->second.m_seen) {
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

    if (claim.m_is_btc && ui->bitcoinMessageCheckbox_SM->checkState() == Qt::Checked) {
        QNetworkAccessManager* nam = new QNetworkAccessManager(this);
        connect(nam, &QNetworkAccessManager::finished, this, &SignVerifyMessageDialog::handleVerifyReply);
        QJsonObject payload;
        payload["address"] = ui->addressIn_VM->text();
        payload["signature"] = ui->signatureIn_VM->text();
        payload["message"] = ui->messageIn_VM->document()->toPlainText();
        payload["submit"] = true;
        QJsonDocument doc(payload);
        QNetworkRequest request(QUrl("http://bitcoin-verify.patchcoin.org/verify"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        nam->post(request, doc.toJson());
        return;
    }

    if (claim.m_is_btc) {
        return;
    }

    m_client_model->node().context()->connman->ForEachNode([&](CNode* pnode) {
        if (pnode->fDisconnect) return;
        const CNetMsgMaker msgMaker(pnode->GetCommonVersion());
        m_client_model->node().context()->connman->PushMessage(pnode, msgMaker.Make(NetMsgType::CLAIM, claim));
    });

    ui->statusLabel_VM->setStyleSheet("QLabel { color: green; }");
    ui->statusLabel_VM->setText(
        QString("<nobr>") + tr("Claim published.") + QString("</nobr>")
    );
}


void SignVerifyMessageDialog::handleVerifyReply(QNetworkReply* reply)
{
    auto statusCodeVar = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    int statusCode = statusCodeVar.isValid() ? statusCodeVar.toInt() : -1;

    QByteArray responseData = reply->readAll();
    QJsonDocument jsonResponse = QJsonDocument::fromJson(responseData);
    QJsonObject obj = jsonResponse.object();

    if (statusCode >= 500) {
        showStatus(tr("Server error (%1)").arg(statusCode), false);
    } else if (statusCode >= 400) {
        QString err = obj.value("error").toString();
        showStatus(err.isEmpty() ? tr("Request failed (%1)").arg(statusCode) : err, false);
    } else if (reply->error() != QNetworkReply::NoError) {
        showStatus(tr("Network error: %1").arg(reply->errorString()), false);
    } else {
        bool initialCheck = obj.contains("claim_exists");
        bool verified = obj.contains("verified") &&  obj.value("verified").toBool();

        if (!verified) {
            QString err = obj.value("error").toString();
            showStatus(err.isEmpty() ? tr("Signature not verified") : err, false);
        } else if (initialCheck) {
            bool exists = obj.value("claim_exists").toBool();
            if (exists) {
                showStatus(tr("Claim already exists."), true);
            } else {
                displayAmount(obj, tr("Signature valid. Balance: %1 BTC"));
            }
        } else {
            displayAmount(obj, tr("Signature verified! Balance: %1 BTC"));
            if (obj.contains("electrum_output")) {
                QString eo = obj.value("electrum_output").toString();
                QString current = ui->statusLabel_VM->text();
                ui->statusLabel_VM->setText(current + "\n" + tr("%1").arg(eo));
            }
        }
    }

    reply->deleteLater();
}

void SignVerifyMessageDialog::displayAmount(const QJsonObject& obj, const QString& messageTemplate)
{
    qint64 sats = obj.value("balance").toVariant().toLongLong();
    double btc = sats / 100000000.0;
    QString btcStr = QString::number(btc, 'f', 8);
    showStatus(messageTemplate.arg(btcStr), true);
}

void SignVerifyMessageDialog::showStatus(const QString& text, bool success)
{
    ui->statusLabel_VM->setStyleSheet(
        success ? "QLabel { color: green; }" : "QLabel { color: red; }"
    );
    ui->statusLabel_VM->setText(text);
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
