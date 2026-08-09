#include "platform-icons.h"

#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QBuffer>
#include <functional>

namespace {
QIcon DrawIcon(int size, const std::function<void(QPainter &, int)> &draw)
{
	QPixmap pix(size, size);
	pix.fill(Qt::transparent);
	QPainter p(&pix);
	p.setRenderHint(QPainter::Antialiasing);
	draw(p, size);
	return QIcon(pix);
}
} // namespace

QIcon MakeTwitchIcon(int size)
{
	return DrawIcon(size, [](QPainter &p, int s) {
		// Twitch brand purple rounded square with a white chat-bubble mark
		// -- an approximation of the real logo (which is a stylized glitch
		// shape), simple enough to read clearly at 14px.
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#9146FF"));
		p.drawRoundedRect(0, 0, s, s, s * 2 / 10, s * 2 / 10);

		p.setPen(QPen(Qt::white, 1.3));
		p.setBrush(Qt::NoBrush);
		p.drawRoundedRect(s * 22 / 100, s * 25 / 100, s * 56 / 100, s * 40 / 100, 1, 1);
		QPolygonF tail;
		tail << QPointF(s * 35 / 100, s * 65 / 100) << QPointF(s * 35 / 100, s * 80 / 100)
		     << QPointF(s * 50 / 100, s * 65 / 100);
		p.setPen(Qt::NoPen);
		p.setBrush(Qt::white);
		p.drawPolygon(tail);
	});
}

QIcon MakeYouTubeIcon(int size)
{
	return DrawIcon(size, [](QPainter &p, int s) {
		// YouTube's actual mark: red rounded rectangle, white play triangle.
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#FF0000"));
		p.drawRoundedRect(0, s * 15 / 100, s, s * 70 / 100, s * 25 / 100, s * 25 / 100);

		QPolygonF tri;
		tri << QPointF(s * 40 / 100, s * 32 / 100) << QPointF(s * 40 / 100, s * 68 / 100)
		    << QPointF(s * 72 / 100, s * 50 / 100);
		p.setBrush(Qt::white);
		p.drawPolygon(tri);
	});
}

QString IconToImgTag(const QIcon &icon, int size)
{
	QPixmap pix = icon.pixmap(size, size);
	QByteArray bytes;
	QBuffer buffer(&bytes);
	buffer.open(QIODevice::WriteOnly);
	pix.save(&buffer, "PNG");

	return QString("<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%2\" "
		       "style=\"vertical-align:middle;\">")
		.arg(QString::fromLatin1(bytes.toBase64()))
		.arg(size);
}
