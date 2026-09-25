#pragma once

#include <QHash>
#include <QLocalServer>
#include <QPointer>
#include <QString>

class MainWindow;
class QLocalSocket;

namespace Ipc {

struct Request {
    QString command;
    QString path;
    QString id;
};

QString endpointName(const QString &profileDirectory);
int forwardIfRunning(const QString &endpoint, const Request &request, bool *connected, QString *error);

class Server final : public QObject {
    Q_OBJECT
public:
    explicit Server(const QString &endpoint, MainWindow *window, QObject *parent = nullptr);
    bool start(QString *error);

private:
    struct Waiter { QPointer<QLocalSocket> socket; QString requestId; };
    QString endpoint_;
    MainWindow *window_;
    QLocalServer server_;
    QHash<QString, Waiter> waiters_;
    void accept();
    void process(QLocalSocket *socket, const QByteArray &payload);
    void reply(QLocalSocket *socket, const QString &requestId, const QString &status, const QString &message = {});
};

}
