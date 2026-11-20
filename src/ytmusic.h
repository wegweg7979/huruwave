#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QVariantList>

// YouTube Music browsing.
//
// Two engines, chosen per job -- measured, not guessed:
//
//  * SEARCH goes to YouTube Music's own inner API (the WEB_REMIX client), the
//    same one ytmusicapi and cliamp use. One POST returns real songs with artist
//    and duration. The alternatives were both wrong for a search box: yt-dlp
//    with --flat-playlist returns album browse-ids with `NA` titles, and without
//    it resolves every result (measured: 1m46s for six entries).
//  * PLAYLISTS and arbitrary URLs stay with yt-dlp, which handles pagination,
//    cookies and the account's own playlists properly.
//
// The API credentials (key + client version) are read from music.youtube.com at
// runtime and cached, because they rotate.
class YtMusic : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString cookieSource READ cookieSource NOTIFY cookieSourceChanged)
    // "file", "browser" or empty -- the UI says which kind it is rather than
    // showing a raw path, which reads like debug output.
    Q_PROPERTY(QString cookieKind READ cookieKind NOTIFY cookieSourceChanged)
    // What the cookie source can actually do, not merely which one was found.
    Q_PROPERTY(QString cookieStatus READ cookieStatus NOTIFY cookieSourceChanged)

public:
    explicit YtMusic(QObject *parent = nullptr, const QString &binary = {});
    ~YtMusic() override;

    bool busy() const { return m_ytdlp.state() != QProcess::NotRunning || m_searchReply != nullptr; }
    QString lastError() const { return m_lastError; }

    // What the resolved cookie source can actually do. A cookies file that
    // contains no session cookie is not a signed-in session, and the UI must not
    // say it is: the exported file on this machine held only visitor and rollout
    // cookies while every `SID`/`__Secure-1PSID` had been dropped, and "signed in
    // - cookie file" was the resulting false claim. Names only -- values are
    // secrets and are never read here.
    static QString describeCookieSource(const QString &source, const QString &kind);
    QString cookieSource() const { return m_cookieSource; }
    QString cookieKind() const { return m_cookieKind; }
    QString cookieStatus() const { return describeCookieSource(m_cookieSource, m_cookieKind); }

    // Order of preference, and why:
    //   1. this app's own config, because it is an explicit choice;
    //   2. a Netscape cookies file, but only when it actually carries a session --
    //      a file of visitor cookies must not shadow a working browser;
    //   3. Firefox (rpm or flatpak), which needs no keyring at all;
    //   4. the session-less cookies file, if nothing better exists;
    //   5. cliamp's own setting, so an existing setup is inherited.
    static QString resolveCookieSource(QString *kind);

    // Parses a WEB_REMIX search response. Static and pure so it can be tested
    // against a saved payload with no network.
    static QVariantList parseSearchResponse(const QByteArray &json);

    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void loadPlaylist(const QString &playlistId);
    Q_INVOKABLE void loadUrl(const QString &url);

signals:
    void busyChanged();
    void lastErrorChanged();
    void cookieSourceChanged();
    // One entry per track: { id, title, artist, duration, thumbnail, url }.
    void tracksReady(const QVariantList &tracks);
    void failed(const QString &message);

private:
    void fail(const QString &message);
    void fetchCredentials();
    void performSearch(const QString &query);
    void runYtDlp(const QStringList &extraArguments, const QString &what);
    QStringList cookieArguments() const;

    QString m_binary;
    QProcess m_ytdlp;
    QByteArray m_ytdlpOutput;
    QNetworkAccessManager m_network;
    QNetworkReply *m_searchReply = nullptr;
    QString m_credentialsKey;
    QString m_clientVersion;
    QString m_pendingQuery;
    QString m_lastError;
    QString m_cookieSource;
    QString m_cookieKind;
    QString m_currentRequest;
};
