#pragma once

#include <QDialog>

class ChatViewDock;
class QPlainTextEdit;

// Read-only report of live Twitch/YouTube chat connection state -- what
// channel/video is configured, whether the connection actually completed,
// which IRC capabilities Twitch acknowledged, and when the last chat message
// was actually received (not just "status says Connected"). Exists because
// "status looks fine but no messages ever show up" turned out to need real
// evidence (raw IRC logs) to diagnose rather than guessing -- this surfaces
// the same signal without digging through log files each time.
class ChatDiagnosticsDialog : public QDialog {
	Q_OBJECT

public:
	explicit ChatDiagnosticsDialog(ChatViewDock *chatDock, QWidget *parent = nullptr);

private slots:
	void Refresh();
	void OnCopyClicked();

private:
	QString BuildReport() const;

	ChatViewDock *chatDock_ = nullptr;
	QPlainTextEdit *report_ = nullptr;
};
