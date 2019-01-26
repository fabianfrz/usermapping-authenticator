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
#include <QTextCodec>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QCloseEvent>

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

    // prefill the user field with the user found in the evironment variables
#ifdef _WIN32
    ui->username->setText(qgetenv("USERNAME"));
#else
    ui->username->setText(qgetenv("USER"));
#endif

    // system tray
    this->logoutAction = new QAction(tr("&Log Out"), this);
    exitAction = new QAction(tr("Log Out and &Exit"), this);
    connect(this->logoutAction, SIGNAL(triggered()), this, SLOT(logout()));
    connect(exitAction, SIGNAL(triggered()), this, SLOT(logoutAndClose()));
    trayMenu = new QMenu(this);
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
}
void MainWindow::closeEvent (QCloseEvent *) {
    qApp->quit();
}

void MainWindow::handleClickOnLoginBtn() {
    const QString url_string = this->ui->base_url->text().trimmed();
    if (url_string.isEmpty()) {
        return;
    }

    const QUrl tmp_url = QUrl::fromUserInput(url_string);
    if (!tmp_url.isValid()) {
        QMessageBox::information(this, tr("Error"), tr("Invalid URL: %1").arg(tmp_url.errorString()));
        return;
    }
    this->url = tmp_url;
    this->sendData();
}

void MainWindow::httpFinished(){
    // echo "HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: 18\r\n\r\n{\"error\": \"JSON\"}" | nc -l -p 3000
    QByteArray qba = reply->readAll();
    qDebug() << qba << '\n';
    if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200) {
        QJsonDocument loadDoc = QJsonDocument::fromJson(qba);
        QJsonObject const obj = loadDoc.object();
        if (obj["error"].isString()) {
            if (!this->isLoggedIn) {
                QMessageBox::critical(this, tr("Error"), obj["error"].toString());
            }
            // in case of an error, do not keep the session alive (let it expire if it still exists)
            this->expireLogin();
        }
        else {
            if (!this->isLoggedIn) {
                this->timer->start(this->ui->poll_seconds->value() * 1000);
                hide();
                // update status information
            }
        }
    }
    else {
        this->expireLogin();
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
}

void MainWindow::sendData() {

    QNetworkRequest request(url);
    QString credentials = this->ui->username->text() + ":" + this->ui->password->text();
    QByteArray credential_bytes;
    credential_bytes.append(credentials);
    request.setRawHeader("User-Agent", "OPNsense Authenticator");
    request.setRawHeader("Authorization", "Basic " + credential_bytes.toBase64());
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

    QNetworkRequest request(QUrl::fromUserInput(ui->base_url->text() + "/logout"));
    QString credentials = this->ui->username->text() + ":" + ui->password->text();
    QByteArray credential_bytes;
    credential_bytes.append(credentials);
    request.setRawHeader("User-Agent", "OPNsense Authenticator");
    request.setRawHeader("Authorization", "Basic " + credential_bytes.toBase64());
    if (reply != nullptr) {
        delete reply;
    }
    reply = qnam.get(request);
    connect(reply, &QNetworkReply::finished, this, &MainWindow::httpFinishedLogout);
}
