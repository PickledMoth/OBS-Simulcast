#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QCoreApplication>
#include <QString>
#include <QFileInfo>
#include <QDir>

#include "multistream-manager.h"
#include "multistream-dock.h"
#include "chat-view-dock.h"

// OBS's official installer doesn't ship any Qt TLS backend plugin (Qt
// loads TLS backends -- OpenSSL/Schannel/etc -- as plugins, not statically,
// and OBS itself never needed QSslSocket so its Qt deployment omits the
// "tls" plugin directory entirely). Without this, TwitchChatClient's
// QSslSocket fails immediately with "TLS initialization failed". We ship
// Qt's Schannel backend (native Windows TLS, no OpenSSL DLL dependency)
// under our own data dir and point Qt's plugin loader at it here.
static void RegisterBundledTlsBackend()
{
	char *dllPath = obs_module_file("tls/qschannelbackend.dll");
	if (!dllPath)
		return;

	QString path = QString::fromUtf8(dllPath);
	bfree(dllPath);

	// QCoreApplication::addLibraryPath expects the directory that
	// *contains* the "tls" plugin subfolder, not the subfolder itself --
	// Qt appends "tls" (and other plugin-type names) automatically when
	// searching. path is ".../tls/qschannelbackend.dll", so its
	// grandparent directory is what we want to add.
	QDir dir = QFileInfo(path).dir(); // .../tls
	if (!dir.cdUp())                  // .../ (module data root)
		return;

	QCoreApplication::addLibraryPath(dir.absolutePath());
}

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("OBS-Simulcast", "en-US")

static MultistreamDock *g_dock = nullptr;

// Keep the extra destinations in lockstep with OBS's own "Start/Stop
// Streaming" button so users don't have to remember a second control:
// hitting Stop Streaming in OBS also tears down YouTube/Kick/Twitch outputs.
static void OnFrontendEvent(enum obs_frontend_event event, void *)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTED:
		MultistreamManager::Get().StartAll();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
		MultistreamManager::Get().StopAll();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		MultistreamManager::Get().StopAll(true);
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		// Target configs live inside the profile directory (see
		// SettingsStore::ConfigFilePath), so a profile switch means a
		// different set of targets/keys entirely -- stop whatever was
		// running under the old profile before swapping in the new one,
		// rather than leaving stale streams pointed at the wrong account.
		MultistreamManager::Get().StopAll();
		MultistreamManager::Get().LoadFromStore();
		if (g_dock)
			g_dock->RefreshFromManager();
		break;
	default:
		break;
	}
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[OBS-Simulcast] loaded version %s", PLUGIN_VERSION);

	RegisterBundledTlsBackend();

	MultistreamManager::Get().LoadFromStore();
	obs_frontend_add_event_callback(OnFrontendEvent, nullptr);

	// Registered here rather than on FINISHED_LOADING: OBS's main window
	// restores its saved dock layout (position/floating/tab state) during
	// OBSInit, and that restore happens *before* FINISHED_LOADING fires but
	// *after* obs_module_load runs for every plugin. A dock added on
	// FINISHED_LOADING therefore always misses the restore and resets to
	// its default spot every launch, however carefully the user placed it.
	QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	g_dock = new MultistreamDock(mainWindow);
	obs_frontend_add_dock_by_id("OBS-Simulcast-dock", obs_module_text("Multistream.DockTitle"), g_dock);

	auto *chatDock = new ChatViewDock(mainWindow);
	obs_frontend_add_dock_by_id("OBS-Simulcast-chat-dock", obs_module_text("Multistream.ChatDockTitle"),
				     chatDock);
	g_dock->SetChatDock(chatDock);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	MultistreamManager::Get().StopAll(true);
	g_dock = nullptr;
	blog(LOG_INFO, "[OBS-Simulcast] unloaded");
}

const char *obs_module_name(void)
{
	return "Multistream";
}

const char *obs_module_description(void)
{
	return "Simultaneously streams to YouTube, Kick, and Twitch (and any custom RTMP target) from one OBS session.";
}
