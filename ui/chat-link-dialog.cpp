#include "chat-link-dialog.h"
#include "chat-url.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QClipboard>
#include <QGuiApplication>

namespace {
QString InputLabelFor(PlatformId platform)
{
	switch (platform) {
	default:
		return "Channel name:";
	}
}

QString HintFor(PlatformId platform)
{
	switch (platform) {
	case PlatformId::Twitch:
		return "Uses Twitch's chat embed with parent=obs, the value the OBS community has found Twitch "
		       "accepts for non-web embeds. If the dock shows a blank/error page instead of chat, Twitch's "
		       "embed validation may have changed.";
	case PlatformId::Kick:
		return "Best-effort guess at Kick's chat page -- not officially documented as stable the way "
		       "Twitch's/YouTube's embeds are. If it doesn't show chat, check Kick's site for a current "
		       "embed option.";
	default:
		return "";
	}
}
} // namespace

ChatLinkDialog::ChatLinkDialog(PlatformId platform, const std::string &platformName, std::string channelOrVideo,
			       QWidget *parent)
	: QDialog(parent),
	  platform_(platform),
	  channelOrVideo_(std::move(channelOrVideo))
{
	setWindowTitle(QString("Chat Link — %1").arg(QString::fromStdString(platformName)));
	resize(460, sizeHint().height());

	auto *layout = new QVBoxLayout(this);

	if (platform_ == PlatformId::YouTube) {
		auto *modeRow = new QHBoxLayout();
		channelModeRadio_ = new QRadioButton("Channel (auto-detect live video)", this);
		videoModeRadio_ = new QRadioButton("Specific video", this);
		auto *group = new QButtonGroup(this);
		group->addButton(channelModeRadio_);
		group->addButton(videoModeRadio_);
		modeRow->addWidget(channelModeRadio_);
		modeRow->addWidget(videoModeRadio_);
		modeRow->addStretch();
		layout->addLayout(modeRow);

		// Infer starting mode from any pre-filled value so reopening this
		// dialog doesn't flip a previously-set video URL back to channel
		// mode; otherwise default to channel mode, the "set once and
		// forget it" common case.
		bool looksLikeVideo = channelOrVideo_.find("watch?v=") != std::string::npos ||
				       channelOrVideo_.find("youtu.be/") != std::string::npos ||
				       channelOrVideo_.find("/live/") != std::string::npos ||
				       channelOrVideo_.find("live_chat") != std::string::npos;
		(looksLikeVideo ? videoModeRadio_ : channelModeRadio_)->setChecked(true);

		connect(channelModeRadio_, &QRadioButton::toggled, this, &ChatLinkDialog::OnModeChanged);
	}

	auto *form = new QFormLayout();
	inputLabel_ = new QLabel(this);
	inputEdit_ = new QLineEdit(QString::fromStdString(channelOrVideo_), this);
	form->addRow(inputLabel_, inputEdit_);
	layout->addLayout(form);

	layout->addWidget(new QLabel("Chat URL (paste into Docks → Custom Browser Docks in OBS):", this));

	auto *urlRow = new QHBoxLayout();
	urlEdit_ = new QLineEdit(this);
	urlEdit_->setReadOnly(true);
	urlEdit_->setPlaceholderText("(switch to \"Specific video\" above to get a link here)");
	auto *copyBtn = new QPushButton("Copy", this);
	urlRow->addWidget(urlEdit_);
	urlRow->addWidget(copyBtn);
	layout->addLayout(urlRow);

	hintLabel_ = new QLabel(this);
	hintLabel_->setWordWrap(true);
	hintLabel_->setStyleSheet("color: #9aa0a6; font-size: 11px;");
	layout->addWidget(hintLabel_);

	auto *btnRow = new QHBoxLayout();
	auto *closeBtn = new QPushButton("Close", this);
	closeBtn->setDefault(true);
	btnRow->addStretch();
	btnRow->addWidget(closeBtn);
	layout->addLayout(btnRow);

	connect(inputEdit_, &QLineEdit::textChanged, this, &ChatLinkDialog::OnInputChanged);
	connect(copyBtn, &QPushButton::clicked, this, &ChatLinkDialog::OnCopyClicked);
	connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

	UpdateLabelsForMode();
	OnInputChanged();
}

bool ChatLinkDialog::IsVideoMode() const
{
	return platform_ == PlatformId::YouTube && videoModeRadio_ && videoModeRadio_->isChecked();
}

void ChatLinkDialog::UpdateLabelsForMode()
{
	if (platform_ != PlatformId::YouTube) {
		inputLabel_->setText(InputLabelFor(platform_));
		hintLabel_->setText(HintFor(platform_));
		return;
	}

	if (IsVideoMode()) {
		inputLabel_->setText("Live video ID or URL:");
		hintLabel_->setText("Tied to one specific broadcast -- paste the watch/live URL (or bare video ID) "
				     "fresh each time you go live, not a channel URL.");
	} else {
		inputLabel_->setText("Channel handle, URL, or channel ID:");
		hintLabel_->setText("Set once -- this plugin's own chat dock/overlay auto-discovers whatever video "
				     "is currently live on this channel. The \"Chat URL\" below is for OBS's own "
				     "Custom Browser Dock instead, which can't auto-follow a channel -- switch to "
				     "\"Specific video\" above if you use that dock too.");
	}
}

void ChatLinkDialog::OnModeChanged()
{
	UpdateLabelsForMode();
	OnInputChanged(); // rebuild the Chat URL preview under the new mode's assumptions
}

void ChatLinkDialog::OnInputChanged()
{
	channelOrVideo_ = inputEdit_->text().trimmed().toStdString();

	std::string url;
	if (platform_ != PlatformId::YouTube || IsVideoMode())
		url = ChatUrl::Build(platform_, channelOrVideo_);
	// else: channel mode -- there's no single video to embed, so urlEdit_
	// is left empty and falls back to its placeholder instead.

	urlEdit_->setText(QString::fromStdString(url));
}

void ChatLinkDialog::OnCopyClicked()
{
	if (!urlEdit_->text().isEmpty())
		QGuiApplication::clipboard()->setText(urlEdit_->text());
}
