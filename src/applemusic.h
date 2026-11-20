#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QVariantList>

// Apple Music, through Apple's public catalog API (the iTunes Search API).
//
// What this can and cannot do, measured rather than assumed:
//
//  * SEARCH and ARTWORK work with no authentication at all. `artworkUrl100`
//    upscales by rewriting `100x100bb` to `600x600bb` -- verified: the rewritten
//    URL serves a 128 KB JPEG.
//  * PLAYBACK works for the 30-second PREVIEW Apple publishes per track
//    (`previewUrl`). Verified that mpv decodes one (44100 Hz stereo). A full
//    track cannot be streamed from here: that needs FairPlay/Widevine and a
//    licensed MusicKit token, neither of which exists outside a browser.
//  * So a track with a preview plays in-app, and one without offers its
//    music.apple.com page instead -- the window says which, it never pretends.
class AppleMusic : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString region READ region NOTIFY regionChanged)

public:
    explicit AppleMusic(QObject *parent = nullptr);
    ~AppleMusic() override;

    bool busy() const { return m_reply != nullptr; }
    QString lastError() const { return m_lastError; }
    // The storefront country, which decides what the catalog returns.
    QString region() const { return m_region; }
    void setRegion(const QString &region);

    // Pure and static so the mapping is testable without the network.
    static QVariantList parseResults(const QByteArray &json);

    Q_INVOKABLE void search(const QString &query);

signals:
    void busyChanged();
    void lastErrorChanged();
    void regionChanged();
    // Same shape as the YouTube Music provider, plus `album`, `webUrl` and
    // `playable` -- the UI does not need to know which provider a row came from.
    void tracksReady(const QVariantList &tracks);
    void failed(const QString &message);

private:
    void fail(const QString &message);

    QNetworkAccessManager m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_lastError;
    QString m_region = QStringLiteral("GB");
    QString m_pendingQuery;
};
