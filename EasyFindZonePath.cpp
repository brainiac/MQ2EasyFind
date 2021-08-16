
#include "EasyFind.h"

static std::vector<ZonePathData> s_activeZonePath;

// /travelto State
static bool s_travelToActive = false;
static EQZoneIndex s_currentZone = 0;
static bool s_findNextPath = false;

// Generates a path to the zone by utilizing data from the ZoneGuideManagerClient.
std::vector<ZonePathData> GeneratePathToZone(EQZoneIndex fromZone, EQZoneIndex toZone,
	std::string& outputMessage)
{
	ZoneGuideManagerClient& zoneMgr = ZoneGuideManagerClient::Instance();

	if (fromZone == toZone)
	{
		outputMessage = "Already at target zone";
		return {};
	}

	EQZoneInfo* toZoneInfo = pWorldData->GetZone(toZone);
	if (!toZoneInfo)
	{
		outputMessage = "Invalid target zone";
		return {};
	}

	ZoneGuideZone* nextZone = nullptr;
	ZoneGuideZone* currentZone = zoneMgr.GetZone(fromZone);

	if (!currentZone)
	{
		outputMessage = "Starting zone has no valid zone connections";
		return {};
	}


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

static void FollowActiveZonePath()
{
	s_activeZonePath.clear();

	for (const ZonePathData& pathData : ZoneGuideManagerClient::Instance().activePath)
		s_activeZonePath.push_back(pathData);

	s_travelToActive = true;
	s_findNextPath = true;
}

void StopTravelTo(bool success)
{
	SetActiveZonePath({}, false);
	s_travelToActive = false;
}

static bool ActivateNextPath()
{
	// Wait to update until we have all of our locations updated.
	CFindLocationWndOverride* pFindLocWnd = pFindLocationWnd.get_as<CFindLocationWndOverride>();
	if (pFindLocWnd && pFindLocWnd->IsCustomLocationsAdded())
	{
		s_findNextPath = false;
		if (!s_activeZonePath.empty())
		{
			EQZoneIndex nextZoneId = 0;
			int transferTypeIndex = -1;

			// Find the next zone to travel to!
			for (size_t i = 0; i < s_activeZonePath.size() - 1; ++i)
			{
				if (s_activeZonePath[i].zoneId == s_currentZone)
				{
					nextZoneId = s_activeZonePath[i + 1].zoneId;
					transferTypeIndex = s_activeZonePath[i].transferTypeIndex;
					break;
				}
			}

			if (nextZoneId != 0)
			{
				if (pFindLocWnd->FindZoneConnectionByZoneIndex(nextZoneId, false))
					return true;

				StopTravelTo(false);
			}
			else
			{
				SPDLOG_ERROR("Unable to find the next zone to travel to!");
				StopTravelTo(false);
			}
		}
	}
	return false;
}

void SetActiveZonePath(const std::vector<ZonePathData>& zonePathData, bool travel)
{
	ZonePathArray pathArray(zonePathData.size());

	for (const ZonePathData& pathData : zonePathData)
	{
		pathArray.Add(pathData);
	}

	ZoneGuideManagerClient::Instance().activePath = std::move(pathArray);

	s_travelToActive = travel;
	s_findNextPath = travel;
	s_activeZonePath = zonePathData;

	if (ActivateNextPath())
	{
		if (pZonePathWnd)
		{
			pZonePathWnd->zonePathDirty = true;
			pZonePathWnd->Show(!s_activeZonePath.empty());
		}
	}
}

static void UpdateForZoneChange()
{
	// Update current zone
	if (!pFindLocationWnd || !pZonePathWnd)
		return;

	// Wait to update until we have all of our locations updated.
	CFindLocationWndOverride* pFindLocWnd = pFindLocationWnd.get_as<CFindLocationWndOverride>();
	if (!pFindLocWnd->IsCustomLocationsAdded())
		return;

	s_currentZone = pLocalPlayer->GetZoneID();

	// If zone path is active then update it.
	if (!s_activeZonePath.empty())
	{
		EQZoneIndex destZone = s_activeZonePath.back().zoneId;
		if (destZone == s_currentZone)
		{
			SPDLOG_INFO("Arrived at our destination: \ay{}\ax!", GetFullZone(destZone));
			StopTravelTo(true);
		}
		else
		{
			// Update the path if we took a wrong turn

			bool found = false;
			for (const ZonePathData& pathData : s_activeZonePath)
			{
				if (pathData.zoneId == s_currentZone)
				{
					found = true;
					break;
				}
			}

			if (!found)
			{
				std::string message;
				auto newPath = GeneratePathToZone(s_currentZone, destZone, message);
				if (newPath.empty())
				{
					SPDLOG_WARN("Path generation failed: {}", message);
				}
				SetActiveZonePath(newPath, s_travelToActive);
			}
		}

		if (s_travelToActive)
		{
			s_findNextPath = true;
		}
	}
}

void ZonePath_OnPulse()
{
	if (s_currentZone != pLocalPlayer->GetZoneID())
	{
		UpdateForZoneChange();
	}

	if (s_findNextPath)
	{
		ActivateNextPath();
	}
}

void ZonePath_NavCanceled()
{
	if (s_travelToActive)
	{
		StopTravelTo(false);

		SPDLOG_INFO("Canceling /travelto due to navigation being canceled");
	}
}

void Command_TravelTo(SPAWNINFO* pSpawn, char* szLine)
{
	ZoneGuideManagerClient& zoneGuide = ZoneGuideManagerClient::Instance();

	if (szLine[0] == 0)
	{
		if (!zoneGuide.activePath.IsEmpty())
		{
			SPDLOG_INFO("Following active zone path");
			FollowActiveZonePath();
			return;
		}

		WriteChatf(PLUGIN_MSG "Usage: /travelto [zone name]");
		WriteChatf(PLUGIN_MSG "    Attempts to find a route to the specified zone and then travels to it.");
		WriteChatf(PLUGIN_MSG "    If no argument is provided, and a zone path is active, /travelto will follow it.");
		return;
	}

	if (ci_equals(szLine, "stop"))
	{
		bool isActive = s_travelToActive || !s_activeZonePath.empty() || !zoneGuide.activePath.IsEmpty();

		if (s_travelToActive)
		{
			SPDLOG_INFO("/travelto stopped");
		}
		else
		{
			SPDLOG_WARN("No /travelto is active.");
		}

		return;
	}

	EQZoneInfo* pCurrentZone = pWorldData->GetZone(pZoneInfo->ZoneID);
	if (!pCurrentZone)
		return;

	EQZoneInfo* pTargetZone = pWorldData->GetZone(GetZoneID(szLine));
	if (!pTargetZone)
	{
		SPDLOG_ERROR("Invalid zone: {}", szLine);
		return;
	}

	std::string message;
	auto path = GeneratePathToZone(pCurrentZone->Id, pTargetZone->Id, message);
	if (path.empty())
	{
		SPDLOG_ERROR("Failed to generate path from \ay{}\ar to \ay{}\ar: {}.",
			pCurrentZone->LongName, pTargetZone->LongName, message);
		return;
	}

	SetActiveZonePath(path, true);
}
