
#include <mq/Plugin.h>

#include "eqlib/WindowOverride.h"
#include "MQ2Nav/PluginAPI.h"

#include <fstream>
#include <optional>

#pragma warning( push )
#pragma warning( disable:4996 )
#include <yaml-cpp/yaml.h>
#pragma warning( pop )

PreSetup("MQ2EasyFind");
PLUGIN_VERSION(1.0);

#define PLUGIN_MSG "\ag[MQ2EasyFind]\ax "
#define DEBUG_MSGS 0

// Limit the rate at which we update the distance to findable locations
constexpr std::chrono::milliseconds distanceCalcDelay = std::chrono::milliseconds{ 100 };

// configuration
static std::string s_configFile;
static YAML::Node s_configNode;
static bool s_enableCustomLocations = true;

static bool s_allowFollowPlayerPath = false;
static bool s_navNextPlayerPath = false;
static std::chrono::steady_clock::time_point s_playerPathRequestTime = {};
static bool s_performCommandFind = false;
static bool s_performGroupCommandFind = false;

static const MQColor s_addedLocationColor(255, 192, 64);
static const MQColor s_modifiedLocationColor(64, 192, 255);

static nav::NavAPI* s_nav = nullptr;
static int s_navObserverId = 0;

// loaded configuration information
struct FindableLocation
{
	FindLocationType type;

	// for findable locations (zone connections and POIs, optional for switches)
	CVector3 location;
	CXStr name;

	// for POIs
	CXStr description;

	// for zone connections
	uint32_t switchId = 0;
	std::string switchName;

	EQZoneIndex zoneId = 0;
	int zoneIdentifier = 0;

	// if false, we won't replace. only add if it doesn't already exist.
	bool replace = false;

	// The EQ version of this location, if it exists, and data for the ui
	CFindLocationWnd::FindZoneConnectionData eqZoneConnectionData;
	bool skip = false;
	bool initialized = false;
	CXStr listCategory;
	CXStr listDescription;
};

using FindableLocations = std::vector<FindableLocation>;
FindableLocations s_findableLocations;

static bool MigrateConfigurationFile(bool force = false);
static void ReloadSettings();

//============================================================================

struct FindLocationRequestState
{
	// The request
	bool valid = false;
	int spawnID = 0;
	glm::vec3 location;
	EQSwitch* pSwitch = nullptr;
	bool asGroup = false;
	FindableLocation findableLocation;

	// state while processing
};

static FindLocationRequestState s_activeFindState;

bool ExecuteNavCommand(const FindLocationRequestState& request)
{
	s_activeFindState = request;

	// spawnID:
	//sprintf_s(command, "spawn id %d | dist=15 log=warning tag=easyfind", ref->index);

	// location:
	//sprintf_s(command, "locyxz %.2f %.2f %.2f log=warning tag=easyfind", coord.X, coord.Y, coord.Z);

	// door?
	//sprintf_s(command, "door id %d click log=warning tag=easyfind", zoneConn.id);

	// Adjust z coordinate to the ground
	//request.location.z = pDisplay->GetFloorHeight(request.location.x, request.location.y, request.location.z, 2.0f);

	//if (s_nav)
	//{
	//	s_nav->ExecuteNavCommand(szLine);
	//}

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

	if (eventType == nav::NavObserverEvent::NavDestinationReached)
	{
	}
}

//============================================================================

class CFindLocationWndOverride : public WindowOverride<CFindLocationWndOverride, CFindLocationWnd>
{
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

public:
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
					// replacing switch with non-switch
					if (listRef->type == FindLocation_Switch && findableLocation.type != FindLocation_Switch)
					{
					}

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

	void AddCustomLocations(bool initial)
	{
		if (sm_customLocationsAdded)
			return;
		if (!s_enableCustomLocations)
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

					// Search for an existing zone entry that matches this one.
					for (FindZoneConnectionData& entry : unfilteredZoneConnectionList)
					{
						if (entry.zoneId == location.zoneId && entry.zoneIdentifier == location.zoneIdentifier)
						{
							// Its a connection representing the same thing.
							if (location.replace)
							{
								// We think we have a Location, but the game has a switch. turn our entry into a switch and copy the switch id
								if (entry.type == FindLocation_Switch && location.type == FindLocation_Location)
								{
									location.type = FindLocation_Switch;
									location.switchId = entry.id;
								}
							}
							else
							{
								location.skip = true;
							}
						}
					}

					if (location.type == FindLocation_Switch)
					{
						if (!location.switchName.empty())
						{
							// this is our opportunity to override the switch merge above, and turn it back into a location.
							if (ci_equals(location.switchName, "none"))
							{
								location.type = FindLocation_Location;
							}
							else
							{
								EQSwitch* pSwitch = FindSwitchByName(location.switchName.c_str());

								if (pSwitch)
								{
									location.eqZoneConnectionData.id = pSwitch->ID;
								}
							}
						}
						else
						{
							location.eqZoneConnectionData.id = location.switchId;
						}
					}

					location.eqZoneConnectionData.subId = 0;
					location.eqZoneConnectionData.zoneId = location.zoneId;
					location.eqZoneConnectionData.zoneIdentifier = location.zoneIdentifier;
					location.eqZoneConnectionData.type = location.type;

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
								if (entry.first.index > refData.index)
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

		sm_customLocationsAdded = false;
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

					int refId = (int)findLocationList->GetItemData(selectedRow);

					// Try to perform the navigation. If we succeed, bail out. Otherwise trigger the
					// navigation via player path.
					if (PerformFindWindowNavigation(refId, groupNav))
					{
						Show(false);
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
			const FindZoneConnectionData& zoneConn = unfilteredZoneConnectionList[ref->index];

			if (ref->type == FindLocation_Location)
			{
				found = true;
				return zoneConn.location;
			}
			else
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
				if (customLocation)
					request.findableLocation = *customLocation;

				ExecuteNavCommand(request);
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

			EQSwitch* pSwitch = nullptr;
			if (switchId)
			{
				pSwitch = pSwitchMgr->GetSwitchById(switchId);
			}

			FindLocationRequestState request;
			request.location = *(glm::vec3*)&zoneConn.location;
			request.pSwitch = pSwitch;
			request.asGroup = asGroup;
			if (customLocation)
				request.findableLocation = *customLocation;

			if (pSwitch)
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax (via switch \ao%s\ax)", szLocationName, pSwitch->Name);
			}
			else
			{
				WriteChatf(PLUGIN_MSG "Navigating to \ayZone Connection\ax: \ag%s\ax", szLocationName);
			}

			ExecuteNavCommand(request);
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

	void LoadZoneSettings()
	{
		RemoveCustomLocations();

		if (!pZoneInfo)
			return;

		EQZoneInfo* zoneInfo = pWorldData->GetZone(pZoneInfo->ZoneID);
		if (!zoneInfo)
			return;
		const char* shortName = zoneInfo->ShortName;

		try
		{
			// Load objects from the AddFindLocations block
			YAML::Node addFindLocations = s_configNode["FindLocations"];
			if (addFindLocations.IsMap())
			{
				YAML::Node zoneNode = addFindLocations[shortName];
				if (zoneNode.IsDefined())
				{
					s_findableLocations = zoneNode.as<FindableLocations>();
				}
			}
		}
		catch (const YAML::Exception& ex)
		{
			// failed to parse, notify and return
			WriteChatf(PLUGIN_MSG "\arFailed to load zone settings for %s: %s", shortName, ex.what());
			return;
		}
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
		MigrateConfigurationFile(true);
		return;
	}

	if (ci_equals(searchArg, "reload"))
	{
		ReloadSettings();
		return;
	}

	// TODO: group command support.

	pFindLocationWnd.get_as<CFindLocationWndOverride>()->FindLocation(searchArg, false);
}

//----------------------------------------------------------------------------

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
						CVector3 position;

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

							position = CVector3(x, y, z);
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
	struct convert<CVector3> {
		static Node encode(const CVector3& vec) {
			Node node;
			node.push_back(vec.X);
			node.push_back(vec.Y);
			node.push_back(vec.Z);
			return node;
		}

		static bool decode(const Node& node, CVector3& vec) {
			if (!node.IsSequence() || node.size() != 3) {
				return false;
			}
			vec.X = node[0].as<float>();
			vec.Y = node[1].as<float>();
			vec.Z = node[2].as<float>();
			return true;
		}
	};

	template <>
	struct convert<FindableLocation> {
		static Node encode(const FindableLocation& data) {
			Node node;
			// todo
			return node;
		}
		static bool decode(const Node& node, FindableLocation& data) {
			if (!node.IsMap()) {
				return false;
			}

			std::string type = node["type"].as<std::string>();

			if (type == "ZoneConnection")
			{
				// If a location is provided, then it is a location.
				if (node["location"].IsDefined())
				{
					data.location = node["location"].as<CVector3>();
					data.type = FindLocation_Location;
				}
				else if (node["switch"].IsDefined())
				{
					YAML::Node switchNode = node["switch"];

					// first try to read as int, then as string.
					data.switchId = switchNode.as<int>(0);
					if (data.switchId == 0)
					{
						data.switchName = switchNode.as<std::string>();
					}

					data.type = FindLocation_Switch;
				}

				// common props

				// read zone name (or id)
				int zoneId = node["targetZone"].as<int>(-1);
				if (zoneId == -1)
				{
					zoneId = GetZoneID(node["targetZone"].as<std::string>().c_str());
				}

				data.zoneId = (EQZoneIndex)zoneId;
				data.zoneIdentifier = node["identifier"].as<int>(0);
				data.name = node["name"].as<std::string>(std::string());
				data.replace = node["replace"].as<bool>(true);
				return true;
			}
			else if (type == "POI")
			{
				data.location = node["location"].as<CVector3>();
				data.name = node["name"].as<std::string>(std::string());
				data.description = node["description"].as<std::string>(std::string());
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

void InitializeNavigation()
{
	s_nav = nav::GetNavAPIFromPlugin();
	if (s_nav)
	{
		s_navObserverId = s_nav->RegisterNavObserver(NavObserverCallback, nullptr);
	}
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

	s_configFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.yaml").string();
	InitializeNavigation();

	LoadSettings();

	if (MigrateConfigurationFile())
	{
		SaveConfigurationFile();
		LoadSettings();
	}

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

	if (s_nav)
	{
		s_nav->UnregisterNavObserver(s_navObserverId);
		s_navObserverId = 0;
	}
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
	if (GameState != GAMESTATE_INGAME)
	{
		Navigation_Reset();
	}
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
	Navigation_Pulse();
}

/**
 * @fn OnBeginZone
 *
 * This is called just after entering a zone line and as the loading screen appears.
 */
PLUGIN_API void OnBeginZone()
{
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
	if (ci_equals(Name, "MQ2Nav"))
	{
		InitializeNavigation();
	}
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
	if (ci_equals(Name, "MQ2Nav"))
	{
		s_nav = nullptr;
		s_navObserverId = 0;
	}
}
