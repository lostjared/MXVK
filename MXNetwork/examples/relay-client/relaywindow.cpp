#include "relaywindow.hpp"
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <cstdlib>
#include <iostream>

RelayWindow::RelayWindow(QWidget *parent) : QMainWindow(parent) {
    containerWidget = new QWidget(this);
    layout = new QVBoxLayout(containerWidget);
    messages = new QTextEdit(this);
    inputField = new QLineEdit(this);
    sendButton = new QPushButton("Send", this);
    layout->addWidget(messages);
    layout->addWidget(inputField);
    layout->addWidget(sendButton);
    setCentralWidget(containerWidget);
    connect(inputField, &QLineEdit::returnPressed, this, &RelayWindow::sendMessage);
    connect(sendButton, &QPushButton::clicked, this, &RelayWindow::sendMessage);
    setGeometry(100, 100, 640, 480);
    connect(&socket, &QObject::destroyed, [](QObject *o) {
        qDebug() << "Destroyed object:" << qobject_cast<QTcpSocket *>(o);
    });
    connect(&socket, &QTcpSocket::readyRead, this, &RelayWindow::readData);
    messages->setReadOnly(true);
    messages->moveCursor(QTextCursor::End);
    setStyleSheet("QTextEdit, QLineEdit { background-color: #000; color: #FFFFFF; } QPushButton, QMainWindow { background-color: #CCCCCC; color: #000; }");
}

RelayWindow::~RelayWindow() {
}

bool RelayWindow::makeConnection(const QString &cuser_name, const QString &cip, const QString &cport) {
    socket.connectToHost(cip, static_cast<quint16>(cport.toUInt()));
    if (!socket.waitForConnected(3000)) {
        qWarning() << "Connection failed:" << socket.errorString();
        return false;
    }
    ip = cip;
    port = cport;
    user_name = cuser_name;
    messages->append("<span style='color: #0000FF;'>connected.</span>");
    messages->moveCursor(QTextCursor::End);
    return true;
}

static constexpr size_t BUFFER_SIZE = 1024 * 8;

void RelayWindow::readData() {
    if (socket.state() != QTcpSocket::ConnectedState) {
        return;
    }
    QByteArray data_text = socket.readAll();
    if (data_text.isEmpty()) {
        return;
    }
    QString receivedMessage = QString::fromUtf8(data_text);
    messages->append("<span style='color: #FF0000; font-size: 18px;'>" + receivedMessage + "</span>");
    messages->moveCursor(QTextCursor::End);
    emit messageReceived(receivedMessage);
}

void RelayWindow::sendMessage() {
    QString message = inputField->text();
    if (!message.isEmpty()) {
        QString final_message = user_name + ": " + message;
        messages->append("<span style='color: #00CC00; font-size: 18px;'>" + final_message + "</span>");
        messages->moveCursor(QTextCursor::End);
        if (socket.state() == QTcpSocket::ConnectedState) {
            QByteArray data_buf = final_message.toUtf8();
            qint64 bytesWritten = socket.write(data_buf);
            if (bytesWritten > 0) {
                emit messageSent(final_message);
            }
        } else {
            std::cout << "Connection is not connected\n";
            messages->append("<span style='color: red;'>Connection lost</span>");
            messages->moveCursor(QTextCursor::End);
        }
        inputField->clear();
    }
}

void RelayWindow::onMessageReceived() {
}

void RelayWindow::onConnectionClosed() {
    std::cout << "Connection closed.";
    exit(EXIT_SUCCESS);
}
