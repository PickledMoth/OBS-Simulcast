#include <obs-module.h>
#include <obs-frontend-api.h>
#include <QMainWindow>
#include <QCoreApplication>
#include <QString>
#include <QFileInfo>
#include <QDir>
#include <QDockWidget>

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
static ChatViewDock *g_chatDock = nullptr;

// obs_frontend_add_dock_by_id always leaves a brand-new dock floating and
// hidden (see OBSStudioAPI::obs_frontend_add_dock_by_id in the OBS source)
// -- OBS restores whatever position it remembers for that dock's ID on
// every later launch (dock layout is saved per Windows user, not per
// profile), but the very first time a dock ID has ever existed, nothing
// remembers it, so it just sits invisible until the user finds it in the
// Docks menu. This gives it one sensible starting position instead, but
// only when it's still in that exact untouched floating+hidden state --
// the instant a user docks, moves, or closes it themselves, at least one of
// those flags changes and this becomes a permanent no-op for that dock ID,
// so a deliberately rearranged layout is never touched again.
static void PlaceDockIfUntouched(QMainWindow *mainWindow, QWidget *dockContent, Qt::DockWidgetArea area)
{
	if (!mainWindow || !dockContent)
		return;

	const auto dockWidgets = mainWindow->findChildren<QDockWidget *>();
	for (QDockWidget *dw : dockWidgets) {
		if (dw->widget() != dockContent)
			continue;
		if (dw->isFloating() && !dw->isVisible()) {
			mainWindow->addDockWidget(area, dw);
			dw->setFloating(false);
			dw->setVisible(true);
		}
		return;
	}
}

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
	case OBS_FRONTEND_EVENT_FINISHED_LOADING: {
		// Fires once OBS has finished its own startup -- main window up,
		// saved dock layout restored -- so it's safe to (a) give our docks
		// a first-ever starting position without racing OBS's own restore,
		// and (b) pop the first-run setup dialog without it appearing
		// before the main window itself is even shown.
		QMainWindow *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
		if (g_dock)
			PlaceDockIfUntouched(mainWindow, g_dock, Qt::BottomDockWidgetArea);
		if (g_chatDock)
			PlaceDockIfUntouched(mainWindow, g_chatDock, Qt::RightDockWidgetArea);
		if (g_dock)
			g_dock->RunFirstTimeSetupIfNeeded();
		break;
	}
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

	g_chatDock = new ChatViewDock(mainWindow);
	obs_frontend_add_dock_by_id("OBS-Simulcast-chat-dock", obs_module_text("Multistream.ChatDockTitle"),
				     g_chatDock);
	g_dock->SetChatDock(g_chatDock);

	return true;
}

void obs_module_unload(void)
{
	obs_frontend_remove_event_callback(OnFrontendEvent, nullptr);
	MultistreamManager::Get().StopAll(true);
	g_dock = nullptr;
	g_chatDock = nullptr;
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
