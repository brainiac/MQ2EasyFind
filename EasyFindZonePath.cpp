
#include "EasyFind.h"
#include "EasyFindConfiguration.h"
#include "EasyFindWindow.h"
#include "EasyFindZoneConnections.h"

#include <fstream>
#include <filesystem>

static ZonePathRequest s_activeZonePathRequest;

// /travelto State
static bool s_travelToActive = false;
static EQZoneIndex s_currentZone = 0;
static bool s_findNextPath = false;

int FindTransferIndexByName(std::string_view name)
{
	ZoneGuideManagerClient& zoneMgr = ZoneGuideManagerClient::Instance();

	for (int i = 0; i < zoneMgr.transferTypes.GetLength(); ++i)
	{
		if (zoneMgr.transferTypes[i].description == name)
			return i;
	}

	return -1;
}

bool ZonePath_IsActive()
{
	return s_travelToActive;
}

// Generates a path to the zone by utilizing data from the ZoneGuideManagerClient.
std::vector<ZonePathNode> ZonePath_GeneratePath(EQZoneIndex fromZone, EQZoneIndex toZone,
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
		outputMessage = "Ending zone is not valid";
		return {};
	}

	EQZoneInfo* fromZoneInfo = pWorldData->GetZone(fromZone);
	if (!fromZoneInfo)
	{
		outputMessage = "Starting zone is not valid";
		return {};
	}

	// Implements a breadth-first search of the zone connections
	EQZoneIndex currentZoneId = pWorldData->GetZoneBaseId(fromZone);
	std::deque<EQZoneIndex> queue;
	struct ZonePathGenerationData {
		int depth = -1;
		int pathMinLevel = -1;
		int prevZoneTransferTypeIndex = -1;
		EQZoneIndex prevZone = 0;

		// information about the origin of this link
		const ParsedFindableLocation* location = nullptr;
		const ZoneGuideConnection* connection = nullptr;
	};
	std::unordered_map<EQZoneIndex, ZonePathGenerationData> pathData;

	queue.push_back(currentZoneId);
	pathData[fromZone].depth = 0;
	pathData[fromZone].pathMinLevel = 0;

	if (const ZoneGuideZone* z = zoneMgr.GetZone(currentZoneId))
		pathData[fromZone].pathMinLevel = z->minLevel;

	// TODO: Handle bind zones (gate)
	// TODO: Handle teleport spell zones (translocate, etc)

	auto addTransfer = [&](EQZoneIndex destZoneId, int transferTypeIndex, int minLevel,
		const ParsedFindableLocation* location, const ZoneGuideConnection* connection)
	{
		auto& data = pathData[destZoneId];
		auto& prevData = pathData[currentZoneId];

		data.location = location;
		data.connection = connection;

		if (data.depth == -1)
		{
			queue.push_back(destZoneId);

			data.prevZoneTransferTypeIndex = transferTypeIndex;
			data.prevZone = currentZoneId;
			data.pathMinLevel = std::max(prevData.pathMinLevel, minLevel);

			data.depth = prevData.depth + 1;
		}
		else if (data.prevZone && (data.depth == prevData.depth + 1)
			&& pathData[data.prevZone].pathMinLevel > prevData.pathMinLevel)
		{
			// lower level preference?
			data.prevZoneTransferTypeIndex = transferTypeIndex;
			data.prevZone = currentZoneId;
			data.pathMinLevel = std::max(prevData.pathMinLevel, minLevel);
		}
	};

	int otherIndex = FindTransferIndexByName("Other");
	int zoneLineIndex = FindTransferIndexByName("Zone Line");
	int translocatorIndex = FindTransferIndexByName("Translocator");

	// Explore the zone graph and cost everything out.
	while (!queue.empty())
	{
		currentZoneId = queue.front();
		queue.pop_front();

		// Did we find a connection to the destination?
		if (pathData[toZone].depth > -1 && pathData[toZone].depth < pathData[currentZoneId].depth)
		{
			break;
		}

		const EZZoneData& ezZoneData = g_zoneConnections->GetZoneData(currentZoneId);

		if (ZoneGuideZone* currentZone = zoneMgr.GetZone(currentZoneId))
		{
			// Search the zone guide with modified parameters.
			for (const ZoneGuideConnection& connection : currentZone->zoneConnections)
			{
				// Skip connection if it is disabled by the user.
				if (connection.disabled)
					continue;

				// Make sure that progression server expansion is available.
				if (connection.requiredExpansions != 0 && pEverQuestInfo->bProgressionServer)
				{
					if ((pEverQuestInfo->ProgressionOpenExpansions & connection.requiredExpansions) != connection.requiredExpansions)
						continue;
				}

				// Make sure we didn't remove this connection
				if (std::find(ezZoneData.removedConnections.begin(), ezZoneData.removedConnections.end(), connection.destZoneId) != ezZoneData.removedConnections.end())
					continue;

				// Make sure that the transfer types are supported
				if (g_configuration->IsDisabledTransferType(connection.transferTypeIndex))
					continue;

				addTransfer(connection.destZoneId, connection.transferTypeIndex, currentZone->minLevel, nullptr, &connection);
				continue;
			}
		}

		// Search our own connections
		for (const ParsedFindableLocation& location : ezZoneData.findableLocations)
		{
			if (!location.IsZoneConnection())
				continue;

			if (!location.CheckRequirements())
				continue;

			int transferTypeIndex = otherIndex;

			if (location.type == LocationType::Location)
				transferTypeIndex = zoneLineIndex;
			else if (location.type == LocationType::Translocator)
				transferTypeIndex = translocatorIndex;

			if (location.zoneId != 0)
				addTransfer(location.zoneId, transferTypeIndex, 0, &location, nullptr);
			for (const auto& dest : location.translocatorDestinations)
			{
				if (dest.zoneId != 0)
					addTransfer(dest.zoneId, translocatorIndex, 0, &location, nullptr);
			}
		}
	}

	// Work backwards from the destination and build the route.
	EQZoneIndex zoneId = toZone;
	int transferTypeIndex = -1;
	const ParsedFindableLocation* location = nullptr;
	const ZoneGuideConnection* connection = nullptr;

	std::vector<ZonePathNode> reversedPath;

	while (zoneId != 0)
	{
		reversedPath.emplace_back(zoneId, transferTypeIndex, location, connection);

		transferTypeIndex = pathData[zoneId].prevZoneTransferTypeIndex;
		zoneId = pathData[zoneId].prevZone;
		location = pathData[zoneId].location;
		connection = pathData[zoneId].connection;
	}

	if (reversedPath.size() <= 1)
	{
		outputMessage = "Could not find path to target zone.";
	}

	std::vector<ZonePathNode> newPath;
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

void ZonePath_FollowActive()
{
	s_activeZonePathRequest.clear();

	for (const ZonePathData& pathData : ZoneGuideManagerClient::Instance().activePath)
		s_activeZonePathRequest.zonePath.emplace_back(pathData);

	s_travelToActive = true;
	s_findNextPath = true;
}

void StopTravelTo(bool success)
{
	ZonePath_SetActive({}, false);
	s_travelToActive = false;
}

static bool ActivateNextPath()
{
	// Wait to update until we have all of our locations updated.
	CFindLocationWndOverride* pFindLocWnd = pFindLocationWnd.get_as<CFindLocationWndOverride>();
	if (pFindLocWnd && pFindLocWnd->IsCustomLocationsAdded())
	{
		s_findNextPath = false;
		if (!s_activeZonePathRequest.zonePath.empty())
		{
			EQZoneIndex nextZoneId = 0;
			int transferTypeIndex = -1;

			// Find the next zone to travel to!
			for (size_t i = 0; i < s_activeZonePathRequest.zonePath.size() - 1; ++i)
			{
				if (s_activeZonePathRequest.zonePath[i].zoneId == s_currentZone)
				{
					nextZoneId = s_activeZonePathRequest.zonePath[i + 1].zoneId;
					transferTypeIndex = s_activeZonePathRequest.zonePath[i].transferTypeIndex;
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

void ZonePath_SetActive(const ZonePathRequest& zonePathData, bool travel)
{
	ZonePathArray pathArray((int)zonePathData.zonePath.size());

	for (const ZonePathNode& pathData : zonePathData.zonePath)
	{
		pathArray.Add(ZonePathData(pathData.zoneId, pathData.transferTypeIndex));
	}

	bool hasItems = !pathArray.empty();
	ZoneGuideManagerClient::Instance().activePath = std::move(pathArray);

	s_travelToActive = travel;
	s_findNextPath = travel;
	s_activeZonePathRequest = zonePathData;

	if (ActivateNextPath())
	{
		if (pZonePathWnd)
		{
			pZonePathWnd->zonePathDirty = true;
			pZonePathWnd->Show(hasItems);
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

	s_currentZone = pWorldData->GetZoneBaseId(pLocalPlayer->GetZoneID());

	// If zone path is active then update it.
	if (!s_activeZonePathRequest.zonePath.empty())
	{
		EQZoneIndex destZone = s_activeZonePathRequest.zonePath.back().zoneId;
		if (destZone == s_currentZone)
		{
			std::string targetQuery = std::move(s_activeZonePathRequest.targetQuery);

			SPDLOG_INFO("Arrived at our destination zone: \ay{}\ax!", GetFullZone(destZone));
			StopTravelTo(true);

			if (!targetQuery.empty())
			{
				FindWindow_FindLocation(targetQuery, false);
			}
		}
		else
		{
			// Update the path if we took a wrong turn

			bool found = false;
			for (const ZonePathNode& pathData : s_activeZonePathRequest.zonePath)
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
				auto newPath = ZonePath_GeneratePath(s_currentZone, destZone, message);
				if (newPath.empty())
				{
					SPDLOG_WARN("Path generation failed: {}", message);
				}
				auto newRequest = s_activeZonePathRequest;
				newRequest.zonePath = newPath;

				ZonePath_SetActive(newRequest, s_travelToActive);
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
	if (s_currentZone != pLocalPC->zoneId)
	{
		UpdateForZoneChange();
	}

	if (s_findNextPath)
	{
		ActivateNextPath();
	}
}

void ZonePath_NavCanceled(bool message)
{
	if (s_travelToActive)
	{
		StopTravelTo(false);
		if (message)
		{
			SPDLOG_INFO("Stopping /travelto because nav was stopped.");
		}
	}
}

void ZonePath_Stop()
{
	ZoneGuideManagerClient& zoneGuide = ZoneGuideManagerClient::Instance();

	bool isActive = s_travelToActive || !s_activeZonePathRequest.zonePath.empty() || !zoneGuide.activePath.IsEmpty();

	if (isActive)
	{
		StopTravelTo(false);
		SPDLOG_INFO("/travelto stopped");
	}
	else
	{
		SPDLOG_WARN("No /travelto is active.");
	}

	Navigation_Stop();
}

namespace YAML
{
	template <>
	struct convert<CXStr>
	{
		static Node encode(const CXStr& str)
		{
			YAML::Node node(NodeType::Scalar);
			node = std::string{ str };

			return node;
		}

		static bool decode(const Node& node, CXStr& str)
		{
			if (!node.IsScalar())
				return false;

			try {
				str = node.as<std::string>();
			}
			catch (const YAML::BadConversion&) {
				return false;
			}
			return true;
		}
	};

	template <>
	struct convert<ZoneGuideConnection>
	{
		static Node encode(const ZoneGuideConnection& zone)
		{
			YAML::Node node;
			node["DestZoneId"] = (int)zone.destZoneId;
			node["DestZone"] = GetShortZone((int)zone.destZoneId);
			node["TransferType"] = ZoneGuideManagerClient::Instance().GetZoneTransferTypeNameByIndex(zone.transferTypeIndex);
			if (zone.requiredExpansions != 0)
				node["RequiredExpansion"] = GetHighestExpansionOwnedName((EQExpansionOwned)zone.requiredExpansions);

			return node;
		}

		static bool decode(const Node& node, ZoneGuideConnection& zone)
		{
			return false;
		}
	};

	template <>
	struct convert<ZoneGuideContinent>
	{
		static Node encode(const ZoneGuideContinent& zone)
		{
			YAML::Node node;
			node["Id"] = zone.id;
			node["Name"] = zone.name;

			return node;
		}

		static bool decode(const Node& node, ZoneGuideContinent& zone)
		{
			return false;
		}
	};

	template <>
	struct convert<ZoneGuideZoneType>
	{
		static Node encode(const ZoneGuideZoneType& zone)
		{
			YAML::Node node;
			node["Id"] = zone.id;
			node["DisplaySequence"] = zone.displaySequence;
			node["Name"] = zone.name;

			return node;
		}

		static bool decode(const Node& node, ZoneGuideZoneType& zone)
		{
			return false;
		}
	};

	template <>
	struct convert<ZoneGuideTransferType>
	{
		static Node encode(const ZoneGuideTransferType& zone)
		{
			YAML::Node node;
			node["Id"] = zone.id;
			node["Description"] = zone.description;

			return node;
		}

		static bool decode(const Node& node, ZoneGuideTransferType& zone)
		{
			return false;
		}
	};

	template <typename T>
	struct convert<ArrayClass<T>> {
		static Node encode(const ArrayClass<T>& arr) {
			Node node(NodeType::Sequence);
			for (const T& value : arr)
				node.push_back(value);
			return node;
		}

		static bool decode(const Node& node, ArrayClass<T>& arr) {
			return false;
		}
	};

	template <>
	struct convert<ZoneGuideZone>
	{
		static Node encode(const ZoneGuideZone& zone)
		{
			YAML::Node node;
			node["ZoneId"] = zone.zoneId;

			EQZoneInfo* pZoneInfo = pWorldData->GetZone(zone.zoneId);
			if (pZoneInfo)
			{
				node["Name"] = pZoneInfo->LongName;
				node["ShortName"] = pZoneInfo->ShortName;
			}
			else
			{
				node["Name"] = "Unknown";
				node["ShortName"] = "Unknown";
			}
			node["Continent"] = ZoneGuideManagerClient::Instance().GetContinentNameByIndex(zone.continentIndex);
			node["MinLevel"] = zone.minLevel;
			node["MaxLevel"] = zone.maxLevel;

			std::vector<std::string> types;
			for (int i = 0; i < zone.types.GetNumBits(); ++i)
			{
				if (zone.types.IsBitSet(i))
				{
					types.push_back(std::string(ZoneGuideManagerClient::Instance().GetZoneTypeNameByIndex(i)));
				}
			}
			node["Types"] = types;
			node["Connections"] = zone.zoneConnections;

			return node;
		}

		static bool decode(const Node& node, ZoneGuideZone& zone)
		{
			return false;
		}
	};
}

void ZonePath_DumpConnections()
{
	// Dump all the connection data from the ZoneGuideManager
	ZoneGuideManagerClient& mgr = ZoneGuideManagerClient::Instance();

	try
	{
		SPDLOG_INFO("Dumping zone connections from ZoneGuideManager...");

		std::string outputFile = (std::filesystem::path(gPathResources) / "ZoneGuide.yaml").string();

		YAML::Node node;
		node["Continents"] = mgr.continents;
		node["ZoneTypes"] = mgr.zoneTypes;
		node["TransferTypes"] = mgr.transferTypes;

		std::vector<ZoneGuideZone> zones;
		for (const ZoneGuideZone& zone : mgr.zones)
		{
			if (zone.zoneId != 0)
				zones.push_back(zone);
		}
		node["Zones"] = zones;

		YAML::Emitter out;
		out.SetIndent(4);
		out.SetFloatPrecision(3);
		out.SetDoublePrecision(3);
		out << node;

		std::fstream file(outputFile, std::ios::out);
		file << out.c_str();
	}
	catch (const std::exception& exc)
	{
		WriteChatf("\arError: %s", exc.what());
	}
}
