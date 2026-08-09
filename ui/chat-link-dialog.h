#pragma once

#include <QDialog>
#include <string>

#include "platform-presets.h"

class QLineEdit;
class QLabel;
class QRadioButton;

// Small dialog for one row's chat link: lets the user enter a channel name
// (or, for YouTube, a choice of channel handle/URL/ID vs. a specific video
// ID/URL) and shows the resulting chat-embed URL with a Copy button. OBS has
// no public API to add a Custom Browser Dock programmatically, so this can't
// add the dock itself -- it just gets the user to a correct URL to paste
// into Docks -> Custom Browser Docks themselves.
//
// For YouTube, the entered value also feeds YouTubeChatClient (this
// plugin's own chat dock/overlay), which happily auto-discovers the live
// video from just a channel handle -- but OBS's Custom Browser Dock has no
// such auto-follow concept and needs one specific video's URL/ID. The
// channel/video radio toggle exists to make that distinction explicit
// rather than guessing it from the input's shape.
class ChatLinkDialog : public QDialog {
	Q_OBJECT

public:
	ChatLinkDialog(PlatformId platform, const std::string &platformName, std::string channelOrVideo,
		       QWidget *parent = nullptr);

	// Empty if the user never entered anything valid.
	const std::string &ChannelOrVideo() const { return channelOrVideo_; }

private slots:
	void OnInputChanged();
	void OnModeChanged();
	void OnCopyClicked();

private:
	void UpdateLabelsForMode();
	bool IsVideoMode() const;

	PlatformId platform_;
	std::string channelOrVideo_;

	QLabel *inputLabel_ = nullptr;
	QLineEdit *inputEdit_ = nullptr;
	QLineEdit *urlEdit_ = nullptr;
	QLabel *hintLabel_ = nullptr;

	// Only constructed for YouTube -- Twitch/Kick have just one concept of
	// "channel name" with no video/channel distinction to toggle between.
	QRadioButton *channelModeRadio_ = nullptr;
	QRadioButton *videoModeRadio_ = nullptr;
};
