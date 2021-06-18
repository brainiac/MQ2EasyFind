
#include <mq/Plugin.h>

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(1.0);

// Update the sort 4 times per second.
constexpr std::chrono::milliseconds distanceCalcDelay = std::chrono::milliseconds{ 250 };

class CFindLocationWnd_ControllerHook : public CXWndControllerHook
{
public:
	virtual void OnProcessFrame() override
	{
		CXWndControllerHook::OnProcessFrame();

		auto now = std::chrono::steady_clock::now();
		if (now - m_lastDistanceUpdate > distanceCalcDelay)
		{
			m_lastDistanceUpdate = now;

			UpdateDistanceColumn();
		}
	}

	virtual bool AboutToShow() override
	{
		bool result = CXWndControllerHook::AboutToShow();
		WriteChatf("CFindLocationWnd_ControllerHook::AboutToShow -> %d", result);
		return result;
	}

	virtual bool AboutToHide() override
	{
		bool result = CXWndControllerHook::AboutToHide();
		WriteChatf("CFindLocationWnd_ControllerHook::AboutToHide -> %d", result);
		return result;
	}

	virtual void WndNotification(CXWnd* sender, uint32_t message, void* data) override
	{
		WriteChatf("CFindLocationWnd_ControllerHook::WndNotification -> sender=%p message=%d data=%p",
			sender, message, data);

		if (sender == pFindLocationWnd->findLocationList)
		{
			if (message == XWM_LCLICK)
			{
			}
			else if (message == XWM_RCLICK)
			{
			}
			else if (message == XWM_COLUMNCLICK)
			{
			}
			else if (message == XWM_SORTREQUEST)
			{
				SListWndSortInfo* si = (SListWndSortInfo*)data;

				if (si->SortCol == m_distanceColumn)
				{
					si->SortResult = static_cast<int>(GetFloatFromString(si->StrLabel2, 0.0f) - GetFloatFromString(si->StrLabel1, 0.0f));
				}
			}
		}

		CXWndControllerHook::WndNotification(sender, message, data);
	}

	virtual void OnHooked() override
	{
		CListWnd* locs = pFindLocationWnd->findLocationList;

		if (locs->Columns.GetCount() == 2)
		{
			m_distanceColumn = locs->AddColumn("Distance", 60, 0, CellTypeBasicText);
			locs->SetColumnJustification(m_distanceColumn, 0);
		}
		else if (locs->Columns.GetCount() == 3 && locs->Columns[2].StrLabel == "Distance")
		{
			m_distanceColumn = 2;
		}

		UpdateDistanceColumn();
	}

	virtual void OnAboutToUnhook() override
	{
		CListWnd* locs = pFindLocationWnd->findLocationList;

		// We can't remove columns (yet), so... just clear out the third column
		if (m_distanceColumn != -1)
		{
			for (int index = 0; index < locs->ItemsArray.GetCount(); ++index)
			{
				locs->SetItemText(index, m_distanceColumn, CXStr());
			}

			locs->SetColumnLabel(2, CXStr());
		}


		m_distanceColumn = -1;
	}

	void UpdateDistanceColumn()
	{
		CListWnd* locs = pFindLocationWnd->findLocationList;
		CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };

		for (int index = 0; index < locs->ItemsArray.GetCount(); ++index)
		{
			int refIndex = (int)locs->GetItemData(index);
			CFindLocationWnd::FindableReference* ref = pFindLocationWnd->referenceList.FindFirst(refIndex);
			if (!ref)
				continue;

			CVector3 location;
			bool found = false;

			// ZoneConnections provide a direct location
			if (ref->type == FindLocation_Switch || ref->type == FindLocation_Location)
			{
				const CFindLocationWnd::FindZoneConnectionData& data = pFindLocationWnd->unfilteredZoneConnectionList[ref->index];

				location = data.location;
				found = true;
			}
			else if (ref->type == FindLocation_Player)
			{
				if (PlayerClient* player = GetSpawnByID(ref->index))
				{
					found = true;
					location = CVector3{ player->Y, player->X, player->Z };
				}
			}

			if (found)
			{
				float distance = location.GetDistance(myPos);
				char label[32];
				sprintf_s(label, 32, "%.2f", distance);

				locs->SetItemText(index, m_distanceColumn, label);
			}
			else
			{
				locs->SetItemText(index, m_distanceColumn, CXStr());
			}
		}

		// If the distance coloumn is being sorted, update it.
		if (locs->SortCol == m_distanceColumn)
		{
			locs->Sort();
		}
	}

private:
	int m_distanceColumn = -1;
	std::chrono::steady_clock::time_point m_lastDistanceUpdate;
};

CFindLocationWnd_ControllerHook* s_findWndHook = nullptr;

/**
 * @fn InitializePlugin
 *
 * This is called once on plugin initialization and can be considered the startup
 * routine for the plugin.
 */
PLUGIN_API void InitializePlugin()
{
	DebugSpewAlways("MQ2EasyFind::Initializing version %f", MQ2Version);

	s_findWndHook = new CFindLocationWnd_ControllerHook();
	if (pFindLocationWnd)
	{
		// Install observer onto the FindLocationWnd
		s_findWndHook->Hook(pFindLocationWnd);
	}
}

/**
 * @fn ShutdownPlugin
 *
 * This is called once when the plugin has been asked to shutdown.  The plugin has
 * not actually shut down until this completes.
 */
PLUGIN_API void ShutdownPlugin()
{
	DebugSpewAlways("MQ2EasyFind::Shutting down");

	delete s_findWndHook;
	s_findWndHook = nullptr;
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
	s_findWndHook->Unhook();
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
		// Install observer onto the FindLocationWnd
		s_findWndHook->Hook(pFindLocationWnd);
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
