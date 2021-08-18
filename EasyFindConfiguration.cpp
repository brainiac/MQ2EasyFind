
#include "EasyFind.h"
#include "EasyFindConfiguration.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/msvc_sink.h>

#include <fstream>
#include <optional>

namespace fs = std::filesystem;

EasyFindConfiguration* g_configuration = nullptr;
FindableLocations g_findableLocations;

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

std::array<MQColor, (size_t)ConfiguredColor::MaxColors> s_defaultColors = {
	MQColor(255, 192, 64),         // AddedLocation
	MQColor(64, 192, 255),         // ModifiedLocation
};

//============================================================================

class WriteChatSink : public spdlog::sinks::base_sink<spdlog::details::null_mutex>
{
public:
	void set_enabled(bool enabled) { enabled_ = enabled; }
	bool enabled() const { return enabled_; }

protected:
	void sink_it_(const spdlog::details::log_msg& msg) override
	{
		if (!enabled_)
			return;

		using namespace spdlog;

		fmt::memory_buffer formatted;
		switch (msg.level)
		{
		case level::critical:
		case level::err:
			fmt::format_to(formatted, PLUGIN_MSG_LOG("\ar") "{}", msg.payload);
			break;

		case level::trace:
		case level::debug:
			fmt::format_to(formatted, PLUGIN_MSG_LOG("\a#7f7f7f") "{}", msg.payload);
			break;

		case level::warn:
			fmt::format_to(formatted, PLUGIN_MSG_LOG("\ay") "{}", msg.payload);
			break;

		case level::info:
		default:
			fmt::format_to(formatted, PLUGIN_MSG_LOG("\ag") "{}", msg.payload);
			break;
		}

		WriteChatf("%s", fmt::to_string(formatted).c_str());
	}

	void flush_() override {}

	bool enabled_ = true;
};

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
	struct convert<spdlog::level::level_enum> {
		static Node encode(spdlog::level::level_enum data) {
			Node node;
			switch (data) {
			case spdlog::level::trace: node = "trace"; break;
			case spdlog::level::debug: node = "debug"; break;
			case spdlog::level::info: node = "info"; break;
			case spdlog::level::warn: node = "warn"; break;
			case spdlog::level::err: node = "error"; break;
			case spdlog::level::critical: node = "critical"; break;
			case spdlog::level::off: node = "off"; break;
			default: node = "info"; break;
			}
			return node;
		}
		static bool decode(const Node& node, spdlog::level::level_enum& data) {
			if (!node.IsScalar()) {
				return false;
			}
			std::string nodeValue = node.as<std::string>(std::string());
			if (nodeValue == "trace") { data = spdlog::level::trace; return true; }
			if (nodeValue == "debug") { data = spdlog::level::debug; return true; }
			if (nodeValue == "info") { data = spdlog::level::info; return true; }
			if (nodeValue == "warn") { data = spdlog::level::warn; return true; }
			if (nodeValue == "error") { data = spdlog::level::err; return true; }
			if (nodeValue == "critical") { data = spdlog::level::critical; return true; }
			if (nodeValue == "off") { data = spdlog::level::off; return true; }

			// just set a default value in unexpected case...
			data = spdlog::level::info;
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

class ZoneConnections
{
public:
	ZoneConnections(const std::string& configDirectory)
		: m_configDirectory(configDirectory)
	{
		std::error_code ec;
		if (!fs::is_directory(m_configDirectory, ec))
		{
			fs::create_directories(m_configDirectory, ec);
		}

		Load();
	}

	~ZoneConnections()
	{
	}

	const std::string& GetConfigDir() const { return m_configDirectory; }

	void Load()
	{
		std::string configFile = m_configDirectory + "/ZoneConnections.yaml";
		try
		{
			// FIXME
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

	void LoadZoneConnections()
	{
		FindWindow_Reset();

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
			YAML::Node addFindLocations = m_zoneConnectionsConfig["FindLocations"];
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
			SPDLOG_ERROR("Failed to load zone settings for {}: {}", shortName, ex.what());
		}

		g_findableLocations = std::move(newLocations);
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

	bool MigrateIniData()
	{
		if (!pWorldData)
		{
			return false; // TODO: Retry later
		}

		SPDLOG_INFO("Migrating configuration from INI...");
		std::string iniFile = (std::filesystem::path(gPathConfig) / "MQ2EasyFind.ini").string();

		int count = 0;
		std::vector<std::string> sectionNames = GetPrivateProfileSections(iniFile);

		if (!sectionNames.empty())
		{
			YAML::Node findLocations = m_zoneConnectionsConfig["FindLocations"];
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

		if (count > 0)
		{
			SPDLOG_INFO("Migrated {} zone connections from MQ2EasyFind.ini", count);
		}
		return true;
	}

private:
	std::string m_configDirectory;
	YAML::Node m_zoneConnectionsConfig;
};

//============================================================================
//============================================================================

EasyFindConfiguration::EasyFindConfiguration()
{
	m_configuredColors = s_defaultColors;

	// The config file holds our user preferences
	m_configFile = (std::filesystem::path(gPathConfig) / "EasyFind.yaml").string();

	// Zone connections and other navigation data is stored here
	std::string easyfindDir = (std::filesystem::path(gPathResources) / "EasyFind").string();
	m_zoneConnections = std::make_unique<ZoneConnections>(easyfindDir);

	// set up the default logger
	auto logger = std::make_shared<spdlog::logger>("EasyFind");
	logger->set_level(spdlog::level::debug);

	//spdlog::details::registry::instance().initialize_logger(logger);
	m_chatSink = std::make_shared<WriteChatSink>();
	m_chatSink->set_level(spdlog::level::info);
	logger->sinks().push_back(m_chatSink);
#if defined(_DEBUG)
	logger->sinks().push_back(std::make_shared<spdlog::sinks::msvc_sink_mt>());
#endif

	spdlog::set_default_logger(logger);
	spdlog::set_pattern("%L %Y-%m-%d %T.%f [%n] %v (%@)");
	spdlog::flush_on(spdlog::level::debug);

	LoadSettings();
	m_zoneConnections->Load();
}

EasyFindConfiguration::~EasyFindConfiguration()
{
	spdlog::shutdown();
}

void EasyFindConfiguration::SetLogLevel(spdlog::level::level_enum level)
{
	m_chatSink->set_level(level);
	m_configNode["GlobalLogLevel"] = level;
}

spdlog::level::level_enum EasyFindConfiguration::GetLogLevel() const
{
	return m_chatSink->level();
}

void EasyFindConfiguration::ReloadSettings()
{
	SPDLOG_INFO("Reloading settings");
	LoadSettings();
}

void EasyFindConfiguration::ReloadZoneConnections()
{
	SPDLOG_INFO("Reloading zone connections");
	LoadZoneConnections();
}

void EasyFindConfiguration::LoadSettings()
{
	try
	{
		m_configNode = YAML::LoadFile(m_configFile);

		spdlog::level::level_enum globalLogLevel = m_configNode["GlobalLogLevel"].as<spdlog::level::level_enum>(spdlog::level::info);
		SetLogLevel(globalLogLevel);
	}
	catch (const YAML::ParserException& ex)
	{
		// failed to parse, notify and return
		SPDLOG_ERROR("Failed to parse YAML in {}: {}", m_configFile, ex.what());
		return;
	}
	catch (const YAML::BadFile&)
	{
		// if we can't read the file, then try to write it with an empty config
		SaveSettings();
		return;
	}
}

void EasyFindConfiguration::SaveSettings()
{
	std::fstream file(m_configFile, std::ios::out);

	if (!m_configNode.IsNull())
	{
		YAML::Emitter y_out;
		y_out.SetIndent(4);
		y_out.SetFloatPrecision(3);
		y_out.SetDoublePrecision(3);
		y_out << m_configNode;

		file << y_out.c_str();
	}
}

void EasyFindConfiguration::LoadZoneConnections()
{
	m_zoneConnections->LoadZoneConnections();
}

MQColor EasyFindConfiguration::GetDefaultColor(ConfiguredColor color) const
{
	return s_defaultColors[(int)color];
}

const std::string& EasyFindConfiguration::GetZoneConnectionsDir() const
{
	return m_zoneConnections->GetConfigDir();
}

//----------------------------------------------------------------------------

void EasyFindConfiguration::MigrationCommand()
{
	if (m_zoneConnections->MigrateIniData())
	{
		SaveSettings();
		LoadSettings();
	}
}

//============================================================================

void Config_Initialize()
{
	g_configuration = new EasyFindConfiguration();
}

void Config_Shutdown()
{
	delete g_configuration;
}
