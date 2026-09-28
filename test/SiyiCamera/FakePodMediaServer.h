#pragma once

#include <memory>
#include <vector>

#include <QtCore/QDateTime>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QLocale>
#include <QtCore/QUrlQuery>
#include <QtHttpServer/QHttpServer>
#include <QtHttpServer/QHttpServerRequest>
#include <QtHttpServer/QHttpServerResponder>
#include <QtHttpServer/QHttpServerResponse>
#include <QtNetwork/QHttpHeaders>
#include <QtNetwork/QTcpServer>

/// A ZT30's media API on loopback: getdirectories and getmedialist as the pod manual (3.5.7) and
/// ArduPilot's siyi-download.py give them, and the files at the URLs the list hands out. The list
/// names the pod's factory address in those URLs, as a real pod may, so the client has to put the
/// configured address back in to reach them.
class FakePodMediaServer
{
public:
    struct File {
        QString dir;
        QString name;
        QByteArray data;
        QDateTime modified;     ///< Sent as Last-Modified
        bool stall = false;     ///< Sends half, then nothing: a download held open
    };

    QList<File> files;
    /// GETs of a file, HEADs not counted
    int fileGets = 0;

    bool listen()
    {
        _http.route(QStringLiteral("/cgi-bin/media.cgi/api/v1/getdirectories"), [this](const QHttpServerRequest &request) {
            if (request.query().queryItemValue(QStringLiteral("media_type")) != QStringLiteral("1")) {
                return QHttpServerResponse(QJsonObject{ { "code", 400 }, { "message", "Invalid media type" }, { "success", false } },
                                           QHttpServerResponse::StatusCode::BadRequest);
            }
            QJsonArray directories;
            QStringList seen;
            for (const File &file : files) {
                if (!seen.contains(file.dir)) {
                    seen.append(file.dir);
                    directories.append(QJsonObject{ { "name", file.dir.section('/', -1) }, { "path", file.dir } });
                }
            }
            return QHttpServerResponse(QJsonObject{
                { "code", 200 }, { "success", true },
                { "data", QJsonObject{ { "media_type", 1 }, { "directories", directories } } } });
        });
        _http.route(QStringLiteral("/cgi-bin/media.cgi/api/v1/getmedialist"), [this](const QHttpServerRequest &request) {
            const QUrlQuery query = request.query();
            const QString dir = query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded);
            QJsonArray list;
            for (const File &file : files) {
                if (file.dir == dir) {
                    list.append(QJsonObject{
                        { "name", file.name },
                        { "url", QStringLiteral("http://192.168.144.25:%1/video/%2/%3").arg(port()).arg(file.dir, file.name) } });
                }
            }
            return QHttpServerResponse(QJsonObject{
                { "code", 200 }, { "success", true },
                { "data", QJsonObject{ { "media_type", 1 }, { "path", dir }, { "list", list } } } });
        });
        _http.route(QStringLiteral("/video/<arg>/<arg>"),
                    [this](const QString &dir, const QString &name, const QHttpServerRequest &request, QHttpServerResponder &responder) {
            const auto found = std::find_if(files.cbegin(), files.cend(), [&](const File &file) { return file.dir == dir && file.name == name; });
            if (found == files.cend()) {
                responder.write(QHttpServerResponder::StatusCode::NotFound);
                return;
            }
            QHttpHeaders headers;
            headers.append(QHttpHeaders::WellKnownHeader::ContentType, "video/mp4");
            headers.append(QHttpHeaders::WellKnownHeader::LastModified,
                           QLocale::c().toString(found->modified.toUTC(), QStringLiteral("ddd, dd MMM yyyy hh:mm:ss 'GMT'")));
            if (request.method() == QHttpServerRequest::Method::Head) {
                headers.append(QHttpHeaders::WellKnownHeader::ContentLength, QByteArray::number(found->data.size()));
                responder.write(headers);
                return;
            }
            fileGets++;
            if (found->stall) {
                // Chunked, so no length goes with it: how far along it is comes from the listing's HEAD.
                responder.writeBeginChunked(headers);
                responder.writeChunk(found->data.left(found->data.size() / 2));
                _held.push_back(std::make_unique<QHttpServerResponder>(std::move(responder)));
                return;
            }
            responder.write(found->data, headers);
        });
        QTcpServer *const tcp = new QTcpServer(&_http);
        return tcp->listen(QHostAddress::LocalHost) && _http.bind(tcp);
    }

    quint16 port() const
    {
        return _http.servers().isEmpty() ? 0 : _http.servers().first()->serverPort();
    }

private:
    QHttpServer _http;
    std::vector<std::unique_ptr<QHttpServerResponder>> _held;
};
