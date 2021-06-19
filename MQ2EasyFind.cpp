
#include <mq/Plugin.h>

#include "eqlib/WindowOverride.h"

#include <optional>

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(1.0);

#define PLUGIN_MSG "\ag[MQ2EasyFind]\ax "

// Limit the rate at which we update the distance to findable locations
constexpr std::chrono::milliseconds distanceCalcDelay = std::chrono::milliseconds{ 100 };

static bool s_allowFollowPlayerPath = false;
static bool s_navNextPlayerPath = false;
static std::chrono::steady_clock::time_point s_playerPathRequestTime = {};
static bool s_performCommandFind = false;
static bool s_performGroupCommandFind = false;

bool IsNavLoaded()
{
	return GetPlugin("MQ2Nav") != nullptr;
}

bool IsNavMeshLoaded()
{
	using fNavMeshLoaded = bool(*)();

	fNavMeshLoaded isNavMeshLoaded = (fNavMeshLoaded)GetPluginProc("MQ2Nav", "IsNavMeshLoaded");
	if (isNavMeshLoaded)
	{
		return isNavMeshLoaded();
	}

	return false;
}

bool ExecuteNavCommand(const char* szLine, bool sendAsGroup)
{
	// TODO: support group command.

	using fExecuteNavCommand = bool(*)(const char*);

	fExecuteNavCommand executeNavCommand = (fExecuteNavCommand)GetPluginProc("MQ2Nav", "ExecuteNavCommand");
	if (executeNavCommand)
	{
		return executeNavCommand(szLine);
	}

	return false;
}

class CFindLocationWndOverride : public WindowOverride<CFindLocationWndOverride, CFindLocationWnd>
{
	static inline int sm_distanceColumn = -1;
	static inline std::chrono::steady_clock::time_point sm_lastDistanceUpdate;

public:
	virtual int OnProcessFrame() override
	{
		auto now = std::chrono::steady_clock::now();
		if (now - sm_lastDistanceUpdate > distanceCalcDelay)
		{
			sm_lastDistanceUpdate = now;

			UpdateDistanceColumn();
		}

		return Super::OnProcessFrame();
	}

	virtual int WndNotification(CXWnd* sender, uint32_t message, void* data) override
	{
		if (sender == findLocationList)
		{
			if (message == XWM_LCLICK)
			{
				int selectedRow = (int)data;

				// TODO: Configurable keybinds
				if (pWndMgr->IsCtrlKey() || s_performCommandFind)
				{
					bool groupNav = pWndMgr->IsShiftKey() || s_performGroupCommandFind;

					if (FindableReference* ref = GetReferenceForListIndex(selectedRow))
					{
						// Try to perform the navigation. If we succeed, bail out. Otherwise trigger the
						// navigation via player path.
						if (PerformFindWindowNavigation(ref, groupNav))
						{
							Show(false);
							return 0;
						}
					}
				}

				return Super::WndNotification(sender, message, data);
			}
			else if (message == XWM_COLUMNCLICK)
			{
				// CFindLocationWnd will proceed to override our sort with its own, so we'll just perform this
				// operation in OnProcessFrame.
				int colIndex = (int)data;
				if (colIndex == sm_distanceColumn)
				{
					findLocationList->SetSortColumn(colIndex);
					return 0;
				}
			}
			else if (message == XWM_SORTREQUEST)
			{
				SListWndSortInfo* si = (SListWndSortInfo*)data;

				if (si->SortCol == sm_distanceColumn)
				{
					si->SortResult = static_cast<int>(GetFloatFromString(si->StrLabel2, 0.0f) - GetFloatFromString(si->StrLabel1, 0.0f));
					return 0;
				}
			}
		}

		return Super::WndNotification(sender, message, data);
	}

	void UpdateDistanceColumn()
	{
		CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };

		for (int index = 0; index < findLocationList->ItemsArray.GetCount(); ++index)
		{
			FindableReference* ref = GetReferenceForListIndex(index);
			if (!ref)
				continue;

			bool found = false;
			CVector3 location = GetReferencePosition(ref, found);

			if (found)
			{
				float distance = location.GetDistance(myPos);
				char label[32];
				sprintf_s(label, 32, "%.2f", distance);

				findLocationList->SetItemText(index, sm_distanceColumn, label);
			}
			else
			{
				findLocationList->SetItemText(index, sm_distanceColumn, CXStr());
			}
		}

		// If the distance coloumn is being sorted, update it.
		if (findLocationList->SortCol == sm_distanceColumn)
		{
			findLocationList->Sort();
		}
	}

	FindableReference* GetReferenceForListIndex(int index) const
	{
		int refId = (int)findLocationList->GetItemData(index);
		CFindLocationWnd::FindableReference* ref = referenceList.FindFirst(refId);

		return ref;
	}

	CVector3 GetReferencePosition(FindableReference* ref, bool& found)
	{
		found = false;

		// TODO: Adjust this for custom positions
		if (ref->type == FindLocation_Player)
		{
			if (PlayerClient* pSpawn = GetSpawnByID(ref->index))
			{
				found = true;
				return CVector3(pSpawn->Y, pSpawn->X, pSpawn->Z);
			}
		}
		else if (ref->type == FindLocation_Location || ref->type == FindLocation_Switch)
		{
			found = true;
			const FindZoneConnectionData& zoneConn = unfilteredZoneConnectionList[ref->index];
			return zoneConn.location;
		}

		return CVector3();
	}

	// Returns true if we handled the navigation here. Returns false if we couldn't do it
	// and that we should let the path get created so we can navigate to it.
	bool PerformFindWindowNavigation(FindableReference* ref, bool asGroup)
	{
		if (!IsNavLoaded())
		{
			WriteChatf(PLUGIN_MSG "\arNavigation requires the MQ2Nav plugin to be loaded.");
			return false;
		}

		switch (ref->type)
		{
		case FindLocation_Player:
			// In the case that we are finding a spawn, then the index is actually the spawn id,
			// and we need to look it up.
			if (PlayerClient* pSpawn = GetSpawnByID(ref->index))
			{
				// TODO: Configuration adjustment

				if (pSpawn->Lastname[0] && pSpawn->Type == SPAWN_NPC)
					WriteChatf(PLUGIN_MSG "Navigating to \aySpawn\ax: \ag%s (%s)", pSpawn->Name, pSpawn->Lastname);
				else if (pSpawn->Type == SPAWN_PLAYER)
					WriteChatf(PLUGIN_MSG "Navigating to \ayPlayer:\ax \ag%s", pSpawn->Name);
				else
					WriteChatf(PLUGIN_MSG "Navigating to \aySpawn:\ax \ag%s", pSpawn->Name);

				char command[256];
				sprintf_s(command, "spawn id %d |dist=15 log=warning", ref->index);

				ExecuteNavCommand(command, asGroup);
				return true;
			}

			return false;

		case FindLocation_Location:
		case FindLocation_Switch: {
			if (ref->index >= (uint32_t)unfilteredZoneConnectionList.GetCount())
			{
				WriteChatf(PLUGIN_MSG "\arUnexpected error: zone connection index is out of range!");
				return false;
			}

			const FindZoneConnectionData& zoneConn = unfilteredZoneConnectionList[ref->index];
			char command[256];

			uint32_t switchId = 0;
			if (ref->type == FindLocation_Switch)
			{
				switchId = zoneConn.id;
			}

			char szLocationName[256];
			if (zoneConn.zoneIdentifier > 0)
				sprintf_s(szLocationName, "%s - %d", GetFullZone(zoneConn.zoneId), zoneConn.zoneIdentifier);
			else
				strcpy_s(szLocationName, GetFullZone(zoneConn.zoneId));

			CVector3 coord = zoneConn.location;

			// TODO: Configuration to fine tune this location. Maybe we want to use a switch instead, who knows?
			// GetAdjustedLocation(szLocationName, coords, pSwitch)

			EQSwitch* pSwitch = nullptr;
			if (switchId)
			{
				pSwitch = pSwitchMgr->GetSwitchById(switchId);
			}

			if (pSwitch)
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax (via switch \ao%s\ax)", szLocationName, pSwitch->Name);

				sprintf_s(command, "door id %d click |log=warning", zoneConn.id);
			}
			else
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax", szLocationName);

				// Adjust z coordinate to the ground
				coord.Z = pDisplay->GetFloorHeight(coord.X, coord.Y, coord.Z, 2.0f);

				sprintf_s(command, "locyxz %.2f %.2f %.2f |log=warning", coord.X, coord.Y, coord.Z);
			}

			ExecuteNavCommand(command, asGroup);
			return true;
		}

		default:
			WriteChatf(PLUGIN_MSG "\arCannot navigate to selection type: %d", ref->type);
			break;
		}

		return false;
	}

	void FindLocation(std::string_view searchTerm, bool group)
	{
		// TODO: Wait for zone connections.
		int foundIndex = -1;

		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			CXStr itemText = findLocationList->GetItemText(i, 1);
			if (ci_equals(itemText, searchTerm))
			{
				foundIndex = i;
				break;
			}
		}

		// if didn't find then try a substring search
		if (foundIndex == -1)
		{
			float closestDistance = FLT_MAX;
			CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };
			CXStr closest;

			for (int i = 0; i < findLocationList->GetItemCount(); ++i)
			{
				CXStr itemText = findLocationList->GetItemText(i, 1);
				if (ci_find_substr(itemText, searchTerm) != -1)
				{
					// Get distance to target.
					FindableReference* ref = GetReferenceForListIndex(i);
					if (ref)
					{
						bool found = false;
						CVector3 pos = GetReferencePosition(ref, found);
						if (found)
						{
							float distance = myPos.GetDistanceSquared(pos);
							if (distance < closestDistance)
							{
								closestDistance = distance;
								foundIndex = i;
								closest = itemText;
							}
						}
					}
				}
			}

			if (foundIndex != -1)
			{
				WriteChatf(PLUGIN_MSG "Finding closest point matching \"\ay%.*s\ax\".", searchTerm.length(), searchTerm.data());
			}
		}

		if (foundIndex == -1)
		{
			WriteChatf(PLUGIN_MSG "Could not find \"\ay%.*s\ax\".", searchTerm.length(), searchTerm.data());
			return;
		}

		// Perform navigaiton by triggering a selection in the list.
		s_performCommandFind = true;
		s_performGroupCommandFind = group;

		findLocationList->SetCurSel(foundIndex);
		findLocationList->ParentWndNotification(findLocationList, XWM_LCLICK, (void*)foundIndex);

		s_performCommandFind = false;
		s_performGroupCommandFind = false;
	}

	static void OnHooked(CFindLocationWndOverride* pWnd)
	{
		CListWnd* locs = pWnd->findLocationList;

		if (locs->Columns.GetCount() == 2)
		{
			sm_distanceColumn = locs->AddColumn("Distance", 60, 0, CellTypeBasicText);
			locs->SetColumnJustification(sm_distanceColumn, 0);
		}
		else if (locs->Columns.GetCount() == 3
			&& (locs->Columns[2].StrLabel == "Distance" || locs->Columns[2].StrLabel == ""))
		{
			sm_distanceColumn = 2;
			locs->SetColumnLabel(sm_distanceColumn, "Distance");
		}

		pWnd->UpdateDistanceColumn();
	}

	static void OnAboutToUnhook(CFindLocationWndOverride* pWnd)
	{
		CListWnd* locs = pWnd->findLocationList;

		// We can't remove columns (yet), so... just clear out the third column
		if (sm_distanceColumn != -1)
		{
			for (int index = 0; index < locs->ItemsArray.GetCount(); ++index)
			{
				locs->SetItemText(index, sm_distanceColumn, CXStr());
			}

			locs->SetColumnLabel(sm_distanceColumn, CXStr());
		}

		sm_distanceColumn = -1;
	}
};

void Command_EasyFind(SPAWNINFO* pSpawn, char* szLine)
{
	if (!pFindLocationWnd || !pLocalPlayer)
		return;

	char searchArg[MAX_STRING] = { 0 };
	GetArg(searchArg, szLine, 1);

	if (szLine[0] == 0)
	{
		WriteChatf(PLUGIN_MSG "Usage: /easyfind [search term]");
		WriteChatf(PLUGIN_MSG "    Searches the Find Window for the given text, either exact or partial match. If found, begins navigation.");
	}

	// TODO: group command support.

	pFindLocationWnd.get_as<CFindLocationWndOverride>()->FindLocation(searchArg, false);
}

/**
 * @fn InitializePlugin
 *
 * This is called once on plugin initialization and can be considered the startup
 * routine for the plugin.
 */
PLUGIN_API void InitializePlugin()
{
	DebugSpewAlways("MQ2EasyFind::Initializing version %f", MQ2Version);

	if (pFindLocationWnd)
	{
		// Install override onto the FindLocationWnd
		CFindLocationWndOverride::InstallHooks(pFindLocationWnd);
	}

	AddCommand("/easyfind", Command_EasyFind, false, true, true);
}

/**
 * @fn ShutdownPlugin
 *
 * This is called once when the plugin has been asked to shutdown.  The plugin has
 * not actually shut down until this completes.
 */
PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/easyfind");
}

/**
 * @fn OnCleanUI
 *
 * This is called once just before the shutdown of the UI system and each time the
 * game requests that the UI be cleaned.  Most commonly this happens when a
 * /loadskin command is issued, but it also occurs when reaching the character
 * select screen and when first entering the game.
 *
 * One purpose of this function is to allow you to destroy any custom windows that
 * you have created and cleanup any UI items that need to be removed.
 */
PLUGIN_API void OnCleanUI()
{
	CFindLocationWndOverride::RemoveHooks(pFindLocationWnd);
}

/**
 * @fn OnReloadUI
 *
 * This is called once just after the UI system is loaded. Most commonly this
 * happens when a /loadskin command is issued, but it also occurs when first
 * entering the game.
 *
 * One purpose of this function is to allow you to recreate any custom windows
 * that you have setup.
 */
PLUGIN_API void OnReloadUI()
{
	if (pFindLocationWnd)
	{
		CFindLocationWndOverride::InstallHooks(pFindLocationWnd);
	}
}

/**
 * @fn SetGameState
 *
 * This is called when the GameState changes.  It is also called once after the
 * plugin is initialized.
 *
 * For a list of known GameState values, see the constants that begin with
 * GAMESTATE_.  The most commonly used of these is GAMESTATE_INGAME.
 *
 * When zoning, this is called once after @ref OnBeginZone @ref OnRemoveSpawn
 * and @ref OnRemoveGroundItem are all done and then called once again after
 * @ref OnEndZone and @ref OnAddSpawn are done but prior to @ref OnAddGroundItem
 * and @ref OnZoned
 *
 * @param GameState int - The value of GameState at the time of the call
 */
PLUGIN_API void SetGameState(int GameState)
{
}

/**
 * @fn OnPulse
 *
 * This is called each time MQ2 goes through its heartbeat (pulse) function.
 *
 * Because this happens very frequently, it is recommended to have a timer or
 * counter at the start of this call to limit the amount of times the code in
 * this section is executed.
 */
PLUGIN_API void OnPulse()
{
}

/**
 * @fn OnBeginZone
 *
 * This is called just after entering a zone line and as the loading screen appears.
 */
PLUGIN_API void OnBeginZone()
{
	// DebugSpewAlways("MQ2EasyFind::OnBeginZone()");
}

/**
 * @fn OnEndZone
 *
 * This is called just after the loading screen, but prior to the zone being fully
 * loaded.
 *
 * This should occur before @ref OnAddSpawn and @ref OnAddGroundItem are called. It
 * always occurs before @ref OnZoned is called.
 */
PLUGIN_API void OnEndZone()
{
	// DebugSpewAlways("MQ2EasyFind::OnEndZone()");
}

/**
 * @fn OnZoned
 *
 * This is called after entering a new zone and the zone is considered "loaded."
 *
 * It occurs after @ref OnEndZone @ref OnAddSpawn and @ref OnAddGroundItem have
 * been called.
 */
PLUGIN_API void OnZoned()
{
	// DebugSpewAlways("MQ2EasyFind::OnZoned()");
}

/**
 * @fn OnUpdateImGui
 *
 * This is called each time that the ImGui Overlay is rendered. Use this to render
 * and update plugin specific widgets.
 *
 * Because this happens extremely frequently, it is recommended to move any actual
 * work to a separate call and use this only for updating the display.
 */
PLUGIN_API void OnUpdateImGui()
{
}

/**
 * @fn OnMacroStart
 *
 * This is called each time a macro starts (ex: /mac somemacro.mac), prior to
 * launching the macro.
 *
 * @param Name const char* - The name of the macro that was launched
 */
PLUGIN_API void OnMacroStart(const char* Name)
{
}

/**
 * @fn OnMacroStop
 *
 * This is called each time a macro stops (ex: /endmac), after the macro has ended.
 *
 * @param Name const char* - The name of the macro that was stopped.
 */
PLUGIN_API void OnMacroStop(const char* Name)
{
}

/**
 * @fn OnLoadPlugin
 *
 * This is called each time a plugin is loaded (ex: /plugin someplugin), after the
 * plugin has been loaded and any associated -AutoExec.cfg file has been launched.
 * This means it will be executed after the plugin's @ref InitializePlugin callback.
 *
 * This is also called when THIS plugin is loaded, but initialization tasks should
 * still be done in @ref InitializePlugin.
 *
 * @param Name const char* - The name of the plugin that was loaded
 */
PLUGIN_API void OnLoadPlugin(const char* Name)
{
}

/**
 * @fn OnUnloadPlugin
 *
 * This is called each time a plugin is unloaded (ex: /plugin someplugin unload),
 * just prior to the plugin unloading.  This means it will be executed prior to that
 * plugin's @ref ShutdownPlugin callback.
 *
 * This is also called when THIS plugin is unloaded, but shutdown tasks should still
 * be done in @ref ShutdownPlugin.
 *
 * @param Name const char* - The name of the plugin that is to be unloaded
 */
PLUGIN_API void OnUnloadPlugin(const char* Name)
{
}
