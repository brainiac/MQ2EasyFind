
#include "EasyFind.h"
#include "EasyFindConfiguration.h"

#include "plugins/lua/LuaInterface.h"

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(1.0);

static mq::lua::LuaPluginInterface* s_lua = nullptr;

bool g_navAPILoaded = false;
bool g_showWindow = false;

// /easyfind
bool g_performCommandFind = false;
bool g_performGroupCommandFind = false;

FindableLocations g_findableLocations;

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
		WriteChatf(PLUGIN_MSG "\agExecuting script.");

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
		WriteChatf(PLUGIN_MSG "\arCannot run script because MQ2Lua is not loaded: Unable to complete navigation.");
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

void Command_EasyFind(SPAWNINFO* pSpawn, char* szLine)
{
	if (!pFindLocationWnd || !pLocalPlayer)
		return;

	if (szLine[0] == 0)
	{
		WriteChatf(PLUGIN_MSG "Usage: /easyfind [search term]");
		WriteChatf(PLUGIN_MSG "    Searches the Find Window for the given text, either exact or partial match. If found, begins navigation.");
		return;
	}

	if (ci_equals(szLine, "migrate"))
	{
		g_configuration->MigrationCommand();
		return;
	}

	if (ci_equals(szLine, "reload"))
	{
		g_configuration->ReloadSettings();
		return;
	}

	if (ci_equals(szLine, "ui"))
	{
		g_showWindow = !g_showWindow;
		return;
	}

	// TODO: group command support.

	pFindLocationWnd.get_as<CFindLocationWndOverride>()->FindLocation(szLine, false);
}

PLUGIN_API void InitializePlugin()
{
	Config_Initialize();
	Navigation_Initialize();
	Lua_Initialize();
	FindWindow_Initialize();

	AddCommand("/easyfind", Command_EasyFind, false, true, true);
	AddCommand("/travelto", Command_TravelTo, false, true, true);
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/easyfind");
	RemoveCommand("/travelto");

	Navigation_Shutdown();
	Lua_Shutdown();
	ImGui_Shutdown();
	FindWindow_Shutdown();
	Config_Shutdown();
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
	ImGui_OnUpdate();
}
