#ifndef RELAYWINDOW_HPP
#define RELAYWINDOW_HPP

#include <QLineEdit>
#include <QMainWindow>
#include <QPushButton>
#include <QTcpSocket>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

class RelayWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit RelayWindow(QWidget *parent = nullptr);
    bool makeConnection(const QString &user, const QString &ip, const QString &port);
    ~RelayWindow();
  signals:
    void messageReceived(const QString &message);
    void connectionDisconnected();
    void messageSent(const QString &message);
  private slots:
    void onConnectionClosed();
    void onMessageReceived();
    void sendMessage();
    void readData();

  private:
    QWidget *containerWidget;
    QVBoxLayout *layout;
    QTextEdit *messages;
    QLineEdit *inputField;
    QPushButton *sendButton;
    QString user_name, ip, port;
    QTcpSocket socket;
    bool isConnected = false;
};

#endif
