#include "launcher/Ipc.h"
#include "AppIdentity.h"

#include "ui/MainWindow.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace Ipc {
namespace {
constexpr quint32 kMaxFrame = 65536;

QByteArray frame(const QJsonObject &object) {
    const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray result(4, '\0');
    const quint32 length = static_cast<quint32>(payload.size());
    result[0] = static_cast<char>(length >> 24);
    result[1] = static_cast<char>(length >> 16);
    result[2] = static_cast<char>(length >> 8);
    result[3] = static_cast<char>(length);
    return result + payload;
}

bool takeFrame(QByteArray *buffer, QByteArray *payload, bool *invalid) {
    if (buffer->size() < 4) return false;
    const auto *bytes = reinterpret_cast<const unsigned char *>(buffer->constData());
    const quint32 length = (quint32(bytes[0]) << 24) | (quint32(bytes[1]) << 16) |
                           (quint32(bytes[2]) << 8) | quint32(bytes[3]);
    if (length == 0 || length > kMaxFrame) { *invalid = true; return false; }
    if (buffer->size() < 4 + static_cast<qsizetype>(length)) return false;
    *payload = buffer->mid(4, length);
    buffer->remove(0, 4 + length);
    return true;
}
}

QString endpointName(const QString &profileDirectory) {
#ifdef Q_OS_UNIX
    const QString user = QString::number(getuid());
#else
    const QString user = qEnvironmentVariable("USERNAME", QStringLiteral("user"));
#endif
    const QByteArray hash = QCryptographicHash::hash(profileDirectory.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return QStringLiteral("promptpad-%1-%2").arg(user, QString::fromLatin1(hash));
}

int forwardIfRunning(const QString &endpoint, const Request &request, bool *connected, QString *error) {
    if (connected) *connected = false;
    QLocalSocket socket;
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(300)) return 1;
    if (connected) *connected = true;
    if (request.path.size() > 16384 || request.id.size() > 100) {
        if (error) *error = QStringLiteral("Request is too long.");
        return 2;
    }
    socket.write(frame({{QStringLiteral("cmd"), request.command},
                        {QStringLiteral("path"), request.path},
                        {QStringLiteral("id"), request.id}}));
    if (!socket.waitForBytesWritten(5000)) {
        if (error) *error = socket.errorString();
        return 2;
    }
    QByteArray buffer;
    for (;;) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(request.command == QStringLiteral("wait") ? 30000 : 5000)) {
            if (socket.state() != QLocalSocket::ConnectedState || request.command != QStringLiteral("wait")) {
                if (error) *error = QStringLiteral("The editor did not complete the request.");
                return 2;
            }
            continue;
        }
        buffer.append(socket.readAll());
        QByteArray payload;
        bool invalid = false;
        while (takeFrame(&buffer, &payload, &invalid)) {
            QJsonParseError parseError;
            const auto reply = QJsonDocument::fromJson(payload, &parseError).object();
            if (parseError.error != QJsonParseError::NoError || reply.value(QStringLiteral("id")).toString() != request.id) {
                if (error) *error = QStringLiteral("Invalid editor response.");
                return 2;
            }
            const QString status = reply.value(QStringLiteral("status")).toString();
            if (status == QStringLiteral("accepted") && request.command == QStringLiteral("wait")) continue;
            if (status == QStringLiteral("ok")) return 0;
            if (error) *error = reply.value(QStringLiteral("message")).toString(QStringLiteral("The request was cancelled."));
            return 2;
        }
        if (invalid || buffer.size() > kMaxFrame + 4) {
            if (error) *error = QStringLiteral("Invalid editor response length.");
            return 2;
        }
    }
}

Server::Server(const QString &endpoint, MainWindow *window, QObject *parent)
    : QObject(parent), endpoint_(endpoint), window_(window) {
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, &Server::accept);
    connect(window_, &MainWindow::waitFinished, this, [this](const QString &id, bool success) {
        const auto waiter = waiters_.take(id);
        if (waiter.socket) {
            reply(waiter.socket, waiter.requestId, success ? QStringLiteral("ok") : QStringLiteral("cancelled"));
            waiter.socket->flush();
            waiter.socket->disconnectFromServer();
        }
    });
}

bool Server::start(QString *error) {
    if (server_.listen(endpoint_)) return true;
    QLocalSocket probe;
    probe.connectToServer(endpoint_);
    if (probe.waitForConnected(250)) {
        if (error) *error = QStringLiteral("Another %1 instance is already running.").arg(AppIdentity::displayName());
        return false;
    }
    QLocalServer::removeServer(endpoint_);
    if (server_.listen(endpoint_)) return true;
    if (error) *error = server_.errorString();
    return false;
}

void Server::accept() {
    while (server_.hasPendingConnections()) {
        auto *socket = server_.nextPendingConnection();
        socket->setParent(this);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
            if (socket->property("promptpadSeen").toBool()) { socket->disconnectFromServer(); return; }
            QByteArray buffer = socket->property("promptpadBuffer").toByteArray();
            buffer.append(socket->readAll());
            QByteArray payload;
            bool invalid = false;
            if (takeFrame(&buffer, &payload, &invalid)) {
                socket->setProperty("promptpadSeen", true);
                process(socket, payload);
            }
            if (invalid || buffer.size() > kMaxFrame + 4 ||
                (socket->property("promptpadSeen").toBool() && !buffer.isEmpty())) {
                socket->disconnectFromServer();
            }
            socket->setProperty("promptpadBuffer", buffer);
        });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            for (auto it = waiters_.begin(); it != waiters_.end();) {
                if (it->socket == socket) {
                    window_->cancelWait(it.key());
                    it = waiters_.erase(it);
                } else ++it;
            }
            socket->deleteLater();
        });
    }
}

void Server::reply(QLocalSocket *socket, const QString &requestId, const QString &status, const QString &message) {
    if (socket && socket->state() == QLocalSocket::ConnectedState)
        socket->write(frame({{QStringLiteral("id"), requestId},
                             {QStringLiteral("status"), status},
                             {QStringLiteral("message"), message}}));
}

void Server::process(QLocalSocket *socket, const QByteArray &payload) {
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
        reply(socket, {}, QStringLiteral("error"), QStringLiteral("Invalid request."));
        socket->disconnectFromServer(); return;
    }
    const QJsonObject object = parsed.object();
    const QString command = object.value(QStringLiteral("cmd")).toString();
    const QString path = object.value(QStringLiteral("path")).toString();
    const QString requestId = object.value(QStringLiteral("id")).toString();
    if (requestId.isEmpty() || requestId.size() > 100 || path.size() > 16384 ||
        !QStringList{QStringLiteral("activate"), QStringLiteral("new"), QStringLiteral("open"), QStringLiteral("wait")}.contains(command) ||
        ((command == QStringLiteral("open") || command == QStringLiteral("wait")) && path.isEmpty())) {
        reply(socket, requestId, QStringLiteral("error"), QStringLiteral("Invalid request."));
        socket->disconnectFromServer(); return;
    }
    QString documentId;
    if (command == QStringLiteral("new")) documentId = window_->newPrompt();
    else if (command == QStringLiteral("open") || command == QStringLiteral("wait"))
        documentId = window_->openPath(path, command == QStringLiteral("wait"));
    if ((command == QStringLiteral("open") || command == QStringLiteral("wait")) && documentId.isEmpty()) {
        reply(socket, requestId, QStringLiteral("error"), QStringLiteral("Could not open the file."));
        socket->disconnectFromServer(); return;
    }
    window_->showNormal(); window_->raise(); window_->activateWindow();
    if (command == QStringLiteral("wait")) {
        if (!window_->attachWait(documentId)) {
            reply(socket, requestId, QStringLiteral("error"), QStringLiteral("Another waiting caller owns this document."));
            socket->disconnectFromServer(); return;
        }
        waiters_.insert(documentId, {socket, requestId});
        reply(socket, requestId, QStringLiteral("accepted"));
        socket->flush();
    } else {
        reply(socket, requestId, QStringLiteral("ok"));
        socket->flush();
        socket->disconnectFromServer();
    }
}

}
