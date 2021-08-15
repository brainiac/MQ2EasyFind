
#pragma once

#include <mq/Plugin.h>

#include "eqlib/WindowOverride.h"

#include <glm/vec3.hpp>

#define PLUGIN_MSG "\ag[EasyFind]\ax "
#define DEBUG_MSGS 1

#if DEBUG_MSG
#define DebugWritef WriteChatf
#else
#define DebugWritef()
#endif

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
extern FindLocationRequestState g_activeFindState;

// /easyfind
extern bool g_performCommandFind;
extern bool g_performGroupCommandFind;

extern bool g_navAPILoaded;
extern bool g_showWindow;

//----------------------------------------------------------------------------

class CFindLocationWndOverride : public WindowOverride<CFindLocationWndOverride, CFindLocationWnd>
{
public:
	enum class CustomRefType {
		Added,
		Modified,
	};

	struct RefData {
		CustomRefType type = CustomRefType::Added;
		const FindableLocation* data = nullptr;
	};

	//----------------------------------------------------------------------------
	// overrides

	virtual int OnProcessFrame() override;
	virtual bool AboutToShow() override;
	virtual int OnZone() override;
	virtual int WndNotification(CXWnd* sender, uint32_t message, void* data) override;

	uint32_t GetAvailableId();

	//----------------------------------------------------------------------------
	// zone connection handling

	void AddZoneConnection(const FindableLocation& findableLocation);
	void AddCustomLocations(bool initial);
	void RemoveCustomLocations();
	void UpdateListRowColor(int row);

	void UpdateDistanceColumn();

	FindableReference* GetReferenceForListIndex(int index) const;
	CVector3 GetReferencePosition(FindableReference* ref, bool& found);

	// Returns true if we handled the navigation here. Returns false if we couldn't do it
	// and that we should let the path get created so we can navigate to it.
	bool PerformFindWindowNavigation(int refId, bool asGroup);

	bool IsCustomLocationsAdded() const { return sm_customLocationsAdded; }

	MQColor GetColorForReference(int refId);
	RefData* GetCustomRefData(int refId);
	const FindZoneConnectionData* GetOriginalZoneConnectionData(int index);

public:
	void FindLocationByRefNum(int refNum, bool group);

	template <typename T>
	int FindClosestLocation(T&& callback)
	{
		int closestIndex = -1;
		float closestDistance = FLT_MAX;
		CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };

		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			if (callback(i))
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
							closestIndex = i;
						}
					}
				}
			}
		}

		return closestIndex;
	}

	bool FindZoneConnectionByZoneIndex(EQZoneIndex zoneId, bool group);
	bool FindLocation(std::string_view searchTerm, bool group);

	void OnHooked();
	void OnAboutToUnhook();

	static void OnHooked(CFindLocationWndOverride* pWnd) { pWnd->OnHooked(); }
	static void OnAboutToUnhook(CFindLocationWndOverride* pWnd) { pWnd->OnAboutToUnhook(); }

private:
	bool FindLocationByListIndex(int listIndex, bool group);



	// our "member variables" are static because we can't actually add new member variables,
	// but we only ever have one instance of CFindLocationWnd, so this works out to be about the same.

	static inline int sm_distanceColumn = -1;
	static inline std::chrono::steady_clock::time_point sm_lastDistanceUpdate;

	// tracks whether the custom locations have been added to the window or not.
	static inline bool sm_customLocationsAdded = false;

	// container holding our custom ref ids and their types.
  	static inline std::map<int, RefData> sm_customRefs;

	// the original zone connections for values that we overwrote.
	static inline std::map<int, FindZoneConnectionData> sm_originalZoneConnections;

	// Holds queued commands in case we try to start a bit too early.
	static inline std::string sm_queuedSearchTerm;
	static inline bool sm_queuedGroupParam = false;
	static inline EQZoneIndex sm_queuedZoneId = 0;
};

//----------------------------------------------------------------------------

void Command_EasyFind(SPAWNINFO* pSpawn, char* szLine);
void Command_TravelTo(SPAWNINFO* pSpawn, char* szLine);

SPAWNINFO* FindSpawnByName(const char* spawnName, bool exact);
void ExecuteLuaScript(std::string_view luaScript, const std::shared_ptr<FindableLocation>& findableLocation);

// Configuration Handlers
void Config_Initialize();
void Config_Shutdown();

// Find Window Handlers
void FindWindow_Initialize();
void FindWindow_Shutdown();
void FindWindow_Reset();

// ImGui Handlers
void ImGui_OnUpdate();
void ImGui_Shutdown();

// Lua Handlers
void Lua_Initialize();
void Lua_Shutdown();

// Navigation Handlers
void Navigation_Initialize();
void Navigation_Shutdown();
void Navigation_BeginZone();
void Navigation_Zoned();
void Navigation_Reset();

// ZonePath Handlers
std::vector<ZonePathData> GeneratePathToZone(EQZoneIndex fromZone, EQZoneIndex toZone);
void SetActiveZonePath(const std::vector<ZonePathData>& zonePathData, bool travel);
void StopTravelTo(bool success);
void ZonePath_OnPulse();
void ZonePath_NavCanceled();
