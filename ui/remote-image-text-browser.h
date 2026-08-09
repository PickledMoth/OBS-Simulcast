#pragma once

#include <QTextBrowser>
#include <QSet>

class QNetworkAccessManager;

// QTextBrowser only resolves local/data-URI image resources out of the
// box -- a remote https:// <img> src (Twitch/YouTube emote CDN images)
// just renders as a broken-image icon unless something fetches it. This
// fetches remote images asynchronously on first reference, caches them
// into the document's resource store once downloaded, and triggers a
// relayout so they appear in place without disrupting already-rendered
// text.
class RemoteImageTextBrowser : public QTextBrowser {
	Q_OBJECT

public:
	explicit RemoteImageTextBrowser(QWidget *parent = nullptr);

protected:
	QVariant loadResource(int type, const QUrl &name) override;

private:
	QNetworkAccessManager *nam_ = nullptr;
	QSet<QUrl> pending_;
};
