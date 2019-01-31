/*
 * Copyright 2019 Fabian Franz
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions
 *    and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY
 * WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "ui_informationdialog.h"
#include <QTextCodec>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QCloseEvent>
#include <QJsonArray>
#include <QApplication>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    QIcon icon(":/img/logo.png");
    setWindowIcon(icon);
    connect(ui->pushButton,SIGNAL(clicked()),this, SLOT(handleClickOnLoginBtn()));
    isLoggedIn = false;
    exitAfterReceive = false;
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(sendData()));
    infoDlg = new InformationDialog();
    settingsFile = QApplication::applicationDirPath() + "/.config";

    // prefill the user field with the user found in the evironment variables
#ifdef _WIN32
    ui->username->setText(qgetenv("USERNAME"));
#else
    ui->username->setText(qgetenv("USER"));
#endif
    loadSettings();

    // system tray
    logoutAction = new QAction(tr("&Log Out"), this);
    exitAction = new QAction(tr("Log Out and &Exit"), this);
    showDialogAction = new QAction(tr("&Status"), this);
    connect(logoutAction, SIGNAL(triggered()), this, SLOT(logout()));
    connect(exitAction, SIGNAL(triggered()), this, SLOT(logoutAndClose()));
    connect(showDialogAction, SIGNAL(triggered()), this, SLOT(showInfoDialog()));
    trayMenu = new QMenu(this);
    trayMenu->addAction(showDialogAction);
    showDialogAction->setEnabled(false);
    trayMenu->addAction(logoutAction);
    trayMenu->addAction(exitAction);
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->setToolTip(tr("OPNsense Network Logon"));
    trayIcon->setIcon(icon);
    trayIcon->show();
}

MainWindow::~MainWindow()
{
    delete ui;
    delete trayIcon;
    delete trayMenu;
    delete exitAction;
    delete logoutAction;
    delete timer;
    delete infoDlg;
}
void MainWindow::closeEvent (QCloseEvent *) {
    qApp->quit();
}
void MainWindow::showInfoDialog() {
    infoDlg->show();
}

void MainWindow::loadSettings() {
    QSettings settings(settingsFile, QSettings::NativeFormat);
    QString text = settings.value("username", "").toString();
    if (!text.isEmpty()) {
        ui->username->setText(text);
    }
    text = settings.value("base_url", "").toString();
    if (!text.isEmpty()) {
        ui->base_url->setText(text);
    }
    text = settings.value("poll_interval", "").toString();
    if (!text.isEmpty()) {
        bool intok;
        int tmp = text.toInt(&intok);
        if (intok) {
            ui->poll_seconds->setValue(tmp);
        }
    }
    text = settings.value("validate_tls", "false").toString();
    if (!text.isEmpty()) {
        ui->validateServerCerts->setChecked(text != "false");
    }
}
void MainWindow::saveSettings() {
    QSettings settings(settingsFile, QSettings::NativeFormat);
    settings.setValue("username", ui->username->text());
    settings.setValue("base_url", ui->base_url->text());
    settings.setValue("poll_seconds", ui->poll_seconds->text());
    settings.setValue("validate_tls", ui->validateServerCerts->isChecked() ? "true" : "false");
}

void MainWindow::handleClickOnLoginBtn() {
    const QString url_string = ui->base_url->text().trimmed();
    if (url_string.isEmpty()) {
        return;
    }

    const QUrl tmp_url = QUrl::fromUserInput(url_string + "/api/usermapping/session/login");
    if (!tmp_url.isValid()) {
        QMessageBox::information(this, tr("Error"), tr("Invalid URL: %1").arg(tmp_url.errorString()));
        return;
    }
    url = tmp_url;
    sendData();
}

void MainWindow::httpFinished(){
    ui->pushButton->setEnabled(true);
    QByteArray qba = reply->readAll();
    qDebug() << qba << '\n';
    qDebug() << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() << '\n';
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
        QJsonDocument loadDoc = QJsonDocument::fromJson(qba);
        QJsonObject const obj = loadDoc.object();
        if (obj["error"].isString()) {
            if (!isLoggedIn) {
                QMessageBox::critical(this, tr("Error"), obj["error"].toString());
            }
            // in case of an error, do not keep the session alive (let it expire if it still exists)
            expireLogin();
        }
        else {
            if (!isLoggedIn) {
                timer->start(ui->poll_seconds->value() * 1000);
                hide();
                saveSettings();
                showDialogAction->setEnabled(true);
                isLoggedIn = true;
            }
            if (obj.contains("username") && obj.contains("groups") && obj.contains("valid_until") && obj.contains("ip_address"))
            {
                infoDlg->ui->username->setText(obj["username"].isString() ? obj["username"].toString() : "unknown");
                QString groups = "";
                if (obj["groups"].isArray()) {
                    QJsonArray array = obj["groups"].toArray();
                    for(int i = 0; i < array.size(); i++) {
                        if (array.at(i).isString())
                        {
                            if (i != 0) {
                                groups.append(", ");
                            }
                            groups.append(array.at(i).toString());
                        }
                    }
                    if (array.size() == 0) {
                        groups = "none";
                    }
                }
                infoDlg->ui->groups->setText(groups.isEmpty() ? "unknown" : groups);
                infoDlg->ui->valid_until->setText(obj["valid_until"].isString() ? obj["valid_until"].toString() : "unknown");
                infoDlg->ui->ip_address->setText(obj["ip_address"].isString() ? obj["ip_address"].toString() : "unknown");
                // update status information
            }
        }
    }
    else {
        expireLogin();
        QMessageBox::critical(this, tr("Error"), tr("A network error occured, your session will not be kept alive"));
    }
}

void MainWindow::httpFinishedLogout(){
    if (exitAfterReceive) {
        qApp->quit();
    }
    expireLogin();
}

void MainWindow::expireLogin() {
    isLoggedIn = false;
    show();
    timer->stop();
    showDialogAction->setEnabled(false);
}

void MainWindow::sendData() {
    ui->pushButton->setEnabled(false);
    QNetworkRequest request(url);
    QString credentials = ui->username->text() + ":" + ui->password->text();
    QByteArray credential_bytes;
    credential_bytes.append(credentials);
    request.setRawHeader("User-Agent", "OPNsense Authenticator");
    request.setRawHeader("Authorization", "Basic " + credential_bytes.toBase64());
    QSslConfiguration tlsconfig;
    tlsconfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    tlsconfig.setProtocol(QSsl::TlsV1_2);
    request.setSslConfiguration(tlsconfig);
    reply = qnam.get(request);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::httpFinished);
}

void MainWindow::logoutAndClose() {
    if (!isLoggedIn) {
        qApp->quit();
    }
    exitAfterReceive = true;
    logout();
}

void MainWindow::logout() {
    isLoggedIn = false;

    QNetworkRequest request(QUrl::fromUserInput(ui->base_url->text() + "/api/usermapping/session/logout"));
    QString credentials = ui->username->text() + ":" + ui->password->text();
    QByteArray credential_bytes;
    credential_bytes.append(credentials);
    request.setRawHeader("User-Agent", "OPNsense Authenticator");
    request.setRawHeader("Authorization", "Basic " + credential_bytes.toBase64());
    QSslConfiguration tlsconfig;
    tlsconfig.setPeerVerifyMode(ui->validateServerCerts->isChecked() ? QSslSocket::VerifyPeer : QSslSocket::VerifyNone);
    tlsconfig.setProtocol(QSsl::TlsV1_2);
    request.setSslConfiguration(tlsconfig);
    reply = qnam.get(request);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::httpFinishedLogout);
}
