
#include <mq/Plugin.h>

#include "eqlib/WindowOverride.h"
#include "imgui/ImGuiUtils.h"
#include "imgui/ImGuiTextEditor.h"

#include "MQ2Nav/PluginAPI.h"
#include "plugins/lua/LuaInterface.h"

#include <fstream>
#include <optional>

#pragma warning( push )
#pragma warning( disable:4996 )
#include <yaml-cpp/yaml.h>
#pragma warning( pop )

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(1.0);

#define PLUGIN_MSG "\ag[MQ2EasyFind]\ax "
#define DEBUG_MSGS 1

#if DEBUG_MSG
#define DebugWritef WriteChatf
#else
#define DebugWritef()
#endif

// Limit the rate at which we update the distance to findable locations
constexpr std::chrono::milliseconds distanceCalcDelay = std::chrono::milliseconds{ 100 };

// configuration
static std::string s_configFile;
static YAML::Node s_configNode;

static bool s_allowFollowPlayerPath = false;
static bool s_navNextPlayerPath = false;
static std::chrono::steady_clock::time_point s_playerPathRequestTime = {};
static bool s_performCommandFind = false;
static bool s_performGroupCommandFind = false;

static const MQColor s_addedLocationColor(255, 192, 64);
static const MQColor s_modifiedLocationColor(64, 192, 255);
static bool s_showMenu = false;

static nav::NavAPI* s_nav = nullptr;
static mq::lua::LuaPluginInterface* s_lua = nullptr;
static int s_navObserverId = 0;
static imgui::TextEditor* s_luaCodeViewer = nullptr;

static std::vector<ZonePathData> s_zonePathTest;

const char* s_luaTranslocatorCode = R"(-- Hail translocator and say keyword
local spawn = mq.TLO.Spawn(location.spawnName)
if spawn ~= nil then
	spawn.DoTarget()
	mq.delay(500)
	mq.cmd("/hail")
	mq.delay(1200)
	mq.cmdf("/say %s", location.translocatorKeyword)
end
)";

// Our own enum just for logging purposes
enum class LocationType {
	Unknown,
	Location,
	Switch,
	Translocator,
};

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
FindableLocations s_findableLocations;

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
static FindLocationRequestState s_activeFindState;

static void GenerateFindableLocations(FindableLocations& findableLocations, std::vector<ParsedFindableLocation>&& parsedLocations);
static bool MigrateConfigurationFile(bool force = false);
static void ReloadSettings();
static void SaveConfigurationFile();
static void LoadSettings();

//============================================================================

#pragma region Scripted Zone Connections

void AddFindableLocationLuaBindings(sol::state_view sv)
{
	// todo: these should be moved to mq2lua

	sv.new_usertype<glm::vec3>(
		"vec3", sol::no_constructor,
		"x",    &glm::vec3::x,
		"y",    &glm::vec3::y,
		"z",    &glm::vec3::z);

	sv.new_enum("LocationType",
		"Unknown",      LocationType::Unknown,
		"Location",     LocationType::Location,
		"Switch",       LocationType::Switch,
		"Translocator", LocationType::Translocator);

	sv.new_usertype<FindableLocation>(
		"FindableLocation",                      sol::no_constructor,
		"type",                                  sol::readonly(&FindableLocation::easyfindType),
		"name",                                  sol::property([](const FindableLocation& l) -> std::string { return std::string(l.name); }), // todo: expose CXStr
		"zoneId",                                sol::readonly(&FindableLocation::zoneId),
		"zoneIdentifier",                        sol::readonly(&FindableLocation::zoneIdentifier),
		"switchId",                              sol::readonly(&FindableLocation::switchId),
		"switchName",                            sol::readonly(&FindableLocation::switchName),
		"spawnName",                             sol::readonly(&FindableLocation::spawnName),
		"translocatorKeyword",                   sol::readonly(&FindableLocation::translocatorKeyword)
	);
}

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

bool ExecuteNavCommand(FindLocationRequestState&& request)
{
	if (!s_nav)
		return false;

	s_activeFindState = std::move(request);

	char command[256] = { 0 };

	if (s_activeFindState.type == FindLocation_Player)
	{
		// nav by spawnID:
		sprintf_s(command, "spawn id %d | dist=15 log=warning tag=easyfind", s_activeFindState.spawnID);
	}
	else if (s_activeFindState.type == FindLocation_Switch || s_activeFindState.type == FindLocation_Location)
	{
		if (s_activeFindState.findableLocation)
		{
			if (!s_activeFindState.findableLocation->spawnName.empty())
			{
				sprintf_s(command, "spawn %s | dist=15 log=warning tag=easyfind", s_activeFindState.findableLocation->spawnName.c_str());
			}
		}

		if (command[0] == 0)
		{
			if (s_activeFindState.location != glm::vec3())
			{
				glm::vec3 loc = s_activeFindState.location;
				loc.z = pDisplay->GetFloorHeight(loc.x, loc.y, loc.z, 2.0f);
				sprintf_s(command, "locyxz %.2f %.2f %.2f log=warning tag=easyfind", loc.x, loc.y, loc.z);

				if (s_activeFindState.type == FindLocation_Switch)
					s_activeFindState.activateSwitch = true;
			}
			else if (s_activeFindState.type == FindLocation_Switch)
			{
				sprintf_s(command, "door id %d click log=warning tag=easyfind", s_activeFindState.switchID);
			}
		}
	}

	if (command[0] != 0)
	{
		s_nav->ExecuteNavCommand(command);
		return true;
	}

	return false;
}

void Navigation_Pulse()
{
	// Check for events to handle
	if (s_activeFindState.valid)
	{

	}
}

void Navigation_Zoned()
{
	// Clear all local navigation state (anything not meant to carry over to the next zone)
	s_activeFindState.valid = {};
}

void Navigation_Reset()
{
	// Clear all existing navigation state
	s_activeFindState = {};
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
	default: break;
	}

	WriteChatf("%s", fmt::format(PLUGIN_MSG "Nav Observer: event=\ag{}\ax tag=\ag{}\ax paused=\ag{}\ax destination=\ag({:.2f}, {:.2f}, {:.2f})\ax type=\ag{}\ax", eventName,
		commandState.tag, commandState.paused, commandState.destination.x, commandState.destination.y, commandState.destination.z,
		commandState.type).c_str());

	if (commandState.tag != "easyfind")
		return;

	if (eventType == nav::NavObserverEvent::NavStarted)
	{
		s_activeFindState.valid = true;
	}
	else if (eventType == nav::NavObserverEvent::NavDestinationReached)
	{
		if (s_activeFindState.valid)
		{
			// Determine if we have extra steps to perform once we reach the destination.
			if (s_activeFindState.activateSwitch)
			{
				WriteChatf(PLUGIN_MSG "Activating switch: \ag%d", s_activeFindState.switchID);

				EQSwitch* pSwitch = GetSwitchByID(s_activeFindState.switchID);
				if (pSwitch)
				{
					pSwitch->UseSwitch(pLocalPlayer->SpawnID, -1, 0, nullptr);
				}
			}

			if (s_activeFindState.findableLocation && !s_activeFindState.findableLocation->luaScript.empty())
			{
				if (s_lua)
				{
					WriteChatf(PLUGIN_MSG "\agExecuting script.");

					mq::lua::LuaScriptPtr threadPtr = s_lua->CreateLuaScript();
					s_lua->InjectMQNamespace(threadPtr);

					// Add bindings about our findable location.
					sol::state_view sv = s_lua->GetLuaState(threadPtr);
					AddFindableLocationLuaBindings(sv);
					sv.set("location", s_activeFindState.findableLocation);

					s_lua->ExecuteString(threadPtr, s_activeFindState.findableLocation->luaScript, "easyfind");
				}
				else
				{
					WriteChatf(PLUGIN_MSG "\arCannot run script because MQ2Lua is not loaded: Unable to complete navigation.");
				}
			}

			s_activeFindState.valid = false;
		}
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

#pragma endregion

//============================================================================

#pragma region FindLocationWnd Override and ZoneConnection list

class CFindLocationWndOverride : public WindowOverride<CFindLocationWndOverride, CFindLocationWnd>
{
public:
	static inline int sm_distanceColumn = -1;
	static inline std::chrono::steady_clock::time_point sm_lastDistanceUpdate;

	enum class CustomRefType {
		Added,
		Modified,
	};

	// tracks whether the custom locations have been added to the window or not.
	static inline bool sm_customLocationsAdded = false;

	struct RefData {
		CustomRefType type = CustomRefType::Added;
		const FindableLocation* data = nullptr;
	};

	// container holding our custom ref ids and their types.
	static inline std::map<int, RefData> sm_customRefs;

	// the original zone connections for values that we overwrote.
	static inline std::map<int, FindZoneConnectionData> sm_originalZoneConnections;

	// Holds queued commands in case we try to start a bit too early.
	static inline std::string sm_queuedSearchTerm;
	static inline bool sm_queuedGroupParam = false;

	virtual int OnProcessFrame() override
	{
		// The following checks match what OnProcessFrame() does to determine when it is
		// time to rebuild the ui. We use the same logic and anticipate the rebuild so that
		// we don't lose our custom connections.
		if (IsActive())
		{
			if (lastUpdateTime + 1000 < pDisplay->TimeStamp)
			{
				// What gets cleared:
				// if playerListDirty is true, the findLocationList is completely cleared, and
				// rebuilt from the ground up. This means all ref entries are removed and
				// all list elements are removed.
				// if playerListDirty, then only group members are changed.
				if (playerListDirty)
				{
					// This will set sm_customLocationsAdded to false, allowing us to re-inject the
					// data after OnProcessFrame() is called.
					RemoveCustomLocations();
				}
			}
		}

		// if didRebuild is true, then this will reset the refs list.
		int result = Super::OnProcessFrame();

		// Update distance column. this will internally skip work if necessary.
		UpdateDistanceColumn();

		if (zoneConnectionsRcvd && !sm_customLocationsAdded)
		{
			AddCustomLocations(true);

			if (findLocationList->GetItemCount() > 0 && !findLocationList->IsVisible())
			{
				findLocationList->SetVisible(true);
				noneLabel->SetVisible(false);
			}
		}

		if (sm_customLocationsAdded && !sm_queuedSearchTerm.empty())
		{
			FindLocation(sm_queuedSearchTerm, sm_queuedGroupParam);

			sm_queuedSearchTerm.clear();
			sm_queuedGroupParam = false;
		}

		return result;
	}

	virtual bool AboutToShow() override
	{
		// Clear selection when showing, to avoid trying to find on appear.
		if (findLocationList)
		{
			findLocationList->CurSel = -1;
		}

		// AboutToShow will reset the window, so anticipate that and remove our items first.
		// We'll add them again in OnProcessFrame().
		RemoveCustomLocations();

		return Super::AboutToShow();
	}

	virtual int OnZone() override
	{
		// Reset any temporary state. When we zone everything is destroyed and we start over.
		sm_customLocationsAdded = false;
		sm_customRefs.clear();
		sm_originalZoneConnections.clear();

		int result = Super::OnZone();

		LoadZoneSettings();

		return result;
	}

	uint32_t GetAvailableId()
	{
		lastId++;

		while (referenceList.FindFirst(lastId))
			lastId++;

		return lastId;
	}

	void AddZoneConnection(const FindableLocation& findableLocation)
	{
		// Scan items for something with the same name and description. If one exists that matches then we
		// replace it and mark it as replaced. Otherwise we add a new element.
		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			if (ci_equals(findLocationList->GetItemText(i, 0), findableLocation.listCategory)
				&& ci_equals(findLocationList->GetItemText(i, 1), findableLocation.listDescription))
			{
				if (!findableLocation.replace)
				{
					return;
				}

				// This is a matching item. Instead of adding a 2nd copy we just replace the entry with our own.
				// Get the ref from the list. This will give us the index in the zone connections list.
				int listRefId = (int)findLocationList->GetItemData(i);
				FindableReference* listRef = referenceList.FindFirst(listRefId);

				// Sanity check the type and then make the copy.
				if (listRef->type == FindLocation_Switch || listRef->type == FindLocation_Location)
				{
					sm_originalZoneConnections[listRef->index] = unfilteredZoneConnectionList[listRef->index];
					unfilteredZoneConnectionList[listRef->index] = findableLocation.eqZoneConnectionData;
					sm_customRefs[listRefId] = { CustomRefType::Modified, &findableLocation };

					// Modify the colors
					UpdateListRowColor(i);

					WriteChatf(PLUGIN_MSG "\ayReplaced %s - %s with custom data", findableLocation.listCategory.c_str(), findableLocation.listDescription.c_str());
				}

				return;
			}
		}

		// add entry to zone connection list
		unfilteredZoneConnectionList.Add(findableLocation.eqZoneConnectionData);
		int id = unfilteredZoneConnectionList.GetCount() - 1;

		// add reference
		uint32_t refId = GetAvailableId();
		FindableReference& ref = referenceList.Insert(refId);
		ref.index = id;
		ref.type = findableLocation.type;
		sm_customRefs[refId] = { CustomRefType::Added, &findableLocation };

		// update list box
		SListWndCell cellName;
		cellName.Text = findableLocation.listCategory;
		SListWndCell cellDescription;
		cellDescription.Text = findableLocation.listDescription;

		SListWndLine line;
		line.Cells.reserve(3); // reserve memory for 3 columns
		line.Cells.Add(cellName);
		line.Cells.Add(cellDescription);
		if (sm_distanceColumn != -1)
			line.Cells.Add(SListWndCell());
		line.Data = refId;

		// initialize the color
		for (SListWndCell& cell : line.Cells)
			cell.Color = (COLORREF)s_addedLocationColor;

		findLocationList->AddLine(&line);

		WriteChatf(PLUGIN_MSG "\aoAdded %s - %s with id %d", findableLocation.listCategory.c_str(), findableLocation.listDescription.c_str(), refId);
	}

	void AddCustomLocations(bool initial)
	{
		if (sm_customLocationsAdded)
			return;
		if (!pLocalPC)
			return;

		for (FindableLocation& location : s_findableLocations)
		{
			if (!location.initialized)
			{
				// Assemble the eq object
				if (location.type == FindLocation_Switch || location.type == FindLocation_Location)
				{
					location.listCategory = "Zone Connection";
					location.eqZoneConnectionData.id = 0;
					location.eqZoneConnectionData.subId = location.type == FindLocation_Location ? 0 : -1;
					location.eqZoneConnectionData.type = location.type;

					// Search for an existing zone entry that matches this one.
					for (FindZoneConnectionData& entry : unfilteredZoneConnectionList)
					{
						if (entry.zoneId == location.zoneId && entry.zoneIdentifier == location.zoneIdentifier)
						{
							// Its a connection representing the same thing.
							if (!location.replace)
							{
								location.skip = true;
							}

							// We replaced a switch with a location. Often times this just means we wanted to change the
							// position where we click the switch, not remove the switch. Unless the configuration says
							// switch: "none", then we just change the location only.
							if (entry.type == FindLocation_Switch && location.eqZoneConnectionData.type == FindLocation_Location
								&& location.spawnName.empty())
							{
								if (!ci_equals(location.switchName, "none"))
								{
									location.eqZoneConnectionData.type = FindLocation_Switch;
									location.eqZoneConnectionData.id = entry.id;
								}
							}
						}
					}

					if (location.type == FindLocation_Switch)
					{
						if (!location.switchName.empty())
						{
							EQSwitch* pSwitch = FindSwitchByName(location.switchName.c_str());

							if (pSwitch)
							{
								location.eqZoneConnectionData.id = pSwitch->ID;
							}
						}
						else
						{
							location.eqZoneConnectionData.id = location.switchId;
						}
					}

					if (!location.spawnName.empty())
					{
						// Get location of the npc
						SPAWNINFO* pSpawn = FindSpawnByName(location.spawnName.c_str(), true);
						if (pSpawn)
						{
							location.location = glm::vec3(pSpawn->Y, pSpawn->X, pSpawn->Z);
						}
						else
						{
							WriteChatf(PLUGIN_MSG "\arFailed to create translocator connection: Could not find \"\ay%s\ar\"!",
								location.spawnName.c_str());
							continue;
						}
					}

					location.eqZoneConnectionData.zoneId = location.zoneId;
					location.eqZoneConnectionData.zoneIdentifier = location.zoneIdentifier;
					location.eqZoneConnectionData.location = CVector3(location.location.x, location.location.y, location.location.z);

					if (location.name.empty())
					{
						CXStr name = GetFullZone(location.zoneId);
						if (location.zoneIdentifier)
							name.append(fmt::format(" - {}", location.zoneIdentifier));
						location.listDescription = name;
					}
					else
					{
						location.listDescription = location.name;
					}
				}

				location.initialized = true;
			}

			if (!location.skip)
			{
				AddZoneConnection(location);
			}
		}

		sm_customLocationsAdded = true;
	}

	void RemoveCustomLocations()
	{
		if (!sm_customLocationsAdded)
			return;
		if (!findLocationList)
			return;

		int index = 0;

		// Remove all the items from the list that contain entries in our custom refs list.
		while (index < findLocationList->GetItemCount())
		{
			int refId = (int)findLocationList->GetItemData(index);
			auto iter = sm_customRefs.find(refId);
			if (iter != sm_customRefs.end())
			{
				auto type = iter->second.type;
				sm_customRefs.erase(iter);

				if (type == CustomRefType::Added)
				{
					// This is a custom entry. Remove it completely.
					findLocationList->RemoveLine(index);

					// Remove the reference too
					auto refIter = referenceList.find(refId);
					if (refIter != referenceList.end())
					{
						// Remove the element from the zone connection list and fix up any other
						// refs that tried to index anything after it.
						auto& refData = refIter->first;

						unfilteredZoneConnectionList.DeleteElement(refData.index);
						if (refData.type == FindLocation_Location || refData.type == FindLocation_Switch)
						{
							// Remove it from the list and decrement any indices that occur after it.
							for (auto& entry : referenceList)
							{
								if (entry.first.index > refData.index
									&& (entry.first.type == FindLocation_Location || entry.first.type == FindLocation_Switch))
								{
									--entry.first.index;
								}
							}
						}

						referenceList.erase(refIter);

					}
				}
				else if (type == CustomRefType::Modified)
				{
					// This is a modification to an existing entry. Restore it.

					auto refIter = referenceList.find(refId);
					if (refIter != referenceList.end())
					{
						auto& refData = refIter->first;

						if (refData.type == FindLocation_Location || refData.type == FindLocation_Switch)
						{
							int connectionIndex = refData.index;

							// Look the original data and do some sanity checks
							auto connectionIter = sm_originalZoneConnections.find(connectionIndex);
							if (connectionIter != sm_originalZoneConnections.end())
							{
								if (connectionIndex >= 0 && connectionIndex < unfilteredZoneConnectionList.GetCount())
								{
									// replace the content
									unfilteredZoneConnectionList[connectionIndex] = connectionIter->second;
								}

								sm_originalZoneConnections.erase(connectionIter);
							}
						}
					}
				}
			}
			else
			{
				++index;
			}
		}

		if (findLocationList->GetItemCount() == 0)
		{
			findLocationList->SetVisible(false);
			noneLabel->SetVisible(true);
		}

		sm_customLocationsAdded = false;
	}

	void UpdateListRowColor(int row)
	{
		int listRefId = (int)findLocationList->GetItemData(row);

		auto iter = sm_customRefs.find(listRefId);
		if (iter != sm_customRefs.end())
		{
			CustomRefType type = iter->second.type;
			SListWndLine& line = findLocationList->ItemsArray[row];

			for (SListWndCell& cell : line.Cells)
			{
				if (type == CustomRefType::Added)
					cell.Color = (COLORREF)s_addedLocationColor;
				else if (type == CustomRefType::Modified)
					cell.Color = (COLORREF)s_modifiedLocationColor;
			}
		}
	}

	virtual int WndNotification(CXWnd* sender, uint32_t message, void* data) override
	{
		if (sender == findLocationList)
		{
			if (message == XWM_LCLICK)
			{
				int selectedRow = (int)data;
				int refId = (int)findLocationList->GetItemData(selectedRow);

				// TODO: Configurable keybinds
				if (pWndMgr->IsCtrlKey() || s_performCommandFind)
				{
					bool groupNav = pWndMgr->IsShiftKey() || s_performGroupCommandFind;

					// Try to perform the navigation. If we succeed, bail out. Otherwise trigger the
					// navigation via player path.
					if (PerformFindWindowNavigation(refId, groupNav))
					{
						Show(false);
						return 0;
					}
				}

				auto refIter = sm_customRefs.find(refId);
				if (refIter != sm_customRefs.end())
				{
					// Don't "find" custom locations
					if (refIter->second.type == CustomRefType::Added)
					{
						WriteChatf(PLUGIN_MSG "\arCannot find custom locations.");
						return 0;
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
		auto now = std::chrono::steady_clock::now();
		bool periodicUpdate = false;

		if (now - sm_lastDistanceUpdate > distanceCalcDelay)
		{
			sm_lastDistanceUpdate = now;
			periodicUpdate = true;
		}

		CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };
		bool needsSort = false;

		for (int index = 0; index < findLocationList->ItemsArray.GetCount(); ++index)
		{
			// Only update columns if this is a periodic update or if a column is empty.
			if (!findLocationList->GetItemText(index, sm_distanceColumn).empty() && !periodicUpdate)
			{
				continue;
			}

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

			needsSort = true;
		}

		// If the distance coloumn is being sorted, update it.
		if (findLocationList->SortCol == sm_distanceColumn && needsSort)
		{
			findLocationList->Sort();
		}
	}

	FindableReference* GetReferenceForListIndex(int index) const
	{
		int refId = (int)findLocationList->GetItemData(index);
		FindableReference* ref = referenceList.FindFirst(refId);

		return ref;
	}

	CVector3 GetReferencePosition(FindableReference* ref, bool& found)
	{
		found = false;

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
			const FindZoneConnectionData& zoneConn = unfilteredZoneConnectionList[ref->index];

			if (zoneConn.location.X != 0.0f && zoneConn.location.Y != 0.0f && zoneConn.location.Z != 0.0f)
			{
				found = true;
				return zoneConn.location;
			}
			else if (ref->type == FindLocation_Switch)
			{
				EQSwitch* pSwitch = GetSwitchByID(zoneConn.id);
				if (pSwitch)
				{
					found = true;
					return CVector3(pSwitch->Y, pSwitch->X, pSwitch->Z);
				}
			}
		}

		return CVector3();
	}

	// Returns true if we handled the navigation here. Returns false if we couldn't do it
	// and that we should let the path get created so we can navigate to it.
	bool PerformFindWindowNavigation(int refId, bool asGroup)
	{
		if (!s_nav)
		{
			WriteChatf(PLUGIN_MSG "\arNavigation requires the MQ2Nav plugin to be loaded.");
			return false;
		}

		FindableReference* ref = referenceList.FindFirst(refId);
		if (!ref)
		{
			return false;
		}

		const FindableLocation* customLocation = nullptr;
		auto customIter = sm_customRefs.find(refId);
		if (customIter != sm_customRefs.end())
		{
			customLocation = customIter->second.data;
		}

		switch (ref->type)
		{
		case FindLocation_Player:
			// In the case that we are finding a spawn, then the index is actually the spawn id,
			// and we need to look it up.
			if (PlayerClient* pSpawn = GetSpawnByID(ref->index))
			{
				if (pSpawn->Lastname[0] && pSpawn->Type == SPAWN_NPC)
					WriteChatf(PLUGIN_MSG "Navigating to \aySpawn\ax: \ag%s (%s)", pSpawn->DisplayedName, pSpawn->Lastname);
				else if (pSpawn->Type == SPAWN_PLAYER)
					WriteChatf(PLUGIN_MSG "Navigating to \ayPlayer:\ax \ag%s", pSpawn->DisplayedName);
				else
					WriteChatf(PLUGIN_MSG "Navigating to \aySpawn:\ax \ag%s", pSpawn->DisplayedName);

				FindLocationRequestState request;
				request.spawnID = ref->index;
				request.asGroup = asGroup;
				request.type = ref->type;
				if (customLocation)
					request.findableLocation = std::make_shared<FindableLocation>(*customLocation);

				ExecuteNavCommand(std::move(request));
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

			int32_t switchId = 0;
			if (ref->type == FindLocation_Switch)
			{
				switchId = zoneConn.id;
			}

			char szLocationName[256];
			if (zoneConn.zoneIdentifier > 0)
				sprintf_s(szLocationName, "%s - %d", GetFullZone(zoneConn.zoneId), zoneConn.zoneIdentifier);
			else
				strcpy_s(szLocationName, GetFullZone(zoneConn.zoneId));

			EQSwitch* pSwitch = nullptr;
			if (ref->type == FindLocation_Switch)
			{
				pSwitch = pSwitchMgr->GetSwitchById(switchId);
			}

			FindLocationRequestState request;
			request.location = *(glm::vec3*)&zoneConn.location;
			request.switchID = switchId;
			request.asGroup = asGroup;
			request.type = ref->type;
			if (customLocation)
				request.findableLocation = std::make_shared<FindableLocation>(*customLocation);

			if (pSwitch)
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax (via switch \ao%s\ax)", szLocationName, pSwitch->Name);
			}
			else
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax", szLocationName);
			}

			ExecuteNavCommand(std::move(request));
			return true;
		}

		default:
			WriteChatf(PLUGIN_MSG "\arCannot navigate to selection type: %d", ref->type);
			break;
		}

		return false;
	}

private:
	void FindLocationByListIndex(int listIndex, bool group)
	{
		// Perform navigaiton by triggering a selection in the list.
		s_performCommandFind = true;
		s_performGroupCommandFind = group;

		findLocationList->SetCurSel(listIndex);
		findLocationList->ParentWndNotification(findLocationList, XWM_LCLICK, (void*)listIndex);

		s_performCommandFind = false;
		s_performGroupCommandFind = false;
	}

public:
	void FindLocationByRefNum(int refNum, bool group)
	{
		for (int index = 0; index < findLocationList->GetItemCount(); ++index)
		{
			int itemRefNum = (int)findLocationList->GetItemData(index);
			if (itemRefNum == refNum)
			{
				FindLocationByListIndex(index, group);
				return;
			}
		}

		WriteChatf(PLUGIN_MSG "\arCouldn't find location by ref: %d", refNum);
	}

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

	void FindLocation(std::string_view searchTerm, bool group)
	{
		if (!sm_customLocationsAdded)
		{
			sm_queuedSearchTerm = searchTerm;
			sm_queuedGroupParam = group;

			WriteChatf(PLUGIN_MSG "Need to wait for connections to be added!");
			return;
		}

		int foundIndex = -1;

		// Do an exact search first.
		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			CXStr itemText = findLocationList->GetItemText(i, 1);
			if (ci_equals(itemText, searchTerm))
			{
				foundIndex = i;
				break;
			}
		}

		// Didn't find an exact match. Try an exact match against the short zone name of each connection.
		if (foundIndex == -1)
		{
			foundIndex = FindClosestLocation(
				[&](int index)
			{
				int refId = (int)findLocationList->GetItemData(index);
				FindableReference* ref = referenceList.FindFirst(refId);
				if (!ref) return false;

				if (ref->type == FindLocation_Location || ref->type == FindLocation_Switch)
				{
					FindZoneConnectionData& connData = unfilteredZoneConnectionList[ref->index];
					EQZoneInfo* pZoneInfo = pWorldData->GetZone(connData.zoneId);

					return pZoneInfo && ci_equals(pZoneInfo->ShortName, searchTerm);
				}

				return false;
			});
		}

		if (foundIndex == -1)
		{
			// if didn't find then try a substring search, picking the closest match by distance.
			foundIndex = FindClosestLocation(
				[&](int index) { return ci_find_substr(findLocationList->GetItemText(index, 1), searchTerm); });
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

		FindLocationByListIndex(foundIndex, group);
	}

	void LoadZoneSettings()
	{
		RemoveCustomLocations();

		if (!pZoneInfo)
			return;

		EQZoneInfo* zoneInfo = pWorldData->GetZone(pZoneInfo->ZoneID);
		if (!zoneInfo)
			return;
		const char* shortName = zoneInfo->ShortName;

		FindableLocations newLocations;

		try
		{
			// Load objects from the AddFindLocations block
			YAML::Node addFindLocations = s_configNode["FindLocations"];
			if (addFindLocations.IsMap())
			{
				YAML::Node zoneNode = addFindLocations[shortName];
				if (zoneNode.IsDefined())
				{
					std::vector<ParsedFindableLocation> parsedLocations = zoneNode.as<std::vector<ParsedFindableLocation>>();
					GenerateFindableLocations(newLocations, std::move(parsedLocations));
				}
			}
		}
		catch (const YAML::Exception& ex)
		{
			// failed to parse, notify and return
			WriteChatf(PLUGIN_MSG "\arFailed to load zone settings for %s: %s", shortName, ex.what());
		}

		s_findableLocations = std::move(newLocations);
	}

	void OnHooked()
	{
		if (!findLocationList)
			return;

		CListWnd* locs = findLocationList;

		if (locs->Columns.GetCount() == 2)
		{
			sm_distanceColumn = locs->AddColumn("Distance", 60, 0, CellTypeBasicText);
			locs->SetColumnJustification(sm_distanceColumn, 0);

			// Copy the color from the other columns
			for (int i = 0; i < locs->GetItemCount(); ++i)
			{
				SListWndLine& line = locs->ItemsArray[i];
				if (line.Cells.GetCount() == 2)
				{
					line.Cells.reserve(3);
					line.Cells.Add(SListWndCell());
				}
				line.Cells[sm_distanceColumn].Color = line.Cells[sm_distanceColumn - 1].Color;
			}
		}
		else if (locs->Columns.GetCount() == 3
			&& (locs->Columns[2].StrLabel == "Distance" || locs->Columns[2].StrLabel == ""))
		{
			sm_distanceColumn = 2;
			locs->SetColumnLabel(sm_distanceColumn, "Distance");
		}

		UpdateDistanceColumn();
		SetWindowText("Find Window (Ctrl+Shift+Click to Navigate)");
		LoadZoneSettings();
	}

	void OnAboutToUnhook()
	{
		if (!findLocationList)
			return;

		RemoveCustomLocations();

		CListWnd* locs = findLocationList;
		if (sm_distanceColumn != -1)
		{
			locs->Columns.DeleteElement(sm_distanceColumn);

			for (int index = 0; index < locs->ItemsArray.GetCount(); ++index)
			{
				if (locs->ItemsArray[index].Cells.GetCount() >= sm_distanceColumn)
					locs->ItemsArray[index].Cells.DeleteElement(sm_distanceColumn);

				// restore colors
				locs->ItemsArray[index].Cells[0].Color = (COLORREF)MQColor(255, 255, 255);
				locs->ItemsArray[index].Cells[1].Color = (COLORREF)MQColor(255, 255, 255);
			}
		}

		SetWindowText("Find Window");
		sm_distanceColumn = -1;
	}

	static void OnHooked(CFindLocationWndOverride* pWnd) { pWnd->OnHooked(); }
	static void OnAboutToUnhook(CFindLocationWndOverride* pWnd) { pWnd->OnAboutToUnhook(); }
};

#pragma endregion

//============================================================================
//============================================================================

#pragma region Zone Path Handling

// Generates a path to the zone by utilizing data from the ZoneGuideManagerClient.
std::vector<ZonePathData> GeneratePathToZone(EQZoneIndex fromZone, EQZoneIndex toZone)
{
	ZoneGuideManagerClient& zoneMgr = ZoneGuideManagerClient::Instance();

	if (fromZone == toZone)
		return {};

	ZoneGuideZone* toZoneData = zoneMgr.GetZone(toZone);
	if (!toZoneData)
		return {};

	ZoneGuideZone* nextZone = nullptr;
	ZoneGuideZone* currentZone = zoneMgr.GetZone(fromZone);

	if (!currentZone)
		return {};

	// Implements a breadth-first search of the zone connections

	std::deque<ZoneGuideZone*> queue;
	struct ZonePathGenerationData {
		int depth = -1;
		int pathMinLevel = -1;
		int prevZoneTransferTypeIndex = -1;
		EQZoneIndex prevZone = 0;
	};
	std::unordered_map<EQZoneIndex, ZonePathGenerationData> pathData;

	queue.push_back(currentZone);
	pathData[fromZone].depth = 0;
	pathData[fromZone].pathMinLevel = currentZone->minLevel;

	// TODO: Handle bind zones (gate)
	// TODO: Handle teleport spell zones (translocate, etc)

	// Explore the zone graph and cost everything out.
	while (!queue.empty())
	{
		currentZone = queue.front();
		queue.pop_front();

		// Did we find a connection to the destination?
		if (pathData[toZone].depth > -1 && pathData[toZone].depth < pathData[currentZone->zoneId].depth)
		{
			break;
		}

		for (const ZoneGuideConnection& connection : currentZone->zoneConnections)
		{
			// Skip connection if it is disabled by the user.
			if (connection.disabled)
				continue;

			// TODO: Progression server check

			nextZone = zoneMgr.GetZone(connection.destZoneId);
			if (nextZone)
			{
				auto& data = pathData[connection.destZoneId];
				auto& prevData = pathData[currentZone->zoneId];

				if (data.depth == -1)
				{
					queue.push_back(nextZone);

					data.prevZoneTransferTypeIndex = connection.transferTypeIndex;
					data.prevZone = currentZone->zoneId;
					data.pathMinLevel = std::max(prevData.pathMinLevel, nextZone->minLevel);

					data.depth = prevData.depth + 1;
				}
				else if (data.prevZone && (data.depth == prevData.depth + 1)
					&& pathData[data.prevZone].pathMinLevel > prevData.pathMinLevel)
				{
					// lower level preference?
					data.prevZoneTransferTypeIndex = connection.transferTypeIndex;
					data.prevZone = currentZone->zoneId;
					data.pathMinLevel = std::max(prevData.pathMinLevel, nextZone->minLevel);
				}
			}
		}
	}

	// Work backwards from the destination and build the route.
	EQZoneIndex zoneId = toZone;
	int transferTypeIndex = -1;
	std::vector<ZonePathData> reversedPath;

	while (zoneId != 0)
	{
		reversedPath.emplace_back(zoneId, transferTypeIndex);

		transferTypeIndex = pathData[zoneId].prevZoneTransferTypeIndex;
		zoneId = pathData[zoneId].prevZone;
	}

	//ZonePathArray newArray(reversedPath.size());
	std::vector<ZonePathData> newPath;
	newPath.reserve(reversedPath.size());

	// If we made it back to the start, then flip the list around and return it.
	if (!reversedPath.empty() && reversedPath.back().zoneId == fromZone)
	{
		for (auto riter = reversedPath.rbegin(); riter != reversedPath.rend(); ++riter)
		{
			newPath.push_back(*riter);
		}
	}

	return newPath;
}

void SetActiveZonePath(const std::vector<ZonePathData>& zonePathData)
{
	ZonePathArray pathArray(zonePathData.size());

	for (const ZonePathData& pathData : zonePathData)
	{
		pathArray.Add(pathData);
	}

	ZoneGuideManagerClient::Instance().activePath = std::move(pathArray);

	if (pZonePathWnd)
	{
		pZonePathWnd->zonePathDirty = true;
		pZonePathWnd->Show(true);
	}
}

#pragma endregion

//============================================================================
//============================================================================

#pragma region Configuration

static bool MigrateConfigurationFile(bool force)
{
	if (!pWorldData)
	{
		return false; // TODO: Retry later
	}

	bool migratedAlready = s_configNode["ConfigurationMigrated"].as<bool>(false);
	if (migratedAlready && !force)
	{
		return false;
	}

	WriteChatf(PLUGIN_MSG "Migrating configuration from INI...");
	std::string iniFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.ini").string();

	int count = 0;
	std::vector<std::string> sectionNames = GetPrivateProfileSections(iniFile);

	if (!sectionNames.empty())
	{
		YAML::Node findLocations = s_configNode["FindLocations"];
		for (const std::string& sectionName : sectionNames)
		{
			std::string zoneShortName = sectionName;
			MakeLower(zoneShortName);

			YAML::Node overrides = findLocations[zoneShortName];

			std::vector<std::string> keyNames = GetPrivateProfileKeys(zoneShortName, iniFile);
			for (std::string& keyName : keyNames)
			{
				auto splitPos = keyName.rfind(" - ");

				int identifier = 0;
				std::string_view zoneLongName = keyName;

				if (splitPos != std::string::npos)
				{
					identifier = GetIntFromString(zoneLongName.substr(splitPos + 3), 0);
					if (identifier != 0)
					{
						zoneLongName = trim(zoneLongName.substr(0, splitPos));
					}
				}

				EQZoneInfo* pZoneInfo = nullptr;

				// Convert long name to short name
				for (EQZoneInfo* pZone : pWorldData->ZoneArray)
				{
					if (pZone && ci_equals(pZone->LongName, zoneLongName))
					{
						pZoneInfo = pZone;
						break;
					}
				}

				if (pZoneInfo)
				{
					std::string value = GetPrivateProfileString(sectionName, keyName, "", iniFile);
					if (!value.empty())
					{
						std::vector<std::string_view> pieces = split_view(value, ' ', true);

						int switchId = -1;
						glm::vec3 position;

						int index = 0;
						size_t size = pieces.size();

						if (size == 4)
						{
							if (starts_with(pieces[index++], "door:"))
							{
								// handle door and position (but we don't use position)
								std::string_view switchNum = pieces[0];

								switchId = GetIntFromString(replace(switchNum, "door:", ""), -1);
								--size;
							}
							else
							{
								WriteChatf("\arFailed to migrate section: %s key: %s, invalid value: %s.", sectionName.c_str(), keyName.c_str(), value.c_str());
								continue;
							}
						}
						else if (size == 3)
						{
							// handle position
							float x = GetFloatFromString(pieces[index], 0.0f);
							float y = GetFloatFromString(pieces[index + 1], 0.0f);
							float z = GetFloatFromString(pieces[index + 2], 0.0f);

							position = glm::vec3(x, y, z);
						}
						else
						{
							WriteChatf("\arFailed to migrate section: %s key: %s, invalid value: %s.", sectionName.c_str(), keyName.c_str(), value.c_str());
							continue;
						}

						YAML::Node obj;
						obj["type"] = "ZoneConnection";

						if (switchId != -1)
						{
							obj["switch"] = switchId;
						}
						else
						{
							obj["location"] = position;
						}

						std::string targetZone = pZoneInfo->ShortName;
						MakeLower(targetZone);
						obj["targetZone"] = targetZone;

						if (identifier != 0)
						{
							obj["identifier"] = identifier;
						}

						overrides.push_back(obj);
						count++;
						continue;
					}
				}

				WriteChatf("\arFailed to migrate section: %s key: %s, zone name not found.", sectionName.c_str(), keyName.c_str());
			}
		}
	}

	s_configNode["ConfigurationMigrated"] = true;
	if (count > 0)
	{
		WriteChatf("\agMigrated %d zone connections from MQ2EasyFind.ini", count);
	}
	return true;
}

static void SaveConfigurationFile()
{
	std::fstream file(s_configFile, std::ios::out);

	if (!s_configNode.IsNull())
	{
		YAML::Emitter y_out;
		y_out.SetIndent(4);
		y_out.SetFloatPrecision(3);
		y_out.SetDoublePrecision(3);
		y_out << s_configNode;

		file << y_out.c_str();
	}
}

namespace YAML
{
	template <>
	struct convert<glm::vec3> {
		static Node encode(const glm::vec3& vec) {
			Node node;
			node.push_back(vec.x);
			node.push_back(vec.y);
			node.push_back(vec.z);
			node.SetStyle(YAML::EmitterStyle::Flow);
			return node;
		}

		static bool decode(const Node& node, glm::vec3& vec) {
			if (!node.IsSequence() || node.size() != 3) {
				return false;
			}
			vec.x = node[0].as<float>();
			vec.y = node[1].as<float>();
			vec.z = node[2].as<float>();
			return true;
		}
	};

	template <>
	struct convert<ParsedTranslocatorDestination> {
		static Node encode(const ParsedTranslocatorDestination& data) {
			Node node;
			return node;
		}

		static bool decode(const Node& node, ParsedTranslocatorDestination& data) {
			if (!node.IsMap()) {
				return false;
			}

			data.keyword = node["keyword"].as<std::string>(std::string());

			// read zone name (or id)
			int zoneId = node["targetZone"].as<int>(0);
			if (zoneId == 0)
			{
				zoneId = GetZoneID(node["targetZone"].as<std::string>().c_str());
			}
			data.zoneId = (EQZoneIndex)zoneId;
			data.zoneIdentifier = node["identifier"].as<int>(0);
			return true;
		}
	};

	template <>
	struct convert<ParsedFindableLocation> {
		static Node encode(const ParsedFindableLocation& data) {
			// todo
			return Node();
		}
		static bool decode(const Node& node, ParsedFindableLocation& data) {
			if (!node.IsMap()) {
				return false;
			}

			data.typeString = node["type"].as<std::string>();
			if (ci_equals(data.typeString, "ZoneConnection"))
			{
				data.type = LocationType::Location;
				data.name = node["name"].as<std::string>(std::string());

				// If a location is provided, then it is a location.
				if (node["location"].IsDefined())
				{
					data.location = node["location"].as<glm::vec3>();
				}

				if (node["switch"].IsDefined())
				{
					YAML::Node switchNode = node["switch"];

					// first try to read as int, then as string.
					data.switchId = switchNode.as<int>(-1);
					if (data.switchId == -1)
					{
						data.switchName = switchNode.as<std::string>();
					}

					if (data.switchId != -1 || (!data.switchName.empty() && !ci_equals(data.switchName, "none")))
					{
						data.type = LocationType::Switch;
					}
				}

				// read zone name (or id)
				int zoneId = node["targetZone"].as<int>(0);
				if (zoneId == 0)
				{
					zoneId = GetZoneID(node["targetZone"].as<std::string>().c_str());
				}
				data.zoneId = (EQZoneIndex)zoneId;
				data.zoneIdentifier = node["identifier"].as<int>(0);
				data.replace = node["replace"].as<bool>(true);
				data.luaScript = node["script"].as<std::string>(std::string());
				return true;
			}
			else if (ci_equals(data.typeString, "Translocator"))
			{
				data.type = LocationType::Translocator;
				data.name = node["name"].as<std::string>(std::string());

				// we can have a list of destinations or a single.
				if (node["destinations"].IsDefined())
				{
					data.translocatorDestinations = node["destinations"].as<std::vector<ParsedTranslocatorDestination>>();
				}
				else
				{
					ParsedTranslocatorDestination destination = node.as<ParsedTranslocatorDestination>();
					data.translocatorDestinations.push_back(destination);
				}
				return true;
			}

			// other types not supported yet
			return false;
		}
	};

	// std::map
	template <typename K, typename V, typename C>
	struct convert<std::map<K, V, C>> {
		static Node encode(const std::map<K, V, C>& rhs) {
			Node node(NodeType::Map);
			for (typename std::map<K, V>::const_iterator it = rhs.begin();
				it != rhs.end(); ++it)
				node.force_insert(it->first, it->second);
			return node;
		}

		static bool decode(const Node& node, std::map<K, V, C>& rhs) {
			if (!node.IsMap())
				return false;

			rhs.clear();
			for (const_iterator it = node.begin(); it != node.end(); ++it)
				rhs[it->first.as<K>()] = it->second.as<V>();
			return true;
		}
	};
}

static void GenerateFindableLocations(FindableLocations& findableLocations, std::vector<ParsedFindableLocation>&& parsedLocations)
{
	findableLocations.reserve(parsedLocations.size());

	for (ParsedFindableLocation& parsedLocation : parsedLocations)
	{
		switch (parsedLocation.type)
		{
		case LocationType::Location:
		case LocationType::Switch: {
			FindableLocation loc;
			loc.easyfindType = parsedLocation.type;
			loc.type = (parsedLocation.type == LocationType::Location) ? FindLocation_Location : FindLocation_Switch;
			loc.location = std::move(parsedLocation.location);
			loc.name = std::move(parsedLocation.name);
			loc.zoneId = parsedLocation.zoneId;
			loc.zoneIdentifier = parsedLocation.zoneIdentifier;
			loc.switchId = parsedLocation.switchId;
			loc.switchName = std::move(parsedLocation.switchName);
			loc.luaScript = std::move(parsedLocation.luaScript);
			loc.replace = parsedLocation.replace;
			findableLocations.push_back(std::move(loc));
			break;
		}

		case LocationType::Translocator: {
			FindableLocation loc;
			loc.easyfindType = parsedLocation.type;
			loc.type = FindLocation_Location;
			loc.spawnName = std::move(parsedLocation.name);

			for (ParsedTranslocatorDestination& dest : parsedLocation.translocatorDestinations)
			{
				FindableLocation transLoc = loc;
				transLoc.zoneId = dest.zoneId;
				transLoc.zoneIdentifier = dest.zoneIdentifier;
				transLoc.translocatorKeyword = dest.keyword;

				transLoc.luaScript = s_luaTranslocatorCode;
				findableLocations.push_back(std::move(transLoc));
			}
			break;
		}

		case LocationType::Unknown:
			break;
		}
	}
}

static void LoadSettings()
{
	try
	{
		s_configNode = YAML::LoadFile(s_configFile);
	}
	catch (const YAML::ParserException& ex)
	{
		// failed to parse, notify and return
		WriteChatf("Failed to parse YAML in %s with %s", s_configFile.c_str(), ex.what());
		return;
	}
	catch (const YAML::BadFile&)
	{
		// if we can't read the file, then try to write it with an empty config
		SaveConfigurationFile();
		return;
	}
}

static void ReloadSettings()
{
	WriteChatf(PLUGIN_MSG "Reloading settings");
	LoadSettings();

	if (pFindLocationWnd)
	{
		pFindLocationWnd.get_as<CFindLocationWndOverride>()->LoadZoneSettings();
	}
}

#pragma endregion

//============================================================================
//============================================================================

#pragma region ImGui

static void DrawFindZoneConnectionData(const CFindLocationWnd::FindZoneConnectionData& data)
{
	ImGui::Text("Type:"); ImGui::SameLine(0.0f, 4.0f);
	ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", FindLocationTypeToString(data.type));

	ImGui::Text("Zone ID: %d", data.zoneId);

	ImGui::Text("Zone Name:"); ImGui::SameLine(0.0f, 4.0f);
	EQZoneInfo* pZoneInfo = pWorldData->GetZone(data.zoneId);
	if (pZoneInfo)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%s)", pZoneInfo->LongName, pZoneInfo->ShortName);
	}
	else
	{
		ImGui::TextColored(MQColor(127, 127, 127).ToImColor(), "(null)");
	}

	if (data.zoneIdentifier > 0)
	{
		ImGui::Text("Zone Identifier: %d", data.zoneIdentifier);
	}

	ImGui::Text("Location:"); ImGui::SameLine(0.0f, 4.0f);
	ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "(%.2f, %.2f, %.2f)", data.location.X, data.location.Y, data.location.Z);

	if (data.type == FindLocation_Switch)
	{
		ImGui::Text("Switch ID: %d", data.id);
		if (data.id >= 0)
		{
			EQSwitch* pSwitch = GetSwitchByID(data.id);
			ImGui::Text("Switch Name:"); ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextColored(pSwitch ? MQColor(0, 255, 0).ToImColor() : MQColor(127, 127, 217).ToImColor(),
				"%s", pSwitch ? pSwitch->Name : "(null)");
		}
	}

	ImGui::Text("Sub ID: %d", data.subId);
}

static void DrawEasyFindZoneConnections()
{
	// refs can change, so we need two ways to determine if we're still on the selected item.
	static int selectedRef = -1;
	static CFindLocationWnd::FindableReference selectedRefData = { FindLocation_Unknown, 0 };
	bool changedRef = false;

	bool foundSelectedRef = false;
	CFindLocationWndOverride* findLocWnd = pFindLocationWnd.get_as<CFindLocationWndOverride>();

	ImGui::BeginGroup();
	{
		ImGui::BeginChild("Entry List", ImVec2(275, 0), true);

		if (ImGui::BeginTable("##Entries", 2, ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Category");
			ImGui::TableSetupColumn("Description");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			if (findLocWnd)
			{
				for (int i = 0; i < findLocWnd->findLocationList->GetItemCount(); ++i)
				{
					int refId = (int)findLocWnd->findLocationList->GetItemData(i);
					CFindLocationWnd::FindableReference* ref = findLocWnd->referenceList.FindFirst(refId);
					if (!ref) continue;

					static char label[256];

					ImGui::PushID(refId);

					CXStr category = findLocWnd->findLocationList->GetItemText(i, 0);
					CXStr description = findLocWnd->findLocationList->GetItemText(i, 1);

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImU32 textColor = MQColor(255, 255, 255, 255).ToImU32();

					auto iter = findLocWnd->sm_customRefs.find(refId);
					if (iter != findLocWnd->sm_customRefs.end())
					{
						if (iter->second.type == CFindLocationWndOverride::CustomRefType::Added)
							textColor = s_addedLocationColor.ToImU32();
						else if (iter->second.type == CFindLocationWndOverride::CustomRefType::Modified)
							textColor = s_modifiedLocationColor.ToImU32();
					}

					ImGui::PushStyleColor(ImGuiCol_Text, textColor);

					bool selected = (selectedRef == refId || *ref == selectedRefData);
					if (selected)
					{
						changedRef = test_and_set(selectedRef, refId);
						selectedRefData = *ref;

						foundSelectedRef = true;
					}

					if (ImGui::Selectable(category.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						if (selected)
						{
							changedRef = test_and_set(selectedRef, refId);
							selectedRefData = *ref;

							foundSelectedRef = true;
						}
					}

					ImGui::TableNextColumn();
					ImGui::Text("%s", description.c_str());
					ImGui::PopStyleColor();

					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		ImGui::EndChild();
	}
	ImGui::EndGroup();

	if (!foundSelectedRef)
	{
		selectedRef = -1;
		selectedRefData = { FindLocation_Unknown, 0 };
	}

	ImGui::SameLine();

	ImGui::BeginGroup();
	{
		ImGui::BeginChild("Entry Viewer");

		if (selectedRef != -1 && findLocWnd)
		{
			CFindLocationWnd::FindableReference* ref = findLocWnd->referenceList.FindFirst(selectedRef);
			if (ref)
			{
				const CFindLocationWnd::FindPlayerData* playerData = nullptr;
				const CFindLocationWnd::FindZoneConnectionData* zoneConnectionData = nullptr;

				CFindLocationWndOverride::RefData* customRefData = nullptr;

				if (ref->type == FindLocation_Player)
				{
					// Find the FindPlayerData with this playerId
					for (const CFindLocationWnd::FindPlayerData& data : findLocWnd->unfilteredPlayerList)
					{
						if (data.spawnId == ref->index)
						{
							playerData = &data;
							break;
						}
					}
				}
				else if (ref->type == FindLocation_Switch || ref->type == FindLocation_Location)
				{
					zoneConnectionData = &findLocWnd->unfilteredZoneConnectionList[ref->index];
				}

				auto iter = findLocWnd->sm_customRefs.find(selectedRef);
				if (iter != findLocWnd->sm_customRefs.end())
				{
					customRefData = &iter->second;
				}

				// Render a title
				char title[256];

				if (playerData)
				{
					if (!playerData->description.empty())
						sprintf_s(title, "%s - %s", playerData->description.c_str(), playerData->name.c_str());
					else
						strcpy_s(title, playerData->name.c_str());
				}
				else if (zoneConnectionData)
				{
					EQZoneInfo* pZoneInfo = pWorldData->GetZone(zoneConnectionData->zoneId);
					const char* zoneName = pZoneInfo ? pZoneInfo->LongName : "(null)";

					if (zoneConnectionData->zoneIdentifier)
						sprintf_s(title, "Zone Connection - %s - %d", zoneName, zoneConnectionData->zoneIdentifier);
					else
						sprintf_s(title, "Zone Connection - %s", zoneName);
				}

				ImGui::TextColored(MQColor(255, 255, 0).ToImColor(), title);
				ImGui::Separator();

				ImGui::Text("Reference ID: %d", selectedRef);
				ImGui::Text("Status:"); ImGui::SameLine(0.0f, 4.0f);
				if (customRefData)
				{
					if (customRefData->type == CFindLocationWndOverride::CustomRefType::Added)
						ImGui::TextColored(s_addedLocationColor.ToImColor(), "Added by EasyFind");
					else if (customRefData->type == CFindLocationWndOverride::CustomRefType::Modified)
						ImGui::TextColored(s_modifiedLocationColor.ToImColor(), "Modified by EasyFind");
				}
				else
				{
					ImGui::TextColored(MQColor(127, 127, 127).ToImColor(), "Unmodified");
				}

				if (ImGui::Button("EasyFind"))
				{
					findLocWnd->FindLocationByRefNum(selectedRef, false);
				}

				ImGui::SameLine();
				if (ImGui::Button("Group EasyFind"))
				{
					findLocWnd->FindLocationByRefNum(selectedRef, true);
				}

				ImGui::NewLine();
				ImGui::TextColored(MQColor("#D040FF").ToImColor(), "Find Location Data:");
				ImGui::Separator();

				if (ref->type == FindLocation_Player)
				{
					int playerId = ref->index;

					// Find the FindPlayerData with this playerId
					if (playerData)
					{
						ImGui::Text("Name: %s", playerData->name.c_str());
						ImGui::Text("Description: %s", playerData->description.c_str());
						ImGui::Text("Spawn ID: %d", playerData->spawnId);
						ImGui::Text("Race: %d", playerData->race);
						ImGui::Text("Class: %d", playerData->Class);
					}
					else
					{
						ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Could not find player '%d'", playerId);
					}
				}
				else if (ref->type == FindLocation_Switch || ref->type == FindLocation_Location)
				{
					const CFindLocationWnd::FindZoneConnectionData& data = findLocWnd->unfilteredZoneConnectionList[ref->index];

					DrawFindZoneConnectionData(data);
				}
				else
				{
					ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Unhandled location type!");
				}

				if (customRefData && customRefData->type == CFindLocationWndOverride::CustomRefType::Modified
					&& (ref->type == FindLocation_Switch || ref->type == FindLocation_Location))
				{
					ImGui::NewLine();
					ImGui::TextColored(MQColor("#D040FF").ToImColor(), "Original Data:");
					ImGui::Separator();

					auto origIter = findLocWnd->sm_originalZoneConnections.find(ref->index);
					if (origIter != findLocWnd->sm_originalZoneConnections.end())
					{
						const CFindLocationWnd::FindZoneConnectionData& data = origIter->second;

						DrawFindZoneConnectionData(data);
					}
					else
					{
						ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Could not find original data!");
					}
				}

				if (customRefData && customRefData->data)
				{
					ImGui::NewLine();
					ImGui::TextColored(MQColor("#D040FF").ToImColor(), "EasyFind Data:");
					ImGui::Separator();

					if (!s_luaCodeViewer)
					{
						s_luaCodeViewer = new imgui::TextEditor();
						s_luaCodeViewer->SetLanguageDefinition(imgui::texteditor::LanguageDefinition::Lua());
						s_luaCodeViewer->SetPalette(imgui::TextEditor::GetDarkPalette());
						s_luaCodeViewer->SetReadOnly(true);
						s_luaCodeViewer->SetRenderLineNumbers(false);
						s_luaCodeViewer->SetRenderCursor(false);
						s_luaCodeViewer->SetShowWhitespace(false);
					}

					const FindableLocation* data = customRefData->data;

					ImGui::Text("Type:"); ImGui::SameLine(0.0f, 4.0f);
					ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", LocationTypeToString(data->easyfindType));

					if (!data->name.empty())
					{
						ImGui::Text("Name:"); ImGui::SameLine(0.0f, 4.0f);
						ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", data->name.c_str());
					}

					if (!data->spawnName.empty())
					{
						ImGui::Text("Spawn Name:"); ImGui::SameLine(0.0f, 4.0f);
						ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", data->spawnName.c_str());
					}

					ImGui::Text("Location:"); ImGui::SameLine(0.0f, 4.0f);
					ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "(%.2f, %.2f, %.2f)", data->location.x, data->location.y, data->location.z);

					ImGui::Text("Switch ID: %d", data->switchId);
					ImGui::Text("Switch Name: %s", data->switchName.c_str());

					EQZoneInfo* pZoneInfo = pWorldData->GetZone(data->zoneId);
					const char* zoneName = pZoneInfo ? pZoneInfo->ShortName : "(null)";

					ImGui::Text("Target Zone: %s (%d)", zoneName, data->zoneId);
					ImGui::Text("Zone Identifier: %d", data->zoneIdentifier);

					if (!data->translocatorKeyword.empty())
					{
						ImGui::Text("Translocator Keyword: %s", data->translocatorKeyword.c_str());
					}

					bool replace = data->replace;
					ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.6f);
					ImGui::Checkbox("Replace Original", &replace);
					ImGui::PopStyleVar();

					if (!data->luaScript.empty())
					{
						ImGui::NewLine();
						ImGui::Text("Lua Script");
						ImGui::Separator();
						ImGui::PushFont(imgui::ConsoleFont);

						if (changedRef)
						{
							s_luaCodeViewer->SetText(data->luaScript);
						}

						s_luaCodeViewer->Render("Script", ImGui::GetContentRegionAvail());
						ImGui::PopFont();
					}
				}
			}
		}

		ImGui::EndChild();
	}
	ImGui::EndGroup();
}

static void DrawEasyFindZonePathGeneration()
{
	static char fromZone[256] = { 0 };
	ImGui::InputText("Starting Zone", fromZone, 256);

	if (ImGui::Button("Use Current"))
	{
		if (EQZoneInfo* pZone = pWorldData->GetZone(ZoneGuideManagerClient::Instance().currentZone))
			strcpy_s(fromZone, pZone->ShortName);
	}

	static char toZone[256] = { 0 };
	ImGui::InputText("Destination Zone", toZone, 256);

	if (ImGui::Button("Use Current"))
	{
		if (EQZoneInfo* pZone = pWorldData->GetZone(ZoneGuideManagerClient::Instance().currentZone))
			strcpy_s(toZone, pZone->ShortName);
	}

	if (ImGui::Button("Swap"))
	{
		static char tempZone[256];
		strcpy_s(tempZone, fromZone);
		strcpy_s(fromZone, toZone);
		strcpy_s(toZone, tempZone);
	}

	ImGui::Separator();

	EQZoneInfo* pFromZone = pWorldData->GetZone(GetZoneID(fromZone));
	ImGui::Text("From Zone:"); ImGui::SameLine(0.0f, 4.0f);
	if (pFromZone)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%d)", pFromZone->LongName, pFromZone->Id);
	}
	else
	{
		ImGui::TextColored(MQColor(255, 255, 255, 127).ToImColor(), "(none)");
	}

	EQZoneInfo* pToZone = pWorldData->GetZone(GetZoneID(toZone));
	ImGui::Text("To Zone:"); ImGui::SameLine(0.0f, 4.0f);
	if (pToZone)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%d)", pToZone->LongName, pToZone->Id);
	}
	else
	{
		ImGui::TextColored(MQColor(255, 255, 255, 127).ToImColor(), "(none)");
	}

	if (ImGui::Button("Generate"))
	{
		if (pFromZone && pToZone)
		{
			s_zonePathTest = GeneratePathToZone(pFromZone->Id, pToZone->Id);
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Clear"))
	{
		s_zonePathTest.clear();
	}

	if (!s_zonePathTest.empty())
	{
		if (ImGui::BeginTable("##Entries", 2))
		{
			ImGui::TableSetupColumn("Zone");
			ImGui::TableSetupColumn("Transfer type");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const ZonePathData& data : s_zonePathTest)
			{
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%s", pWorldData->GetZone(data.zoneId)->LongName);

				ImGui::TableNextColumn();
				ImGui::Text("%s", ZoneGuideManagerClient::Instance().GetZoneTransferTypeNameByIndex(data.transferTypeIndex).c_str());
			}

			ImGui::EndTable();
		}

		if (ImGui::Button("Set Active"))
		{
			SetActiveZonePath(s_zonePathTest);
		}
	}
}

PLUGIN_API void OnUpdateImGui()
{
	if (!s_showMenu)
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 440), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("EasyFind", &s_showMenu, ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Reload Configuration"))
				{
					ReloadSettings();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		if (ImGui::BeginTabBar("EasyFindTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("Zone Connections"))
			{
				DrawEasyFindZoneConnections();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Zone Path Generation"))
			{
				DrawEasyFindZonePathGeneration();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}

#pragma endregion

//============================================================================
//============================================================================

#pragma region Commands

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
		return;
	}

	if (ci_equals(searchArg, "migrate"))
	{
		if (MigrateConfigurationFile(true))
		{
			SaveConfigurationFile();
			LoadSettings();
		}

		return;
	}

	if (ci_equals(searchArg, "reload"))
	{
		ReloadSettings();
		return;
	}

	if (ci_equals(searchArg, "ui"))
	{
		s_showMenu = !s_showMenu;
		return;
	}

	// TODO: group command support.

	pFindLocationWnd.get_as<CFindLocationWndOverride>()->FindLocation(searchArg, false);
}

void Command_TravelTo(SPAWNINFO* pSpawn, char* szLine)
{

}

#pragma endregion

#pragma region Plugin Callbacks

void InitializeNavigation()
{
	s_nav = (nav::NavAPI*)GetPluginInterface("MQ2Nav");
	if (s_nav)
	{
		s_navObserverId = s_nav->RegisterNavObserver(NavObserverCallback, nullptr);
	}
}

void ShutdownNavigation()
{
	if (s_nav)
	{
		s_nav->UnregisterNavObserver(s_navObserverId);
		s_navObserverId = 0;
	}

	s_nav = nullptr;
}

void InitializeLua()
{
	s_lua = (mq::lua::LuaPluginInterface*)GetPluginInterface("MQ2Lua");
}

void ShutdownLua()
{
	s_lua = nullptr;
}

PLUGIN_API void InitializePlugin()
{
	DebugSpewAlways("MQ2EasyFind::Initializing version %f", MQ2Version);

	s_configFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.yaml").string();
	InitializeNavigation();
	InitializeLua();

	LoadSettings();

	if (pFindLocationWnd)
	{
		// Install override onto the FindLocationWnd
		CFindLocationWndOverride::InstallHooks(pFindLocationWnd);
	}

	AddCommand("/easyfind", Command_EasyFind, false, true, true);
	AddCommand("/travelto", Command_TravelTo, false, true, true);
}

PLUGIN_API void ShutdownPlugin()
{
	RemoveCommand("/easyfind");
	RemoveCommand("/travelto");

	ShutdownNavigation();
	ShutdownLua();

	delete s_luaCodeViewer;
}

PLUGIN_API void OnCleanUI()
{
	CFindLocationWndOverride::RemoveHooks(pFindLocationWnd);
}

PLUGIN_API void OnReloadUI()
{
	if (pFindLocationWnd)
	{
		CFindLocationWndOverride::InstallHooks(pFindLocationWnd);
	}
}

PLUGIN_API void SetGameState(int GameState)
{
	if (GameState != GAMESTATE_INGAME)
	{
		Navigation_Reset();
	}
}

PLUGIN_API void OnPulse()
{
	Navigation_Pulse();
}

PLUGIN_API void OnBeginZone()
{
}

PLUGIN_API void OnEndZone()
{
}

PLUGIN_API void OnZoned()
{
}


PLUGIN_API void OnMacroStart(const char* Name)
{
}

PLUGIN_API void OnMacroStop(const char* Name)
{
}

PLUGIN_API void OnLoadPlugin(const char* Name)
{
	if (ci_equals(Name, "MQ2Nav"))
	{
		InitializeNavigation();
	}

	if (ci_equals(Name, "MQ2Lua"))
	{
		InitializeLua();
	}
}

PLUGIN_API void OnUnloadPlugin(const char* Name)
{
	if (ci_equals(Name, "MQ2Nav"))
	{
		ShutdownNavigation();
	}

	if (ci_equals(Name, "MQ2Lua"))
	{
		ShutdownLua();
	}
}

#pragma endregion
