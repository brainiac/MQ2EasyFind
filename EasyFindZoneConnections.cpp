
#include "EasyFind.h"
#include "EasyFindConfiguration.h"
#include "EasyFindZoneConnections.h"

#include <fstream>

namespace fs = std::filesystem;

const char* s_luaTranslocatorCode = R"(-- Hail translocator and say keyword
local spawn = mq.TLO.NearestSpawn('npc ' .. location.spawnName)
if spawn() ~= nil then
	spawn.DoTarget()
	mq.cmd("/makemevisible")
	mq.delay(500)
	mq.cmd("/hail")
	mq.delay(1200)
	mq.cmdf("/say %s", location.translocatorKeyword)
end
)";

ZoneConnections* g_zoneConnections = nullptr;

//============================================================================

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

			if (node["expansion"].IsDefined())
			{
				std::string expansionName = node["expansion"].as<std::string>(std::string());
				int expansionNum = GetExpansionNumber(expansionName);
				if (expansionNum > 0)
					data.requiredExpansions = (EQExpansionOwned)EQ_EXPANSION(expansionNum);
			}

			YAML::Node achievementNode = node["requiredAchievement"];
			if (achievementNode.IsDefined())
			{
				data.requiredAchievement = achievementNode.as<int>(0);
				if (data.requiredAchievement == 0)
					data.requiredAchievementName = achievementNode.as<std::string>(std::string());
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
				data.remove = node["remove"].as<bool>(false);
				data.luaScript = node["script"].as<std::string>(std::string());
				if (data.luaScript.empty())
					data.luaScriptFile = node["scriptFile"].as<std::string>(std::string());
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

bool ParsedFindableLocation::IsZoneConnection() const
{
	if (remove)
		return false;
	if (zoneId != 0)
		return true;

	for (const auto& dest : translocatorDestinations)
	{
		if (dest.zoneId != 0)
			return true;
	}
	return false;
}

bool ParsedFindableLocation::CheckRequirements() const
{
	// check expansion. We only track the expansion num so convert to flags and check that its set.
	if (requiredExpansions != 0 && pEverQuestInfo->bProgressionServer)
	{
		if ((pEverQuestInfo->ProgressionOpenExpansions & requiredExpansions) == 0)
			return false;
	}

	if (requiredAchievement != 0)
	{
		if (!IsAchievementComplete(GetAchievementById(requiredAchievement)))
			return false;
	}
	else if (!requiredAchievementName.empty())
	{
		if (!IsAchievementComplete(GetAchievementByName(requiredAchievementName)))
			return false;
	}

	return true;
}

ZoneConnections::ZoneConnections(const std::string& easyfindDirectory)
	: m_easyfindDir(easyfindDirectory)
{
	std::error_code ec;
	if (!fs::is_directory(m_easyfindDir, ec))
	{
		fs::create_directories(m_easyfindDir, ec);
	}

	Load();
}

ZoneConnections::~ZoneConnections()
{
}

void ZoneConnections::Load()
{
	std::string configFile = (fs::path(m_easyfindDir) / "ZoneConnections.yaml").string();
	try
	{
		m_zoneConnectionsConfig = YAML::LoadFile(configFile);
	}
	catch (const YAML::ParserException& ex)
	{
		// failed to parse, notify and return
		SPDLOG_ERROR("Failed to parse YAML in {}: {}", configFile, ex.what());
		return;
	}
	catch (const YAML::BadFile&)
	{
		// if we can't read the file, then try to write it with an empty config
		return;
	}
}

void ZoneConnections::ReloadFindableLocations()
{
	SPDLOG_INFO("Reloading zone connections");

	Load();
	LoadFindableLocations();
}

void ZoneConnections::LoadFindableLocations()
{
	if (!pWorldData)
		return;
	m_zoneDataLoaded = true;

	FindWindow_Reset();

	FindableLocationsMap locationMap;

	try
	{
		ParsedFindableLocationsMap newLocations;

		// Load objects from the FindLocations block
		YAML::Node addFindLocations = m_zoneConnectionsConfig["FindLocations"];
		if (addFindLocations.IsMap())
		{
			newLocations = addFindLocations.as<ParsedFindableLocationsMap>();
		}

		// move the findable locations into place.
		for (auto& [name, locations] : newLocations)
		{
			EZZoneData& data = m_findableLocations[name];

			data.zoneId = GetZoneID(name.c_str());
			data.findableLocations = std::move(locations);

			// move any "remove" entries to the removed connections list
			data.removedConnections.clear();
			data.findableLocations.erase(
				std::remove_if(data.findableLocations.begin(), data.findableLocations.end(),
					[&](const ParsedFindableLocation& pfl)
				{
					if (pfl.remove)
					{
						if (pfl.zoneId != 0)
							data.removedConnections.push_back(pfl.zoneId);

						return true;
					}

					return false;
				}), data.findableLocations.end());

			// Load any removed zones into the zone guide.
			if (pZoneGuideWnd && !data.removedConnections.empty())
			{
				ZoneGuideZone* zoneGuideZone = ZoneGuideManagerClient::Instance().GetZone(data.zoneId);
				if (zoneGuideZone)
				{
					for (int destZoneId : data.removedConnections)
					{
						for (ZoneGuideConnection& connection : zoneGuideZone->zoneConnections)
						{
							if (connection.destZoneId == destZoneId)
								connection.disabled = true;
						}
					}
				}
			}
		}
	}
	catch (const YAML::Exception& ex)
	{
		// failed to parse, notify and return
		SPDLOG_ERROR("Failed to load zone connections: {}", ex.what());
	}

	FindWindow_LoadZoneConnections();
}

void ZoneConnections::CreateFindableLocations(FindableLocations& findableLocations, std::string_view shortName)
{
	auto iter = m_findableLocations.find(shortName);
	if (iter == m_findableLocations.end())
		return;

	std::vector<ParsedFindableLocation>& parsedLocations = iter->second.findableLocations;

	findableLocations.reserve(parsedLocations.size());

	for (const ParsedFindableLocation& parsedLocation : parsedLocations)
	{
		switch (parsedLocation.type)
		{
		case LocationType::Location:
		case LocationType::Switch: {
			FindableLocation loc;
			loc.parsedData = &parsedLocation;
			loc.easyfindType = parsedLocation.type;
			loc.type = (parsedLocation.type == LocationType::Location) ? FindLocation_Location : FindLocation_Switch;
			loc.location = parsedLocation.location;
			loc.name = parsedLocation.name;
			loc.zoneId = parsedLocation.zoneId;
			loc.zoneIdentifier = parsedLocation.zoneIdentifier;
			loc.switchId = parsedLocation.switchId;
			loc.switchName = parsedLocation.switchName;
			loc.luaScript = parsedLocation.luaScript;
			loc.replace = parsedLocation.replace;
			findableLocations.push_back(loc);
			break;
		}

		case LocationType::Translocator: {
			FindableLocation loc;
			loc.parsedData = &parsedLocation;
			loc.easyfindType = parsedLocation.type;
			loc.type = FindLocation_Location;
			loc.spawnName = parsedLocation.name;

			for (const ParsedTranslocatorDestination& dest : parsedLocation.translocatorDestinations)
			{
				FindableLocation transLoc = loc;
				transLoc.zoneId = dest.zoneId;
				transLoc.zoneIdentifier = dest.zoneIdentifier;
				transLoc.translocatorKeyword = dest.keyword;

				transLoc.luaScript = s_luaTranslocatorCode;
				findableLocations.push_back(transLoc);
			}
			break;
		}

		case LocationType::Unknown:
			break;
		}
	}
}

const EZZoneData& ZoneConnections::GetZoneData(EQZoneIndex zoneId) const
{
	const char* zoneName = GetShortZone(zoneId);

	auto iter = m_findableLocations.find(zoneName);
	if (iter == m_findableLocations.end())
	{
		static EZZoneData empty;
		return empty;
	}

	return iter->second;
}

bool ZoneConnections::MigrateIniData()
{
	if (!pWorldData)
	{
		SPDLOG_WARN("Zone data is not available, try again later...");
		return false; // TODO: Retry later
	}

	SPDLOG_INFO("Migrating configuration from INI...");
	std::string iniFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.ini").string();

	int count = 0;
	std::vector<std::string> sectionNames = GetPrivateProfileSections(iniFile);

	YAML::Node migrated;

	if (!sectionNames.empty())
	{
		YAML::Node findLocations = migrated["FindLocations"];
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
								SPDLOG_ERROR("Failed to migrate section: {} key: {}, invalid value: {}.", sectionName, keyName, value);
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
							SPDLOG_ERROR("Failed to migrate section: {} key: {}, invalid value: {}.", sectionName, keyName, value);
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

				SPDLOG_ERROR("Failed to migrate section: {} key: {}, zone name not found.", sectionName, keyName);
			}
		}
	}

	std::string filename = (fs::path(m_easyfindDir) / "Migrated.yaml").string();
	try
	{
		std::fstream file(filename, std::ios::out);

		if (!migrated.IsNull())
		{
			YAML::Emitter y_out;
			y_out.SetIndent(4);
			y_out.SetFloatPrecision(2);
			y_out.SetDoublePrecision(2);
			y_out << migrated;

			file << y_out.c_str();

			if (count > 0)
			{
				SPDLOG_INFO("Migrated {} zone connections from MQ2EasyFind.ini to {}", count, filename);
				SPDLOG_INFO("Review this file and copy anything you want into ZoneConnections.yaml");
			}
		}

	}
	catch (const std::exception&)
	{
		SPDLOG_ERROR("Failed to write settings file: {}", filename);
	}

	return true;
}

void ZoneConnections::Pulse()
{
	if (!m_zoneDataLoaded)
	{
		if (pWorldData)
		{
			m_zoneDataLoaded = true;
			LoadFindableLocations();
		}
	}
	if (GetGameState() != GAMESTATE_INGAME)
	{
		m_transferTypesLoaded = false;
		return;
	}

	if (!m_transferTypesLoaded
		&& ZoneGuideManagerClient::Instance().zoneGuideDataSet)
	{
		g_configuration->RefreshTransferTypes();

		m_transferTypesLoaded = true;
	}
}
