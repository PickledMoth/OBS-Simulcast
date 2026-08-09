#include "remote-image-text-browser.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QTextDocument>
#include <QPointer>
#include <QTimer>

RemoteImageTextBrowser::RemoteImageTextBrowser(QWidget *parent) : QTextBrowser(parent)
{
	nam_ = new QNetworkAccessManager(this);
}

QVariant RemoteImageTextBrowser::loadResource(int type, const QUrl &name)
{
	if (type != QTextDocument::ImageResource || (name.scheme() != "http" && name.scheme() != "https"))
		return QTextBrowser::loadResource(type, name);

	// Qt only calls loadResource() on a cache miss in the first place --
	// calling document()->resource() here to "check the cache" doesn't
	// just redundantly re-check it, it recurses straight back into this
	// same function (resource() invokes the resource provider, which is
	// this widget's own loadResource()), infinitely, until the stack
	// overflows. Once a fetch completes, addResource() below populates
	// Qt's own cache, so it simply won't call loadResource() again for
	// that URL -- nothing to check here ourselves.
	if (!pending_.contains(name)) {
		pending_.insert(name);
		QNetworkReply *reply = nam_->get(QNetworkRequest(name));

		// QPointer rather than a raw `this`: if this widget is destroyed
		// (dock closed/recreated) while the fetch is still in flight, the
		// callback below must not touch it.
		QPointer<RemoteImageTextBrowser> self(this);

		connect(reply, &QNetworkReply::finished, this, [self, name, reply]() {
			reply->deleteLater();
			if (!self)
				return;
			self->pending_.remove(name);
			if (reply->error() != QNetworkReply::NoError)
				return;

			QImage image;
			if (!image.loadFromData(reply->readAll()) || image.isNull())
				return;

			// Deferred to the next event loop iteration rather than
			// mutated here directly: this callback can run while Qt's
			// text layout engine is still on the call stack (it's what
			// triggered the fetch via loadResource in the first place),
			// and reentering addResource()/markContentsDirty() from
			// inside that isn't safe.
			QTimer::singleShot(0, self, [self, name, image]() {
				if (!self)
					return;
				self->document()->addResource(QTextDocument::ImageResource, name, image);
				self->document()->markContentsDirty(0, self->document()->characterCount());
			});
		});
	}

	// Nothing available yet -- shows as a blank/broken image this first
	// paint, then fills in once the fetch above completes.
	return QVariant();
}
