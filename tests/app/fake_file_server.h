#pragma once
// A one-file web server for the one-click download tests: every request gets
// body back.
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

class FakeFileServer : public QObject {
public:
    QTcpServer server;
    QByteArray body;
    FakeFileServer() {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* c = server.nextPendingConnection()) {
                connect(c, &QTcpSocket::readyRead, c, [this, c] {
                    if (!c->readAll().contains("\r\n\r\n")) return;
                    c->write("HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nConnection: close\r\nContent-Length: " +
                             QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    c->disconnectFromHost();
                });
            }
        });
    }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/download.whl").arg(server.serverPort())); }
};
