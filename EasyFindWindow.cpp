
#include "EasyFindWindow.h"
#include "EasyFindConfiguration.h"
#include "EasyFindZoneConnections.h"

//----------------------------------------------------------------------------
//
// Limit the rate at which we update the distance to findable locations
static constexpr std::chrono::milliseconds s_distanceCalcDelay = std::chrono::milliseconds{ 100 };

static bool s_performCommandFind = false;
static bool s_performGroupCommandFind = false;
static FindableLocations s_findableLocations;
static bool s_findableLocationsDirty = false;

//============================================================================

int CFindLocationWndOverride::OnProcessFrame()
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

	if (s_findableLocationsDirty)
	{
		RemoveCustomLocations();
		s_findableLocationsDirty = false;
	}

	// if didRebuild is true, then this will reset the refs list.
	int result = Super::OnProcessFrame();
	bool updateColors = false;

	if (sm_displayDistanceColumn != g_configuration->IsDistanceColumnEnabled())
	{
		sm_displayDistanceColumn = g_configuration->IsDistanceColumnEnabled();

		if (sm_displayDistanceColumn)
		{
			AddDistanceColumn();
		}
		else
		{
			RemoveDistanceColumn();

			updateColors = true;
		}
	}

	if (sm_displayColors != g_configuration->IsColoredFindWindowEnabled()
		|| sm_addedColor != g_configuration->GetColor(ConfiguredColor::AddedLocation)
		|| sm_modifiedColor != g_configuration->GetColor(ConfiguredColor::ModifiedLocation))
	{
		sm_displayColors = g_configuration->IsColoredFindWindowEnabled();
		sm_addedColor = g_configuration->GetColor(ConfiguredColor::AddedLocation);
		sm_modifiedColor = g_configuration->GetColor(ConfiguredColor::ModifiedLocation);

		updateColors = true;
	}

	// Update distance column. this will internally skip work if necessary.
	UpdateDistanceColumn();

	// Ensure that we wait for spawns to be populated into the spawn map first.
	if (zoneConnectionsRcvd && !sm_customLocationsAdded && gSpawnCount > 0)
	{
		AddCustomLocations(true);

		if (findLocationList->GetItemCount() > 0 && !findLocationList->IsVisible())
		{
			findLocationList->SetVisible(true);
			noneLabel->SetVisible(false);
		}
	}

	if (sm_customLocationsAdded && (!sm_queuedSearchTerm.empty() || sm_queuedZoneId != 0))
	{
		if (sm_queuedZoneId != 0)
		{
			FindZoneConnectionByZoneIndex(sm_queuedZoneId, sm_queuedGroupParam);
		}
		else
		{
			FindLocation(sm_queuedSearchTerm, sm_queuedGroupParam);
		}

		sm_queuedSearchTerm.clear();
		sm_queuedGroupParam = false;
		sm_queuedZoneId = 0;
	}

	if (updateColors)
	{
		// update the list
		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			UpdateListRowColor(i);
		}
	}

	return result;
}

bool CFindLocationWndOverride::AboutToShow()
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

int CFindLocationWndOverride::OnZone()
{
	// Reset any temporary state. When we zone everything is destroyed and we start over.
	sm_customLocationsAdded = false;
	sm_customRefs.clear();
	sm_originalZoneConnections.clear();

	int result = Super::OnZone();

	LoadZoneConnections();

	return result;
}

uint32_t CFindLocationWndOverride::GetAvailableId()
{
	lastId++;

	while (referenceList.FindFirst(lastId))
		lastId++;

	return lastId;
}

void CFindLocationWndOverride::AddZoneConnection(const FindableLocation& findableLocation)
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

				SPDLOG_DEBUG("\ayReplaced {} - {} with custom data", findableLocation.listCategory, findableLocation.listDescription);
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
		cell.Color = (COLORREF)g_configuration->GetColor(ConfiguredColor::AddedLocation);

	findLocationList->AddLine(&line);

	SPDLOG_DEBUG("\aoAdded {} - {} with id {}", findableLocation.listCategory, findableLocation.listDescription, refId);
}

void CFindLocationWndOverride::AddCustomLocations(bool initial)
{
	if (sm_customLocationsAdded)
		return;
	if (!pLocalPC)
		return;

	for (FindableLocation& location : s_findableLocations)
	{
		if (!location.initialized)
		{
			// check requirements
			if (location.parsedData)
			{
				if (!location.parsedData->CheckRequirements())
					continue;
			}

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
						SPDLOG_ERROR("Failed to create translocator connection: Could not find \"\ay{}\ax\"!", location.spawnName);
						continue;
					}
				}

				location.eqZoneConnectionData.zoneId = location.zoneId;
				location.eqZoneConnectionData.zoneIdentifier = location.zoneIdentifier;
				if (location.location.has_value())
					location.eqZoneConnectionData.location = CVector3(location.location->x, location.location->y, location.location->z);

				if (location.name.empty())
				{
					CXStr name = GetFullZone(location.zoneId);
					location.listDescription = name;
				}
				else
				{
					location.listDescription = location.name;
				}

				if (location.zoneIdentifier)
					location.listDescription.append(fmt::format(" - {}", location.zoneIdentifier));
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

void CFindLocationWndOverride::RemoveCustomLocations()
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

void CFindLocationWndOverride::UpdateListRowColor(int row)
{
	int listRefId = (int)findLocationList->GetItemData(row);

	auto iter = sm_customRefs.find(listRefId);
	if (iter != sm_customRefs.end())
	{
		CustomRefType type = iter->second.type;
		SListWndLine& line = findLocationList->ItemsArray[row];

		for (SListWndCell& cell : line.Cells)
		{
			if (sm_displayColors)
			{
				if (type == CustomRefType::Added)
					cell.Color = (COLORREF)sm_addedColor;
				else if (type == CustomRefType::Modified)
					cell.Color = (COLORREF)sm_modifiedColor;
			}
			else
			{
				cell.Color = (COLORREF)MQColor(255, 255, 255);
			}
		}
	}
}

int CFindLocationWndOverride::WndNotification(CXWnd* sender, uint32_t message, void* data)
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
				bool groupNav = (pWndMgr->IsShiftKey() && !s_performCommandFind) || s_performGroupCommandFind;

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
					SPDLOG_ERROR("Cannot use standard find window functionality for custom locations.");
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

void CFindLocationWndOverride::UpdateDistanceColumn()
{
	if (sm_distanceColumn == -1)
		return;

	auto now = std::chrono::steady_clock::now();
	bool periodicUpdate = false;

	if (now - sm_lastDistanceUpdate > s_distanceCalcDelay)
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

CFindLocationWnd::FindableReference* CFindLocationWndOverride::GetReferenceForListIndex(int index) const
{
	int refId = (int)findLocationList->GetItemData(index);
	FindableReference* ref = referenceList.FindFirst(refId);

	return ref;
}

CVector3 CFindLocationWndOverride::GetReferencePosition(FindableReference* ref, bool& found)
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

		if (zoneConn.location.X != 0.0f || zoneConn.location.Y != 0.0f || zoneConn.location.Z != 0.0f)
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
bool CFindLocationWndOverride::PerformFindWindowNavigation(int refId, bool asGroup)
{
	if (!Navigation_IsInitialized())
	{
		SPDLOG_ERROR("Navigation requires the MQ2Nav plugin to be loaded.");
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
			if (asGroup)
			{
				// If we do this as a group, we execute via a command instead. This ensures that all
				// members experience the same result (hopefully).
				std::string command = fmt::format("/easyfind {}", pSpawn->DisplayedName);
				DoGroupCommand(command, true);
				return true;
			}

			std::string name;
			if (pSpawn->Lastname[0] && pSpawn->Type == SPAWN_NPC)
				name = fmt::format("{} ({})", pSpawn->DisplayedName, pSpawn->Lastname);
			else
				name = pSpawn->DisplayedName;

			if (pSpawn->Type == SPAWN_PLAYER)
				SPDLOG_INFO("Navigating to \ayPlayer:\ax \ag{}", name);
			else
				SPDLOG_INFO("Navigating to \aySpawn:\ax \ag{}", name);

			FindLocationRequestState request;
			request.spawnID = ref->index;
			request.type = ref->type;
			request.name = std::move(name);
			if (customLocation)
				request.findableLocation = std::make_shared<FindableLocation>(*customLocation);

			Navigation_ExecuteCommand(std::move(request));
			return true;
		}

		return false;

	case FindLocation_Location:
	case FindLocation_Switch: {
		if (ref->index >= (uint32_t)unfilteredZoneConnectionList.GetCount())
		{
			SPDLOG_ERROR("Unexpected error: zone connection index is out of range!");
			return false;
		}

		const FindZoneConnectionData& zoneConn = unfilteredZoneConnectionList[ref->index];

		int32_t switchId = 0;
		if (ref->type == FindLocation_Switch)
		{
			switchId = zoneConn.id;
		}

		std::string locationName;
		EQZoneInfo* pZoneInfo = pWorldData->GetZone(zoneConn.zoneId);
		if (pZoneInfo)
			locationName = pZoneInfo->LongName;
		else
			locationName = fmt::format("Unknown({})", (int)zoneConn.zoneId);
		if (zoneConn.zoneIdentifier > 0)
			locationName.append(fmt::format(" - {}", zoneConn.zoneIdentifier));

		EQSwitch* pSwitch = nullptr;
		if (ref->type == FindLocation_Switch)
		{
			pSwitch = pSwitchMgr->GetSwitchById(switchId);
		}

		if (asGroup)
		{
			// If we do this as a group, we execute via a command instead. This ensures that all
			// members experience the same result (hopefully).
			std::string command = fmt::format("/easyfind {}", locationName);
			DoGroupCommand(command, true);
			return true;
		}

		FindLocationRequestState request;
		request.location = *(glm::vec3*)&zoneConn.location;
		request.switchID = switchId;
		request.type = ref->type;
		request.zoneId = zoneConn.zoneId;

		if (customLocation)
			request.findableLocation = std::make_shared<FindableLocation>(*customLocation);

		if (pSwitch && g_configuration->IsVerboseMessages())
		{
			SPDLOG_INFO("Navigating to \ayZone Connection\ax: \ag{}\ax (via switch \ao{}\ax)", locationName, pSwitch->Name);
		}
		else
		{
			SPDLOG_INFO("Navigating to \ayZone Connection\ax: \ag{}\ax", locationName);
		}

		request.name = std::move(locationName);

		Navigation_ExecuteCommand(std::move(request));
		return true;
	}

	default:
		SPDLOG_ERROR("Cannot navigate to selection type: {}", ref->type);
		break;
	}

	return false;
}

MQColor CFindLocationWndOverride::GetColorForReference(int refId)
{
	MQColor color(255, 255, 255);

	auto iter = sm_customRefs.find(refId);
	if (iter != sm_customRefs.end())
	{
		if (iter->second.type == CustomRefType::Added)
			color = g_configuration->GetColor(ConfiguredColor::AddedLocation);
		else if (iter->second.type == CustomRefType::Modified)
			color = g_configuration->GetColor(ConfiguredColor::ModifiedLocation);
	}

	return color;
}

CFindLocationWndOverride::RefData* CFindLocationWndOverride::GetCustomRefData(int refId)
{
	auto iter = sm_customRefs.find(refId);
	if (iter != sm_customRefs.end())
	{
		return &iter->second;
	}

	return nullptr;
}

const CFindLocationWnd::FindZoneConnectionData* CFindLocationWndOverride::GetOriginalZoneConnectionData(int index)
{
	auto origIter = sm_originalZoneConnections.find(index);
	if (origIter != sm_originalZoneConnections.end())
	{
		return &origIter->second;
	}

	return nullptr;
}

bool CFindLocationWndOverride::FindLocationByListIndex(int listIndex, bool group)
{
	// Perform navigaiton by triggering a selection in the list.
	s_performCommandFind = true;
	s_performGroupCommandFind = group;

	findLocationList->SetCurSel(listIndex);
	findLocationList->ParentWndNotification(findLocationList, XWM_LCLICK, (void*)listIndex);

	s_performCommandFind = false;
	s_performGroupCommandFind = false;
	return true;
}

void CFindLocationWndOverride::FindLocationByRefNum(int refNum, bool group)
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

	SPDLOG_ERROR("Couldn't find location by ref: {}", refNum);
}

bool CFindLocationWndOverride::FindZoneConnectionByZoneIndex(EQZoneIndex zoneId, bool group)
{
	if (!sm_customLocationsAdded)
	{
		sm_queuedZoneId = sm_queuedZoneId;
		sm_queuedGroupParam = group;

		SPDLOG_WARN("Waiting for connections to be loaded!");
		return true;
	}

	// look for exact match by zone id
	int foundIndex = FindClosestLocation(
		[&](int index)
		{
			int refId = (int)findLocationList->GetItemData(index);
			FindableReference* ref = referenceList.FindFirst(refId);
			if (!ref) return false;

			if (ref->type == FindLocation_Location || ref->type == FindLocation_Switch)
			{
				FindZoneConnectionData& connData = unfilteredZoneConnectionList[ref->index];
				return connData.zoneId == zoneId;
			}

			return false;
		});

	if (foundIndex == -1)
	{
		EQZoneInfo* pZoneInfo = pWorldData->GetZone(zoneId);

		SPDLOG_ERROR("Could not find connection to \"\ay{}\ax\".", pZoneInfo ? pZoneInfo->LongName : "Unknown");
		return false;
	}

	return FindLocationByListIndex(foundIndex, group);
}

bool CFindLocationWndOverride::FindLocation(std::string_view searchTerm, bool group)
{
	if (!sm_customLocationsAdded)
	{
		sm_queuedSearchTerm = searchTerm;
		sm_queuedGroupParam = group;

		SPDLOG_WARN("Waiting for connections to be loaded!");
		return true;
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
			[&](int index) { return ci_find_substr(findLocationList->GetItemText(index, 1), searchTerm) != -1; });
		if (foundIndex != -1)
		{
			SPDLOG_INFO("Finding closest point matching \"\ay{}\ax\".", searchTerm);
		}
	}

	if (foundIndex == -1)
	{
		SPDLOG_ERROR("Could not find \"\ay{}\ax\".", searchTerm);
		return false;
	}

	return FindLocationByListIndex(foundIndex, group);
}

void CFindLocationWndOverride::AddDistanceColumn()
{
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
}

void CFindLocationWndOverride::RemoveDistanceColumn()
{
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

	sm_distanceColumn = -1;
}

void CFindLocationWndOverride::OnHooked()
{
	sm_addedColor = g_configuration->GetColor(ConfiguredColor::AddedLocation);
	sm_modifiedColor = g_configuration->GetColor(ConfiguredColor::ModifiedLocation);

	if (!findLocationList)
		return;

	if (sm_displayDistanceColumn)
		AddDistanceColumn();

	SetWindowText("Find Window (Ctrl+Click to Navigate)");
	LoadZoneConnections();
}

void CFindLocationWndOverride::OnAboutToUnhook()
{
	if (!findLocationList)
		return;

	RemoveCustomLocations();
	RemoveDistanceColumn();

	SetWindowText("Find Window");
}

void CFindLocationWndOverride::LoadZoneConnections()
{
	if (!pWorldData || !pLocalPC)
		return;

	EQZoneInfo* zoneInfo = pWorldData->GetZone(pLocalPC->currentZoneId);
	if (!zoneInfo)
		return;

	FindableLocations newLocations;

	g_zoneConnections->CreateFindableLocations(newLocations, zoneInfo->ShortName);

	s_findableLocations = std::move(newLocations);
	s_findableLocationsDirty = true;
}

//----------------------------------------------------------------------------

void FindWindow_Initialize()
{
	s_findableLocations = {};

	if (pFindLocationWnd)
	{
		CFindLocationWndOverride::InstallHooks(pFindLocationWnd);
	}
}

void FindWindow_Shutdown()
{
	CFindLocationWndOverride::RemoveHooks(pFindLocationWnd);
}

void FindWindow_Reset()
{
	if (pFindLocationWnd)
	{
		pFindLocationWnd.get_as<CFindLocationWndOverride>()->RemoveCustomLocations();
	}
}

void FindWindow_FindLocation(std::string_view searchTerm, bool asGroup)
{
	if (pFindLocationWnd)
	{
		pFindLocationWnd.get_as<CFindLocationWndOverride>()->FindLocation(searchTerm, asGroup);
	}
}

void FindWindow_LoadZoneConnections()
{
	if (pFindLocationWnd)
	{
		pFindLocationWnd.get_as<CFindLocationWndOverride>()->LoadZoneConnections();
	}
}
