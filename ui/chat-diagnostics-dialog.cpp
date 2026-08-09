#include "chat-diagnostics-dialog.h"
#include "chat-view-dock.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QClipboard>
#include <QGuiApplication>
#include <QDateTime>

namespace {
QString FormatAgo(const QDateTime &when)
{
	if (!when.isValid())
		return "never";

	qint64 secs = when.secsTo(QDateTime::currentDateTime());
	if (secs < 2)
		return "just now";
	if (secs < 60)
		return QString("%1s ago").arg(secs);
	if (secs < 3600)
		return QString("%1m %2s ago").arg(secs / 60).arg(secs % 60);
	return QString("%1h %2m ago").arg(secs / 3600).arg((secs % 3600) / 60);
}
} // namespace

ChatDiagnosticsDialog::ChatDiagnosticsDialog(ChatViewDock *chatDock, QWidget *parent)
	: QDialog(parent),
	  chatDock_(chatDock)
{
	setWindowTitle("Chat Diagnostics");
	resize(560, 480);

	auto *layout = new QVBoxLayout(this);

	auto *introLabel = new QLabel(
		"Live connection state for Twitch/YouTube chat -- if messages aren't showing up in the chat dock "
		"despite this saying \"Connected\", check whether a message has actually ever been received below. "
		"\"Connected\" only means the network handshake succeeded, not that the platform is relaying chat.",
		this);
	introLabel->setWordWrap(true);
	introLabel->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	layout->addWidget(introLabel);

	report_ = new QPlainTextEdit(this);
	report_->setReadOnly(true);
	report_->setStyleSheet("QPlainTextEdit { font-family: Consolas, monospace; font-size: 12px; }");
	layout->addWidget(report_, /*stretch=*/1);

	auto *btnRow = new QHBoxLayout();
	auto *refreshBtn = new QPushButton("Refresh", this);
	auto *copyBtn = new QPushButton("Copy Report", this);
	auto *closeBtn = new QPushButton("Close", this);
	closeBtn->setDefault(true);
	connect(refreshBtn, &QPushButton::clicked, this, &ChatDiagnosticsDialog::Refresh);
	connect(copyBtn, &QPushButton::clicked, this, &ChatDiagnosticsDialog::OnCopyClicked);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
	btnRow->addWidget(refreshBtn);
	btnRow->addWidget(copyBtn);
	btnRow->addStretch();
	btnRow->addWidget(closeBtn);
	layout->addLayout(btnRow);

	Refresh();
}

QString ChatDiagnosticsDialog::BuildReport() const
{
	QStringList lines;
	lines << QString("Generated: %1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
	lines << "";

	if (!chatDock_) {
		lines << "Chat dock is unavailable.";
		return lines.join('\n');
	}

	lines << "=== Twitch ===";
	auto *twitch = chatDock_->TwitchClient();
	if (!twitch) {
		lines << "  (not initialized)";
	} else {
		lines << QString("  Channel:              %1")
				 .arg(twitch->CurrentChannel().isEmpty() ? "(none configured)" : twitch->CurrentChannel());
		lines << QString("  Status:               %1").arg(twitch->LastStatus());
		lines << QString("  Socket connected:     %1").arg(twitch->IsConnected() ? "yes" : "no");
		QStringList caps = twitch->AckedCapabilities();
		lines << QString("  IRC capabilities ACKed: %1").arg(caps.isEmpty() ? "(none yet)" : caps.join(", "));
		lines << QString("  Messages received:    %1").arg(twitch->MessagesReceivedCount());
		lines << QString("  Last message:         %1").arg(FormatAgo(twitch->LastMessageAt()));
		lines << QString("  Viewer count:         %1")
				 .arg(chatDock_->TwitchViewerCount() < 0 ? "unknown/offline"
								  : QString::number(chatDock_->TwitchViewerCount()));
	}

	lines << "";
	lines << "=== YouTube ===";
	auto *youtube = chatDock_->YouTubeClient();
	if (!youtube) {
		lines << "  (not initialized)";
	} else {
		lines << QString("  Live video ID:        %1")
				 .arg(youtube->CurrentVideoId().isEmpty() ? "(none -- not live, or not configured)"
									   : youtube->CurrentVideoId());
		lines << QString("  Status:               %1").arg(youtube->LastStatus());
		lines << QString("  Messages received:    %1").arg(youtube->MessagesReceivedCount());
		lines << QString("  Last message:         %1").arg(FormatAgo(youtube->LastMessageAt()));
		lines << QString("  Viewer count:         %1")
				 .arg(chatDock_->YouTubeViewerCount() < 0
					      ? "unknown/offline"
					      : QString::number(chatDock_->YouTubeViewerCount()));
	}

	lines << "";
	lines << "=== Overlay ===";
	QString overlayUrl = chatDock_->OverlayUrl();
	lines << QString("  Browser Source URL:   %1").arg(overlayUrl.isEmpty() ? "(server not running)" : overlayUrl);

	return lines.join('\n');
}

void ChatDiagnosticsDialog::Refresh()
{
	report_->setPlainText(BuildReport());
}

void ChatDiagnosticsDialog::OnCopyClicked()
{
	QGuiApplication::clipboard()->setText(report_->toPlainText());
}
