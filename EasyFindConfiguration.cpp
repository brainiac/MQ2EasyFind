
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

std::array<MQColor, (size_t)ConfiguredColor::MaxColors> s_defaultColors = {
	MQColor(96, 255, 72),          // AddedLocation
	MQColor(64, 192, 255),         // ModifiedLocation
};

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

//============================================================================
//============================================================================

EasyFindConfiguration::EasyFindConfiguration()
{
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

	LoadSettings();
}

EasyFindConfiguration::~EasyFindConfiguration()
{
	spdlog::shutdown();
}

void EasyFindConfiguration::SetLogLevel(spdlog::level::level_enum level)
{
	m_chatSink->set_level(level);
	m_configNode["GlobalLogLevel"] = level;

	SaveSettings();
}

spdlog::level::level_enum EasyFindConfiguration::GetLogLevel() const
{
	return m_chatSink->level();
}

void EasyFindConfiguration::SetNavLogLevel(spdlog::level::level_enum level)
{
	m_navLogLevel = level;
	m_configNode["NavLogLevel"] = level;

	SaveSettings();
}

spdlog::level::level_enum EasyFindConfiguration::GetNavLogLevel() const
{
	return m_navLogLevel;
}

void EasyFindConfiguration::ReloadSettings()
{
	SPDLOG_INFO("Reloading settings");
	LoadSettings();
}

void EasyFindConfiguration::LoadSettings()
{
	try
	{
		m_configNode = YAML::LoadFile(m_configFile);

		spdlog::level::level_enum globalLogLevel = m_configNode["GlobalLogLevel"].as<spdlog::level::level_enum>(spdlog::level::info);
		SetLogLevel(globalLogLevel);

		LoadDisabledTransferTypes();
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
		y_out.SetFloatPrecision(2);
		y_out.SetDoublePrecision(2);
		y_out << m_configNode;

		file << y_out.c_str();
	}
}

MQColor EasyFindConfiguration::GetDefaultColor(ConfiguredColor color) const
{
	return s_defaultColors[(int)color];
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
	size_t numTransferTypes = mgr.transferTypes.size();

	// these things are hard-coded to disabled above because they always need
	// extra information to make them work.
	m_supportedTransferTypes.resize(numTransferTypes);
	for (size_t i = 0; i < numTransferTypes; ++i)
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
	for (size_t i = 0; i < numTransferTypes; ++i)
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

		SaveSettings();
	}
	else
	{
		if (iter != m_disabledTransferTypesPrefs.end())
			m_disabledTransferTypesPrefs.erase(iter);

		SaveSettings();
	}
}
