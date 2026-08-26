#include "first-run-setup-dialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QFont>

#include "platform-presets.h"

namespace {
QGroupBox *MakeKeyGroup(const char *title, QCheckBox *&enableBox, QLineEdit *&keyEdit, QWidget *parent)
{
	auto *group = new QGroupBox(title, parent);
	group->setCheckable(false);

	auto *form = new QFormLayout(group);

	enableBox = new QCheckBox("Stream to this platform", group);
	form->addRow(enableBox);

	keyEdit = new QLineEdit(group);
	keyEdit->setEchoMode(QLineEdit::Password);
	keyEdit->setPlaceholderText("Paste stream key");
	keyEdit->setEnabled(false);
	form->addRow("Stream key:", keyEdit);

	QObject::connect(enableBox, &QCheckBox::toggled, keyEdit, &QLineEdit::setEnabled);
	return group;
}
} // namespace

FirstRunSetupDialog::FirstRunSetupDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle("Welcome to OBS-Simulcast");
	setMinimumWidth(380);

	auto *layout = new QVBoxLayout(this);

	auto *intro = new QLabel(
		"Enter your Twitch and/or YouTube stream keys to get started. This saves a \"Default\" "
		"preset you can apply any time, and stream keys are stored encrypted on this machine "
		"(never uploaded anywhere). You can skip this and set everything up later from the dock.",
		this);
	intro->setWordWrap(true);
	layout->addWidget(intro);

	auto *twitchGroup = MakeKeyGroup("Twitch", twitchEnable_, twitchKey_, this);
	auto *twitchForm = qobject_cast<QFormLayout *>(twitchGroup->layout());
	twitchChannel_ = new QLineEdit(twitchGroup);
	twitchChannel_->setPlaceholderText("Channel name (for chat)");
	twitchChannel_->setEnabled(false);
	twitchForm->addRow("Channel:", twitchChannel_);
	connect(twitchEnable_, &QCheckBox::toggled, twitchChannel_, &QLineEdit::setEnabled);
	layout->addWidget(twitchGroup);

	auto *youtubeGroup = MakeKeyGroup("YouTube", youtubeEnable_, youtubeKey_, this);
	auto *youtubeForm = qobject_cast<QFormLayout *>(youtubeGroup->layout());
	youtubeChannel_ = new QLineEdit(youtubeGroup);
	youtubeChannel_->setPlaceholderText("Channel handle, e.g. @yourchannel (for chat)");
	youtubeChannel_->setEnabled(false);
	youtubeForm->addRow("Channel:", youtubeChannel_);
	connect(youtubeEnable_, &QCheckBox::toggled, youtubeChannel_, &QLineEdit::setEnabled);
	layout->addWidget(youtubeGroup);

	auto *note = new QLabel(
		"(A channel handle lets the plugin's own chat dock auto-follow your next live video. "
		"OBS's separate Custom Browser Dock embed still needs a specific video link, set later "
		"via the chat link button once you're live.)",
		this);
	note->setWordWrap(true);
	QFont noteFont = note->font();
	noteFont.setItalic(true);
	note->setFont(noteFont);
	layout->addWidget(note);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	buttons->button(QDialogButtonBox::Ok)->setText("Save and Continue");
	buttons->button(QDialogButtonBox::Cancel)->setText("Skip for now");
	connect(buttons, &QDialogButtonBox::accepted, this, &FirstRunSetupDialog::OnAccept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

void FirstRunSetupDialog::OnAccept()
{
	result_.clear();

	if (twitchEnable_->isChecked() && !twitchKey_->text().trimmed().isEmpty()) {
		const PlatformPreset *preset = FindPreset(PlatformId::Twitch);
		OutputConfig c;
		c.platform = PlatformId::Twitch;
		c.name = preset ? preset->name : "Twitch";
		c.enabled = true;
		c.server = preset ? preset->defaultServer : "";
		c.videoBitrateKbps = preset ? preset->defaultVideoBitrateKbps : c.videoBitrateKbps;
		c.audioBitrateKbps = preset ? preset->defaultAudioBitrateKbps : c.audioBitrateKbps;
		c.streamKey = twitchKey_->text().trimmed().toStdString();
		c.channelName = twitchChannel_->text().trimmed().toStdString();
		result_.push_back(std::move(c));
	}

	if (youtubeEnable_->isChecked() && !youtubeKey_->text().trimmed().isEmpty()) {
		const PlatformPreset *preset = FindPreset(PlatformId::YouTube);
		OutputConfig c;
		c.platform = PlatformId::YouTube;
		c.name = preset ? preset->name : "YouTube";
		c.enabled = true;
		c.server = preset ? preset->defaultServer : "";
		c.videoBitrateKbps = preset ? preset->defaultVideoBitrateKbps : c.videoBitrateKbps;
		c.audioBitrateKbps = preset ? preset->defaultAudioBitrateKbps : c.audioBitrateKbps;
		c.streamKey = youtubeKey_->text().trimmed().toStdString();
		c.channelName = youtubeChannel_->text().trimmed().toStdString();
		result_.push_back(std::move(c));
	}

	accept();
}
