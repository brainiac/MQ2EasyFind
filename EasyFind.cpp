
#include "EasyFind.h"
#include "EasyFindConfiguration.h"
#include "EasyFindZoneConnections.h"

#include "plugins/lua/LuaInterface.h"

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(0.7);

#define EASYFIND_PLUGIN_VERSION "0.7.1"

static mq::lua::LuaPluginInterface* s_lua = nullptr;

//============================================================================

const char* LocationTypeToString(LocationType type)
{
	switch (type)
	{
	case LocationType::Location: return "Location";
	case LocationType::Switch: return "Switch";
	case LocationType::Translocator: return "Translocator";
	case LocationType::Unknown:
	default: return "Unknown";
	}
}

SPAWNINFO* FindSpawnByName(const char* spawnName, bool exact)
{
	MQSpawnSearch SearchSpawn;
	ClearSearchSpawn(&SearchSpawn);

	SearchSpawn.bExactName = exact;
	strcpy_s(SearchSpawn.szName, spawnName);

	SPAWNINFO* pSpawn = SearchThroughSpawns(&SearchSpawn, pLocalPlayer);

	return pSpawn;
}

//============================================================================

void AddFindableLocationLuaBindings(sol::state_view sv)
{
	// todo: these should be moved to mq2lua

	sv.new_usertype<glm::vec3>(
		"vec3", sol::no_constructor,
		"x", &glm::vec3::x,
		"y", &glm::vec3::y,
		"z", &glm::vec3::z);

	sv.new_enum("LocationType",
		"Unknown", LocationType::Unknown,
		"Location", LocationType::Location,
		"Switch", LocationType::Switch,
		"Translocator", LocationType::Translocator);

	sv.new_usertype<FindableLocation>(
		"FindableLocation", sol::no_constructor,
		"type", sol::readonly(&FindableLocation::easyfindType),
		"name", sol::property([](const FindableLocation& l) -> std::string { return std::string(l.name); }), // todo: expose CXStr
		"zoneId", sol::readonly(&FindableLocation::zoneId),
		"zoneIdentifier", sol::readonly(&FindableLocation::zoneIdentifier),
		"switchId", sol::readonly(&FindableLocation::switchId),
		"switchName", sol::readonly(&FindableLocation::switchName),
		"spawnName", sol::readonly(&FindableLocation::spawnName),
		"translocatorKeyword", sol::readonly(&FindableLocation::translocatorKeyword)
		);
}

void ExecuteLuaScript(std::string_view luaScript, const std::shared_ptr<FindableLocation>& findableLocation)
{
	if (s_lua)
	{
		SPDLOG_DEBUG("Executing lua script");

		mq::lua::LuaScriptPtr threadPtr = s_lua->CreateLuaScript();
		s_lua->InjectMQNamespace(threadPtr);

		// Add bindings about our findable location.
		sol::state_view sv = s_lua->GetLuaState(threadPtr);
		AddFindableLocationLuaBindings(sv);
		sv.set("location", findableLocation);

		s_lua->ExecuteString(threadPtr, findableLocation->luaScript, "easyfind");
	}
	else
	{
		SPDLOG_WARN("Cannot run script because Lua is not loaded: Unable to complete navigation.");
	}
}

void Lua_Initialize()
{
	s_lua = (mq::lua::LuaPluginInterface*)GetPluginInterface("MQ2Lua");
}

void Lua_Shutdown()
{
	s_lua = nullptr;
}

//============================================================================
//============================================================================

void ShowHelp()
{
	WriteChatf(PLUGIN_MSG "EasyFind Usage:");
	WriteChatf(PLUGIN_MSG "\ag/easyfind \ao[search term]");
	WriteChatf(PLUGIN_MSG "\ag/easyfind \aygroup\ax \ao[search term]");
	WriteChatf(PLUGIN_MSG "    Searches the Find Window for the given \ao[search term]\ax, either exact or partial match. If found, begins navigation. "
		"\ao[search term]\ax may also be a zone shortname. In this case, the closest zone connection matching for that zone will be found. "
		"If \aggroup\ax is specified, the command will attempt to navigate the whole group.");
	WriteChatf(PLUGIN_MSG "\ag/easyfind \aystop\aw - Stops any active easyfind.");
	WriteChatf(PLUGIN_MSG "\ag/easyfind \ayreload\aw - Reload zone connections from easyfind folder: \ay%s", g_zoneConnections->GetConfigDir().c_str());
	WriteChatf(PLUGIN_MSG "\ag/easyfind \ayui\aw - Toggle EasyFind ui");
	WriteChatf(PLUGIN_MSG "\ag/easyfind \aymigrate\aw - Migrate MQ2EasyFind.ini from old MQ2EasyFind to new format");

	WriteChatf(PLUGIN_MSG "");
	WriteChatf(PLUGIN_MSG "\ag/travelto \ao[zonename]");
	WriteChatf(PLUGIN_MSG "\ag/travelto \aygroup\ax \ao[zonename]");
	WriteChatf(PLUGIN_MSG "    Find a route to the specified \ao[zonename]\ax (short or long) and then follow it. "
		"If \aggroup\ax is specified, the command will attempt to navigate the whole group.");
	WriteChatf(PLUGIN_MSG "\ag/travelto \ayactivate\aw - If an existing zone path is active (created by the zone guide), "
		"activate that path with /travelto");
	WriteChatf(PLUGIN_MSG "\ag/travelto \aystop\aw - Stops an active /travelto");
	WriteChatf(PLUGIN_MSG "\ag/travelto \aydump\aw - Dumps zone information from the zone guide to resources/ZoneGuide.yaml");
}

void Command_EasyFind(SPAWNINFO* pSpawn, char* szLine)
{
	if (!pFindLocationWnd || !pLocalPlayer)
		return;

	if (szLine[0] == 0 || ci_equals(szLine, "help"))
	{
		ShowHelp();
		return;
	}

	if (ci_equals(szLine, "migrate"))
	{
		g_zoneConnections->MigrateIniData();
		// TODO: Save/Reload?
		return;
	}

	if (ci_equals(szLine, "reload"))
	{
		g_zoneConnections->ReloadFindableLocations();
		return;
	}

	if (ci_equals(szLine, "ui"))
	{
		ImGui_ToggleWindow();
		return;
	}

	if (ci_equals(szLine, "stop"))
	{
		Navigation_Stop();
		return;
	}

	bool group = false;
	if (ci_starts_with(szLine, "group"))
	{
		szLine += strlen("group") + 1;
		group = true;
	}

	FindWindow_FindLocation(szLine, group);
}

void Command_TravelTo(SPAWNINFO* pSpawn, char* szLine)
{
	ZoneGuideManagerClient& zoneGuide = ZoneGuideManagerClient::Instance();

	if (szLine[0] == 0)
	{
		ShowHelp();
		return;
	}

	if (ci_equals(szLine, "stop"))
	{
		ZonePath_Stop();
		return;
	}

	if (ci_equals(szLine, "activate"))
	{
		if (!zoneGuide.activePath.IsEmpty())
		{
			SPDLOG_INFO("Following active zone path");
			ZonePath_FollowActive();
			return;
		}

		SPDLOG_WARN("No active zone path to follow");
		return;
	}

	if (ci_equals(szLine, "dump"))
	{
		ZonePath_DumpConnections();
		return;
	}

	if (!pLocalPC)
	{
		SPDLOG_ERROR("You need to be in game to travel!");
		return;
	}


	EQZoneInfo* pCurrentZone = pWorldData->GetZone(pLocalPC->currentZoneId);
	if (!pCurrentZone)
	{
		SPDLOG_ERROR("Unable to determine the current zone!");
		return;
	}

	EQZoneInfo* pTargetZone = pWorldData->GetZone(GetZoneID(szLine));
	if (!pTargetZone)
	{
		SPDLOG_ERROR("Invalid zone: {}", szLine);
		return;
	}

	std::string message;
	auto path = ZonePath_GeneratePath(pCurrentZone->Id, pTargetZone->Id, message);
	if (path.empty())
	{
		SPDLOG_ERROR("Failed to generate path from \ay{}\ar to \ay{}\ar: {}",
			pCurrentZone->LongName, pTargetZone->LongName, message);
		return;
	}

	ZonePath_SetActive(path, true);
}

PLUGIN_API void InitializePlugin()
{
	WriteChatf(PLUGIN_MSG "v%s \arBETA\ax by brainiac (\aohttps://github.com/brainiac/MQ2EasyFind\ax)", EASYFIND_PLUGIN_VERSION);
	WriteChatf(PLUGIN_MSG "Type \ag/easyfind help\ax for more info.");

	g_configuration = new EasyFindConfiguration();

	// Zone connections and other navigation data is stored here
	std::string easyfindDir = (std::filesystem::path(gPathResources) / "EasyFind").string();
	g_zoneConnections = new ZoneConnections(easyfindDir);

	ImGui_Initialize();
	Navigation_Initialize();
	Lua_Initialize();
	FindWindow_Initialize();

	AddCommand("/easyfind", Command_EasyFind, false, true, true);
	AddCommand("/travelto", Command_TravelTo, false, true, true);

	g_zoneConnections->LoadFindableLocations();
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/easyfind");
	RemoveCommand("/travelto");

	Navigation_Shutdown();
	Lua_Shutdown();
	FindWindow_Shutdown();
	ImGui_Shutdown();

	delete g_configuration;
	delete g_zoneConnections;
}

PLUGIN_API void OnCleanUI()
{
	FindWindow_Shutdown();
}

PLUGIN_API void OnReloadUI()
{
	FindWindow_Initialize();
}

PLUGIN_API void SetGameState(int GameState)
{
	if (GameState != GAMESTATE_INGAME)
	{
		FindWindow_Reset();
		Navigation_Reset();
	}
}

PLUGIN_API void OnPulse()
{
	g_zoneConnections->Pulse();

	if (GetGameState() != GAMESTATE_INGAME)
		return;

	ZonePath_OnPulse();
}

PLUGIN_API void OnBeginZone()
{
	Navigation_BeginZone();
}

PLUGIN_API void OnLoadPlugin(const char* Name)
{
	if (ci_equals(Name, "MQ2Nav"))
	{
		Navigation_Initialize();
	}

	if (ci_equals(Name, "MQ2Lua"))
	{
		Lua_Initialize();
	}
}

PLUGIN_API void OnUnloadPlugin(const char* Name)
{
	if (ci_equals(Name, "MQ2Nav"))
	{
		Navigation_Shutdown();
	}

	if (ci_equals(Name, "MQ2Lua"))
	{
		Lua_Shutdown();
	}
}

PLUGIN_API void OnUpdateImGui()
{
	if (GetGameState() == GAMESTATE_INGAME)
	{
		ImGui_OnUpdate();
	}
}
