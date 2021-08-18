
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

MQColor EasyFindConfiguration::GetDefaultColor(ConfiguredColor color) const
{
	return s_defaultColors[(int)color];
}
