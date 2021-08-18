
#pragma once

#include <mq/Plugin.h>
#include <glm/vec3.hpp>

#define PLUGIN_MSG "\ag[EasyFind]\ax "
#define PLUGIN_MSG_LOG(x) x "[EasyFind]\ax "

// Our own enum just for logging purposes
enum class LocationType {
	Unknown,
	Location,
	Switch,
	Translocator,
};
const char* LocationTypeToString(LocationType type);

// Information parsed from YAML
struct ParsedTranslocatorDestination
{
	std::string keyword;
	EQZoneIndex zoneId = 0;           // numeric zone id
	int zoneIdentifier = 0;
};

struct ParsedFindableLocation
{
	std::string typeString;
	LocationType type;                // interpreted type
	glm::vec3 location;
	std::string name;
	EQZoneIndex zoneId = 0;           // numeric zone id
	int zoneIdentifier = 0;
	int switchId = -1;                // switch num, or -1 if not set
	std::string switchName;           // switch name, or "none"
	std::string luaScript;
	bool replace = false;
	std::vector<ParsedTranslocatorDestination> translocatorDestinations;
};

// loaded configuration information
struct FindableLocation
{
	FindLocationType type;
	LocationType easyfindType = LocationType::Unknown;
	glm::vec3 location;
	std::string spawnName;                    // target spawn name (instead of location)
	CXStr name;
	EQZoneIndex zoneId = 0;                   // for zone connections
	int zoneIdentifier = 0;
	int32_t switchId = -1;                    // for switch zone connections
	std::string switchName;
	std::string translocatorKeyword;
	std::string luaScript;                    // lua script for zone connections
	bool replace = false;                     // if false, we won't replace. only add if it doesn't already exist.

	// The EQ version of this location, if it exists, and data for the ui
	CFindLocationWnd::FindZoneConnectionData eqZoneConnectionData;
	bool skip = false;
	bool initialized = false;
	CXStr listCategory;
	CXStr listDescription;
};
using FindableLocations = std::vector<FindableLocation>;

struct FindLocationRequestState
{
	// The request
	bool valid = false;
	int spawnID = 0;
	int switchID = -1;
	glm::vec3 location;
	bool asGroup = false;
	FindLocationType type;
	std::shared_ptr<FindableLocation> findableLocation;

	// state while processing
	bool activateSwitch = false;
};

//----------------------------------------------------------------------------

extern FindableLocations g_findableLocations;

//----------------------------------------------------------------------------

SPAWNINFO* FindSpawnByName(const char* spawnName, bool exact);
void ExecuteLuaScript(std::string_view luaScript, const std::shared_ptr<FindableLocation>& findableLocation);

// Configuration Handlers
void Config_Initialize();
void Config_Shutdown();

// Find Window Handlers
void FindWindow_Initialize();
void FindWindow_Shutdown();
void FindWindow_Reset();
void FindWindow_FindLocation(std::string_view searchTerm, bool asGroup);

// ImGui Handlers
void ImGui_Initialize();
void ImGui_Shutdown();
void ImGui_OnUpdate();
void ImGui_ToggleWindow();

// Lua Handlers
void Lua_Initialize();
void Lua_Shutdown();

// Navigation Handlers
void Navigation_Initialize();
void Navigation_Shutdown();
void Navigation_BeginZone();
void Navigation_Zoned();
void Navigation_Reset();
bool Navigation_IsInitialized();
bool Navigation_ExecuteCommand(FindLocationRequestState&& request);

// ZonePath Handlers
std::vector<ZonePathData> ZonePath_GeneratePath(EQZoneIndex fromZone, EQZoneIndex toZone, std::string& outputMessage);
void ZonePath_SetActive(const std::vector<ZonePathData>& zonePathData, bool travel);
void ZonePath_OnPulse();
void ZonePath_NavCanceled();
void ZonePath_FollowActive();
void ZonePath_Stop();
