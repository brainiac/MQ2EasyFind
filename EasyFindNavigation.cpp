
#include "EasyFind.h"
#include "EasyFindConfiguration.h"

#include "MQ2Nav/PluginAPI.h"

static nav::NavAPI* s_nav = nullptr;
static int s_navObserverId = 0;

FindLocationRequestState g_activeNavigationState;

//----------------------------------------------------------------------------

bool Navigation_ExecuteCommand(FindLocationRequestState&& request)
{
	if (!s_nav)
		return false;

	g_activeNavigationState = std::move(request);
	g_activeNavigationState.pending = true;

	std::string command;
	fmt::string_view logLevel = spdlog::level::to_string_view(g_configuration->GetNavLogLevel());

	if (g_activeNavigationState.type == FindLocation_Player)
	{
		// nav by spawnID:
		command = fmt::format("spawn id {} | dist={} log={} tag=easyfind", g_activeNavigationState.spawnID,
			g_configuration->GetNavDistance(), logLevel);
	}
	else if (g_activeNavigationState.type == FindLocation_Switch || g_activeNavigationState.type == FindLocation_Location)
	{
		if (g_activeNavigationState.findableLocation)
		{
			if (!g_activeNavigationState.findableLocation->spawnName.empty())
			{
				command = fmt::format("spawn {} | dist={} log={} tag=easyfind",
					g_activeNavigationState.findableLocation->spawnName, g_configuration->GetNavDistance(), logLevel);
			}
		}

		if (command.empty())
		{
			if (g_activeNavigationState.location != glm::vec3())
			{
				glm::vec3 loc = g_activeNavigationState.location;

				float newHeight = pDisplay->GetFloorHeight(loc.x, loc.y, loc.z, 2.0f);
				if (newHeight > -32000.f)
					loc.z = newHeight;

				command = fmt::format("locyxz {:.2f} {:.2f} {:.2f} log={} tag=easyfind", loc.x, loc.y, loc.z, logLevel);

				if (g_activeNavigationState.type == FindLocation_Switch)
					g_activeNavigationState.activateSwitch = true;
			}
			else if (g_activeNavigationState.type == FindLocation_Switch)
			{
				command = fmt::format("door id {} click log={} tag=easyfind", g_activeNavigationState.switchID, logLevel);
			}
		}
	}

	if (command[0] != 0)
	{
		g_activeNavigationState.navCommand = std::move(command);
		s_nav->ExecuteNavCommand(g_activeNavigationState.navCommand);
		return true;
	}

	return false;
}

bool Navigation_ExecuteCommand(std::string_view navCommand)
{
	if (!s_nav)
		return false;

	g_activeNavigationState = {};

	if (!navCommand.empty())
	{
		g_activeNavigationState.pending = true;

		g_activeNavigationState.navCommand = std::string{ navCommand };
		s_nav->ExecuteNavCommand(g_activeNavigationState.navCommand);
		return true;
	}

	return false;
}

void Navigation_Stop()
{
	if (!s_nav)
		return;

	s_nav->ExecuteNavCommand("stop log=off");
}

void NavObserverCallback(nav::NavObserverEvent eventType, const nav::NavCommandState& commandState, void* userData)
{
	const char* eventName = "Unknown";
	switch (eventType)
	{
	case nav::NavObserverEvent::NavCanceled: eventName = "CANCELED"; break;
	case nav::NavObserverEvent::NavPauseChanged: eventName = "PAUSED"; break;
	case nav::NavObserverEvent::NavStarted: eventName = "STARTED"; break;
	case nav::NavObserverEvent::NavDestinationReached: eventName = "DESTINATIONREACHED"; break;
	case nav::NavObserverEvent::NavFailed: eventName = "FAILED"; break;
	default: break;
	}

	SPDLOG_DEBUG("Nav Observer: event=\ag{}\ax tag=\ag{}\ax paused=\ag{}\ax destination=\ag({:.2f}, {:.2f}, {:.2f})\ax type=\ag{}\ax", eventName,
		commandState.tag, commandState.paused, commandState.destination.x, commandState.destination.y, commandState.destination.z,
		commandState.type);

	if (commandState.tag != "easyfind")
		return;

	if (eventType == nav::NavObserverEvent::NavStarted)
	{
		g_activeNavigationState.valid = true;
		g_activeNavigationState.pending = false;
	}
	else if (eventType == nav::NavObserverEvent::NavDestinationReached)
	{
		if (g_activeNavigationState.valid)
		{
			// Determine if we have extra steps to perform once we reach the destination.
			if (g_activeNavigationState.activateSwitch)
			{
				SPDLOG_DEBUG("Activating switch: \ag{}", g_activeNavigationState.switchID);

				EQSwitch* pSwitch = GetSwitchByID(g_activeNavigationState.switchID);
				if (pSwitch)
				{
					pSwitch->UseSwitch(pLocalPlayer->SpawnID, -1, 0, nullptr);
				}
			}

			if (g_activeNavigationState.findableLocation && !g_activeNavigationState.findableLocation->luaScript.empty())
			{
				ExecuteLuaScript(g_activeNavigationState.findableLocation->luaScript, g_activeNavigationState.findableLocation);
			}

			g_activeNavigationState.valid = false;
		}
	}
	else if (eventType == nav::NavObserverEvent::NavCanceled)
	{
		if (g_activeNavigationState.valid)
		{
			// Need to find a better way to do this.
			if (gZoning)
			{
				// We canceled because we are zoning. Get the new zone and determine that we went to the
				// right location.

				int zoneId = pLocalPC ? pWorldData->GetZoneBaseId(pLocalPC->zoneId) : -1;
				SPDLOG_DEBUG("NavCanceled because of zoning. New zoneId: {}, we are expecting: {}", zoneId, g_activeNavigationState.zoneId);

				if (g_activeNavigationState.zoneId == zoneId)
					return;
			}

			g_activeNavigationState.valid = false;

			ZonePath_NavCanceled(true);
		}
	}
	else if (eventType == nav::NavObserverEvent::NavFailed)
	{
		if (g_activeNavigationState.pending)
		{
			g_activeNavigationState.valid = false;
			g_activeNavigationState.pending = false;

			SPDLOG_ERROR("Failed to find a path to the destination: \ay{}", g_activeNavigationState.name);
			ZonePath_NavCanceled(false);
		}
	}
}

void Navigation_Initialize()
{
	s_nav = (nav::NavAPI*)GetPluginInterface("MQ2Nav");
	if (s_nav)
	{
		s_navObserverId = s_nav->RegisterNavObserver(NavObserverCallback, nullptr);
	}
}

void Navigation_Shutdown()
{
	if (s_nav)
	{
		s_nav->UnregisterNavObserver(s_navObserverId);
		s_navObserverId = 0;
	}

	s_nav = nullptr;
}

void Navigation_Zoned()
{
	// Clear all local navigation state (anything not meant to carry over to the next zone)
	g_activeNavigationState.valid = {};
}

void Navigation_Reset()
{
	// Clear all existing navigation state
	g_activeNavigationState = {};
}

void Navigation_BeginZone()
{
	// TODO: Zoning while nav is active counts as a cancel, but maybe it shouldn't.
	g_activeNavigationState.valid = false;
	g_activeNavigationState.pending = false;
}

bool Navigation_IsInitialized()
{
	return s_nav != nullptr;
}
