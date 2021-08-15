
#include "EasyFind.h"

#include <fstream>
#include <optional>

static std::string s_configFile;

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

static bool MigrateConfigurationFile(bool force)
{
	if (!pWorldData)
	{
		return false; // TODO: Retry later
	}

	bool migratedAlready = g_configNode["ConfigurationMigrated"].as<bool>(false);
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
		YAML::Node findLocations = g_configNode["FindLocations"];
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

	g_configNode["ConfigurationMigrated"] = true;
	if (count > 0)
	{
		WriteChatf("\agMigrated %d zone connections from MQ2EasyFind.ini", count);
	}
	return true;
}

static void SaveConfigurationFile()
{
	std::fstream file(s_configFile, std::ios::out);

	if (!g_configNode.IsNull())
	{
		YAML::Emitter y_out;
		y_out.SetIndent(4);
		y_out.SetFloatPrecision(3);
		y_out.SetDoublePrecision(3);
		y_out << g_configNode;

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

void GenerateFindableLocations(FindableLocations& findableLocations, std::vector<ParsedFindableLocation>&& parsedLocations)
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

void LoadZoneSettings()
{
	if (pFindLocationWnd)
	{
		pFindLocationWnd.get_as<CFindLocationWndOverride>()->RemoveCustomLocations();
	}

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
		YAML::Node addFindLocations = g_configNode["FindLocations"];
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

	g_findableLocations = std::move(newLocations);
}


static void LoadSettings()
{
	try
	{
		g_configNode = YAML::LoadFile(s_configFile);
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

void ReloadSettings()
{
	WriteChatf(PLUGIN_MSG "Reloading settings");
	LoadSettings();

	LoadZoneSettings();
}

//----------------------------------------------------------------------------

void MigrateConfigurationCommand()
{
	if (MigrateConfigurationFile(true))
	{
		SaveConfigurationFile();
		LoadSettings();
	}
}

void Config_Initialize()
{
	s_configFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.yaml").string();
	LoadSettings();
}

void Config_Shutdown()
{
}
