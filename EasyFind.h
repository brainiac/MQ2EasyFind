
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

struct ParsedFindableLocation;

// loaded configuration information
struct FindableLocation
{
	FindLocationType type;
	LocationType easyfindType = LocationType::Unknown;
	std::optional<glm::vec3> location;
	std::string spawnName;                    // target spawn name (instead of location)
	CXStr name;
	EQZoneIndex zoneId = 0;                   // for zone connections
	int zoneIdentifier = 0;
	int32_t switchId = -1;                    // for switch zone connections
	std::string switchName;
	std::string translocatorKeyword;
	std::string luaScript;                    // lua script for zone connections
	bool replace = true;                      // if false, we won't replace. only add if it doesn't already exist.
	const ParsedFindableLocation* parsedData = nullptr;

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
	bool pending = false;

	int spawnID = 0;
	int switchID = -1;
	glm::vec3 location = { 0, 0, 0 };
	FindLocationType type;
	EQZoneIndex zoneId = 0;
	std::shared_ptr<FindableLocation> findableLocation;

	std::string name;
	std::string navCommand;

	// state while processing
	bool activateSwitch = false;
};

//----------------------------------------------------------------------------

SPAWNINFO* FindSpawnByName(const char* spawnName, bool exact);
void ExecuteLuaScript(std::string_view luaScript, const std::shared_ptr<FindableLocation>& findableLocation);

void DoGroupCommand(std::string_view command, bool includeSelf);

// Find Window Handlers
void FindWindow_Initialize();
void FindWindow_Shutdown();
void FindWindow_Reset();
void FindWindow_LoadZoneConnections();
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
bool Navigation_ExecuteCommand(std::string_view navCommand);
void Navigation_Stop();

// ZonePath Handlers
struct ZonePathNode {
	EQZoneIndex zoneId;
	int transferTypeIndex;
	const ParsedFindableLocation* location = nullptr;
	const ZoneGuideConnection* connection = nullptr;

	ZonePathNode(EQZoneIndex zoneId, int transferIndex, const ParsedFindableLocation* location, const ZoneGuideConnection* connection)
		: zoneId(zoneId), transferTypeIndex(transferIndex), location(location), connection(connection) {}
	ZonePathNode(const ZonePathData& data)
		: zoneId(data.zoneId), transferTypeIndex(data.transferTypeIndex) {}
};

struct ZonePathRequest
{
	std::vector<ZonePathNode> zonePath;
	std::string targetQuery;

	void clear()
	{
		zonePath.clear();
		targetQuery.clear();
	}
};

std::vector<ZonePathNode> ZonePath_GeneratePath(EQZoneIndex fromZone, EQZoneIndex toZone, std::string& outputMessage);
void ZonePath_SetActive(const ZonePathRequest& zonePathData, bool travel);
void ZonePath_OnPulse();
void ZonePath_NavCanceled(bool message);
void ZonePath_FollowActive();
void ZonePath_Stop();
void ZonePath_DumpConnections();
bool ZonePath_IsActive();
