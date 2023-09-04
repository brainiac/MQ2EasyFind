
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

static constexpr std::array<MQColor, (size_t)ConfiguredColor::MaxColors> s_defaultColors = {
	MQColor(96, 255, 72),          // AddedLocation
	MQColor(64, 192, 255),         // ModifiedLocation
};

const char* GetConfiguredColorName(ConfiguredColor color)
{
	switch (color)
	{
	case ConfiguredColor::AddedLocation: return "AddedLocation";
	case ConfiguredColor::ModifiedLocation: return "ModifiedLocation";
	default: return "Unknown";
	}
}

const char* GetConfiguredColorDescription(ConfiguredColor color)
{
	switch (color)
	{
	case ConfiguredColor::AddedLocation: return "Added Locations";
	case ConfiguredColor::ModifiedLocation: return "Modified Locations";
	default: return "Unknown";
	}
}

static const std::vector<std::pair<ConfiguredGroupPlugin, const char*>> s_groupPluginNames = {
	{ ConfiguredGroupPlugin::Auto, "auto" },
	{ ConfiguredGroupPlugin::Dannet, "dannet" },
	{ ConfiguredGroupPlugin::EQBC, "eqbc" },
	{ ConfiguredGroupPlugin::None, "none" },
};

const char* GetGroupPluginPreferenceString(ConfiguredGroupPlugin plugin)
{
	for (const auto& p : s_groupPluginNames)
	{
		if (p.first == plugin)
			return p.second;
	}

	return "unknown";
}

//============================================================================

namespace YAML
{
	template <>
	struct convert<spdlog::level::level_enum> {
		static Node encode(spdlog::level::level_enum data) {
			Node node;

			auto sv = spdlog::level::to_string_view(data);
			node = std::string(sv.data(), sv.size());
			return node;
		}
		static bool decode(const Node& node, spdlog::level::level_enum& data) {
			if (!node.IsScalar()) {
				return false;
			}
			std::string nodeValue = node.as<std::string>(std::string());
			data = spdlog::level::from_str(nodeValue);
			return true;
		}
	};

	template <>
	struct convert<GuildHallClickyItem> {
		static Node encode(const GuildHallClickyItem& data) {
			Node node;
			return node;
		}

		static bool decode(const Node& node, GuildHallClickyItem& data) {
			if (!node.IsMap()) {
				return false;
			}

			data.zoneName = node["zoneName"].as<std::string>(std::string());
			data.zoneShortName = node["zoneShortName"].as<std::string>(std::string());
			data.itemName = node["itemName"].as<std::string>(std::string());
			data.menuText = node["menuText"].as<std::string>(std::string());
			data.guildClickyCommand = node["guildClickyCommand"].as<std::string>(std::string());

			return true;
		}
	};

	template <>
	struct convert<ConfiguredGroupPlugin> {
		static Node encode(ConfiguredGroupPlugin data) {
			Node node;

			node = GetGroupPluginPreferenceString(data);
			return node;
		}
		static bool decode(const Node& node, ConfiguredGroupPlugin& data) {
			if (!node.IsScalar())
				return false;
			std::string nodeValue = node.as<std::string>(std::string());
			for (const auto& p : s_groupPluginNames)
			{
				if (std::string_view{ p.second } == nodeValue)
				{
					data = p.first;
					return true;
				}
			}
			return false;
		}
	};

	template <>
	struct convert<mq::MQColor> {
		static Node encode(const mq::MQColor& data) {
			Node node;

			// could probably have a to_string for MQColor
			if (data.Alpha == 255) {
				// full alpha we just use rgb hex value
				node = fmt::format("#{:02x}{:02x}{:02x}", data.Red, data.Green, data.Blue);
			}
			else {
				// transparent we use rgba components
				std::array<uint8_t, 4> components = {
					data.Red, data.Green, data.Blue, data.Alpha
				};

				node = components;
			}

			return node;
		}
		static bool decode(const Node& node, mq::MQColor& data) {
			try {
				if (node.IsScalar()) {
					std::string hexValue = node.as<std::string>(std::string());

					data = MQColor(hexValue.c_str());
					return true;
				}
				else if (node.IsSequence()) {
					std::vector<uint8_t> components = node.as<std::vector<uint8_t>>(std::vector<uint8_t>());
					if (components.size() != 4) {
						return false;
					}

					data = MQColor(components[0], components[1], components[2], components[3]);
					return true;
				}
			}
			catch (const mq::detail::InvalidHexChar&) {
				return false;
			}

			return false;
		}
	};
}

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
			fmt::format_to(fmt::appender(formatted), PLUGIN_MSG_LOG("\ar") "{}", msg.payload);
			break;

		case level::trace:
		case level::debug:
			fmt::format_to(fmt::appender(formatted), PLUGIN_MSG_LOG("\a#7f7f7f") "{}", msg.payload);
			break;

		case level::warn:
			fmt::format_to(fmt::appender(formatted), PLUGIN_MSG_LOG("\ay") "{}", msg.payload);
			break;

		case level::info:
		default:
			fmt::format_to(fmt::appender(formatted), PLUGIN_MSG_LOG("\ag") "{}", msg.payload);
			break;
		}

		WriteChatf("%s", fmt::to_string(formatted).c_str());
	}

	void flush_() override {}

	bool enabled_ = true;
};

//============================================================================
//============================================================================

EasyFindConfiguration::EasyFindConfiguration(const std::string& easyfindDirectory)
	: m_easyfindDir(easyfindDirectory)
{
	std::error_code ec;
	if (!fs::is_directory(m_easyfindDir, ec))
	{
		fs::create_directories(m_easyfindDir, ec);
	}

	m_configuredColors = s_defaultColors;

	// The config file holds our user preferences
	m_configFile = (std::filesystem::path(gPathConfig) / "EasyFind.yaml").string();

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

	m_dannetLoaded = IsPluginLoaded("MQ2DanNet");
	m_eqbcLoaded = IsPluginLoaded("MQ2EQBC");

	LoadSettings();
}

EasyFindConfiguration::~EasyFindConfiguration()
{
	spdlog::shutdown();
}

//============================================================================

void EasyFindConfiguration::ReloadSettings()
{
	SPDLOG_INFO("Reloading settings");
	LoadSettings();
}

void EasyFindConfiguration::ResetSettings()
{
	m_disabledTransferTypesPrefs.clear();
	m_enabledGuildHallClickies.clear();
	m_configuredColors = s_defaultColors;
	m_navLogLevel = spdlog::level::err;
	m_chatSink->set_level(spdlog::level::info);
	m_groupPluginSelection = ConfiguredGroupPlugin::Auto;
	m_distanceColumnEnabled = true;
	m_coloredFindWindowEnabled = true;
	m_silentGroupCommands = true;
	m_verboseMessages = false;
	m_ignoreZoneConnectionDataEnabled = false;

	m_useGuildClickies = true;
	m_useGuildClickyLua = true;

	m_configNode = YAML::Node();
	SaveSettings();
}

void EasyFindConfiguration::LoadSettings()
{
	try
	{
		m_configNode = YAML::LoadFile(m_configFile);

		// Log levels
		spdlog::level::level_enum globalLogLevel = m_configNode["GlobalLogLevel"].as<spdlog::level::level_enum>(spdlog::level::info);
		m_chatSink->set_level(globalLogLevel);
		m_navLogLevel = m_configNode["NavLogLevel"].as<spdlog::level::level_enum>(spdlog::level::err);

		// Colors
		for (int i = 0; i < (int)m_configuredColors.size(); ++i)
		{
			ConfiguredColor c = (ConfiguredColor)i;

			m_configuredColors[i] = m_configNode["Colors"][GetConfiguredColorName(c)].as<mq::MQColor>(s_defaultColors[i]);
		}

		// transfer types
		LoadDisabledTransferTypes();
		LoadEnabledGuildHallClickies();
		LoadGuildHallClickiesFile();

		m_groupPluginSelection = m_configNode["GroupPlugin"].as<ConfiguredGroupPlugin>(ConfiguredGroupPlugin::Auto);

		m_coloredFindWindowEnabled = m_configNode["ColoredFindWindow"].as<bool>(true);
		m_distanceColumnEnabled = m_configNode["DistanceColumn"].as<bool>(true);
		m_silentGroupCommands = m_configNode["SilentGroupCommands"].as<bool>(true);
		m_verboseMessages = m_configNode["VerboseMessages"].as<bool>(false);
		m_ignoreZoneConnectionDataEnabled = m_configNode["IgnoreZoneConnectionData"].as<bool>(false);

		m_useGuildClickies = m_configNode["UseGuildClickies"].as<bool>(true);
		m_useGuildClickyLua = m_configNode["UseGuildClickyLua"].as<bool>(true);
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
	try
	{
		std::fstream file(m_configFile, std::ios::out);

		if (!m_configNode.IsNull())
		{
			YAML::Emitter y_out;
			y_out.SetIndent(4);
			y_out.SetFloatPrecision(2);
			y_out.SetDoublePrecision(2);
			y_out << m_configNode;

			file << y_out.c_str();
		}

	}
	catch (const std::exception&)
	{
		SPDLOG_ERROR("Failed to write settings file: {}", m_configFile);
	}
}

//============================================================================

void EasyFindConfiguration::SetColor(ConfiguredColor color, MQColor value)
{
	m_configuredColors[(int)color] = value;
	m_configNode["Colors"][GetConfiguredColorName(color)] = value;

	SaveSettings();
}

MQColor EasyFindConfiguration::GetColor(ConfiguredColor color) const
{
	return m_configuredColors[(int)color];
}

MQColor EasyFindConfiguration::GetDefaultColor(ConfiguredColor color) const
{
	return s_defaultColors[(int)color];
}

void EasyFindConfiguration::SetLogLevel(spdlog::level::level_enum level)
{
	m_chatSink->set_level(level);
	m_configNode["GlobalLogLevel"] = m_chatSink->level();

	SaveSettings();
}

spdlog::level::level_enum EasyFindConfiguration::GetLogLevel() const
{
	return m_chatSink->level();
}

void EasyFindConfiguration::SetNavLogLevel(spdlog::level::level_enum level)
{
	m_navLogLevel = level;
	m_configNode["NavLogLevel"] = m_navLogLevel;

	SaveSettings();
}

spdlog::level::level_enum EasyFindConfiguration::GetNavLogLevel() const
{
	return m_navLogLevel;
}

void EasyFindConfiguration::SetColoredFindWindowEnabled(bool colorize)
{
	m_coloredFindWindowEnabled = colorize;
	m_configNode["ColoredFindWindow"] = colorize;

	SaveSettings();
}

void EasyFindConfiguration::SetDistanceColumnEnabled(bool enable)
{
	m_distanceColumnEnabled = enable;
	m_configNode["DistanceColumn"] = enable;

	SaveSettings();
}

void EasyFindConfiguration::SetSilentGroupCommands(bool silent)
{
	m_silentGroupCommands = silent;
	m_configNode["SilentGroupCommands"] = silent;

	SaveSettings();
}

void EasyFindConfiguration::SetVerboseMessages(bool verbose)
{
	m_verboseMessages = verbose;
	m_configNode["VerboseMessages"] = verbose;

	SaveSettings();
}

void EasyFindConfiguration::SetIgnoreZoneConnectionDataEnabled(bool ignore)
{
	m_ignoreZoneConnectionDataEnabled = ignore;
	m_configNode["IgnoreZoneConnectionData"] = ignore;

	SaveSettings();
}

//----------------------------------------------------------------------------

void EasyFindConfiguration::RefreshTransferTypes()
{
	// These are always disabled because they require configuration to make them work.
	static const std::vector<std::string> unsupportedTransferTypeNames = {
		//"Gate",
		//"Zone Line",
		//"Door",
		//"Keyed Door",
		//"Knowledge Tome",
		"Boat",
		"Wizard's Spire",
		"Translocator",
		"Magus",
		//"Other",
		"Priest of Discord",
		//"Crystal",
		//"Rubble",
		//"Passage",
		//"Portal",
		//"Tree",
		//"Pedestal",
		//"Statue",
		//"Ladder",
		//"Tomb",
		//"Knowledge Stone",
		//"Lever",
		//"Pillar",
		//"Broken Mirror",
		//"Skull",
		//"Platform",
	};

	const ZoneGuideManagerClient& mgr = ZoneGuideManagerClient::Instance();
	int numTransferTypes = (int)mgr.transferTypes.size();

	// these things are hard-coded to disabled above because they always need
	// extra information to make them work.
	m_supportedTransferTypes.resize(numTransferTypes);
	for (int i = 0; i < numTransferTypes; ++i)
	{
		const ZoneGuideTransferType& transferType = mgr.transferTypes[i];
		m_supportedTransferTypes[i] = true;

		for (const std::string& defaults : unsupportedTransferTypeNames)
		{
			if (transferType.description == defaults)
			{
				m_supportedTransferTypes[i] = false;
				break;
			}
		}
	}

	// These are a bit hit-and-miss and so we let the user decide.
	m_disabledTransferTypes.resize(numTransferTypes);
	for (int i = 0; i < numTransferTypes; ++i)
	{
		const ZoneGuideTransferType& transferType = mgr.transferTypes[i];

		for (const std::string& defaults : m_disabledTransferTypesPrefs)
		{
			if (transferType.description == defaults)
			{
				m_disabledTransferTypes[i] = true;
				break;
			}
		}

		m_disabledTransferTypes[i] = false;
	}
}

void EasyFindConfiguration::LoadEnabledGuildHallClickies()
{
	std::vector<std::string> enabledGuildHallClickies;

	YAML::Node node = m_configNode["EnabledGuildHallClickies"];
	if (node.IsDefined())
	{
		enabledGuildHallClickies = node.as<std::vector<std::string>>(std::vector<std::string>());
	}

	m_enabledGuildHallClickies = std::move(enabledGuildHallClickies);
}

GuildHallClickies EasyFindConfiguration::GetAllGuildHallClickyItems() const
{
	return m_allGuildHallClickies;
}

void EasyFindConfiguration::LoadGuildHallClickiesFile()
{
	std::string configFile = (fs::path(m_easyfindDir) / "GuildHallClickies.yaml").string();

	YAML::Node guildHallClickiesConfig;
	try
	{
		guildHallClickiesConfig = YAML::LoadFile(configFile);
	}
	catch (const YAML::ParserException& ex)
	{
		// failed to parse, notify and return
		SPDLOG_ERROR("Failed to parse YAML in {}: {}", configFile, ex.what());
		return;
	}
	catch (const YAML::BadFile&)
	{
		return;
	}

	YAML::Node guildHallClickies = guildHallClickiesConfig["Clickies"];

	if (guildHallClickies.IsMap() == false)
	{
		// TODO: Error or... default or...
		return;
	}

	auto& map = guildHallClickies.as<std::map<std::string, std::vector<GuildHallClickyItem>, ci_less>>();

	for (auto& [name, locations] : map)
	{
		for (auto& location : locations)
		{
			// TODO: better way to handle this
			//auto zoneIndex = pWorldData->GetIndexFromZoneName(name.c_str());
			//auto zoneDetails = pWorldData->GetZone(zoneIndex);

			location.zoneShortName = name;

			EQZoneInfo* pZone = pWorldData->GetZone(GetZoneID(name.c_str()));
			if (pZone == NULL)
			{
				location.zoneName = fmt::format("*** INVALID: ({})", name);
			}
			else if (location.zoneName.empty())
			{
				location.zoneName = pZone->LongName;
			}

			if (location.guildClickyCommand.empty())
			{
				location.guildClickyCommand = location.zoneShortName;
			}

			auto iterPref = std::find(m_enabledGuildHallClickies.begin(), m_enabledGuildHallClickies.end(), location.itemName);
			location.enabled = iterPref != m_enabledGuildHallClickies.end();

			m_allGuildHallClickies.push_back(location);
		}
	}
}

// TODO: Can move to more appropriate location
const bool EasyFindConfiguration::CurrentlyInAGuildHall() const
{
	EQZoneInfo* pCurrentZone = pWorldData->GetZone(pLocalPC->currentZoneId);
	switch (pCurrentZone->Id)
	{
	case 751: // Guldhall3 (Modest Guild Hall)
	case 737: // guildhalllrg (Palatial Guild Hall)
	case 738: // guildhallsml (Grand Guild Hall)
		return true;
	default:
		return false;
	}
}

void EasyFindConfiguration::DetermineGuildHallClickies()
{
	if (CurrentlyInAGuildHall() == false)
	{
		return;
	}

	m_enabledGuildHallClickies.clear();
	for (auto clicky = m_allGuildHallClickies.begin(); clicky != m_allGuildHallClickies.end(); clicky++)
	{
		auto x = GetGroundSpawnByName(clicky->itemName);
		auto exists = x.Type == MQGroundSpawnType::Placed;
		clicky->enabled = exists;
		if (exists)
			m_enabledGuildHallClickies.push_back(clicky->itemName);
	}

	m_configNode["EnabledGuildHallClickies"] = m_enabledGuildHallClickies;
	SaveSettings();
	m_requireConnectionReload = true;
}

const bool EasyFindConfiguration::GetUseGuildClickies() const
{
	return m_useGuildClickies;
}

void EasyFindConfiguration::SetUseGuildClickies(bool useGuildClickies)
{
	m_useGuildClickies = useGuildClickies; 
	m_configNode["UseGuildClickies"] = m_useGuildClickies;
	SaveSettings();
	m_requireConnectionReload = true;
}

const bool EasyFindConfiguration::GetUseGuildClickyLua() const
{
	return m_useGuildClickyLua;
}

void EasyFindConfiguration::SetUseGuildClickyLua(bool useGuildClickyLua)
{
	m_useGuildClickyLua = useGuildClickyLua;
	m_configNode["UseGuildClickyLua"] = m_useGuildClickyLua;
	SaveSettings();
	m_requireConnectionReload = true;
}

bool EasyFindConfiguration::RequireReload()
{
	if (m_requireConnectionReload == false)
		return false;

	m_requireConnectionReload = false;
	return true;
}

void EasyFindConfiguration::SetEnabledGuildHallClicky(GuildHallClickyItem* clicky, bool enabled)
{
	auto iterClicky = std::find_if(
		m_allGuildHallClickies.begin(), m_allGuildHallClickies.end(),
		[&](const GuildHallClickyItem& value) { return value.itemName == clicky->itemName; });

	if (iterClicky->enabled == enabled)
		return;

	iterClicky->enabled = enabled;
	auto iterPref = std::find(m_enabledGuildHallClickies.begin(), m_enabledGuildHallClickies.end(), iterClicky->itemName);

	bool isDirty = false;

	if (enabled && iterPref == m_enabledGuildHallClickies.end())
	{
		m_enabledGuildHallClickies.push_back(iterClicky->itemName);
		isDirty = true;
	}
	else if (enabled == false && iterPref != m_enabledGuildHallClickies.end())
	{
		m_enabledGuildHallClickies.erase(iterPref);
		isDirty = true;
	}

	if (isDirty == false)
		return;

	m_configNode["EnabledGuildHallClickies"] = m_enabledGuildHallClickies;
	SaveSettings();
	m_requireConnectionReload = true;
}

void EasyFindConfiguration::LoadDisabledTransferTypes()
{
	std::vector<std::string> disabledTransferTypes;

	YAML::Node node = m_configNode["DisabledTransferTypes"];
	if (node.IsDefined())
	{
		disabledTransferTypes = node.as<std::vector<std::string>>(std::vector<std::string>());
	}

	m_disabledTransferTypesPrefs = std::move(disabledTransferTypes);
}

bool EasyFindConfiguration::IsSupportedTransferType(int transferTypeIndex) const
{
	if (transferTypeIndex < 0 || transferTypeIndex >= (int)m_supportedTransferTypes.size())
		return false;

	return m_supportedTransferTypes[transferTypeIndex];
}

bool EasyFindConfiguration::IsDisabledTransferType(int transferTypeIndex) const
{
	if (transferTypeIndex < 0 || transferTypeIndex >= (int)m_supportedTransferTypes.size())
		return true;

	if (!m_supportedTransferTypes[transferTypeIndex])
		return true;

	return m_disabledTransferTypes[transferTypeIndex];
}

void EasyFindConfiguration::SetDisabledTransferType(int transferTypeIndex, bool disabled)
{
	if (transferTypeIndex < 0 || transferTypeIndex >= (int)m_supportedTransferTypes.size())
		return;

	m_disabledTransferTypes[transferTypeIndex] = disabled;

	CXStr transferTypeName = ZoneGuideManagerClient::Instance().GetZoneTransferTypeNameByIndex(transferTypeIndex);

	if (transferTypeName.empty())
		return;

	auto iter = std::find_if(
		m_disabledTransferTypesPrefs.begin(), m_disabledTransferTypesPrefs.end(),
		[&](const std::string& value) { return value == transferTypeName; });

	if (disabled)
	{
		if (iter == m_disabledTransferTypesPrefs.end())
			m_disabledTransferTypesPrefs.push_back(std::string(transferTypeName));

		m_configNode["DisabledTransferTypes"] = m_disabledTransferTypesPrefs;
	}
	else
	{
		if (iter != m_disabledTransferTypesPrefs.end())
			m_disabledTransferTypesPrefs.erase(iter);

		m_configNode["DisabledTransferTypes"] = m_disabledTransferTypesPrefs;
	}

	SaveSettings();
}

//----------------------------------------------------------------------------

ConfiguredGroupPlugin EasyFindConfiguration::GetPreferredGroupPlugin() const
{
	return m_groupPluginSelection;
}

void EasyFindConfiguration::SetPreferredGroupPlugin(ConfiguredGroupPlugin p)
{
	m_groupPluginSelection = p;
	m_configNode["GroupPlugin"] = p;

	SaveSettings();
}

ConfiguredGroupPlugin EasyFindConfiguration::GetActiveGroupPlugin() const
{
	switch (m_groupPluginSelection)
	{
	case ConfiguredGroupPlugin::Auto:
		if (m_dannetLoaded)
			return ConfiguredGroupPlugin::Dannet;
		if (m_eqbcLoaded)
			return ConfiguredGroupPlugin::EQBC;
		break;
	case ConfiguredGroupPlugin::Dannet:
		if (m_dannetLoaded)
			return ConfiguredGroupPlugin::Dannet;
		break;
	case ConfiguredGroupPlugin::EQBC:
		if (m_eqbcLoaded)
			return ConfiguredGroupPlugin::EQBC;
		break;
	default:
		break;
	}

	return ConfiguredGroupPlugin::None;
}

void EasyFindConfiguration::HandlePluginChange(std::string_view pluginName, bool loaded)
{
	if (ci_equals(pluginName, "MQ2EQBC"))
	{
		m_eqbcLoaded = loaded;
	}
	else if (ci_equals(pluginName, "MQ2DanNet"))
	{
		m_dannetLoaded = loaded;
	}
}
