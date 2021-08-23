
#pragma once

#include <mq/Plugin.h>

#include <spdlog/common.h>

#pragma warning( push )
#pragma warning( disable:4996 )
#include <yaml-cpp/yaml.h>
#pragma warning( pop )

#include <memory>

enum class ConfiguredColor
{
	AddedLocation,
	ModifiedLocation,

	MaxColors,
};

namespace spdlog {
	namespace sinks {
		class sink;
	}
}

class EasyFindConfiguration
{
public:
	EasyFindConfiguration();
	~EasyFindConfiguration();

	void LoadSettings();
	void SaveSettings();

	void ReloadSettings();

	void SetColor(ConfiguredColor color, MQColor value) { m_configuredColors[(int)color] = value; }
	MQColor GetColor(ConfiguredColor color) const { return m_configuredColors[(int)color]; }
	MQColor GetDefaultColor(ConfiguredColor color) const;

	void SetLogLevel(spdlog::level::level_enum level);
	spdlog::level::level_enum GetLogLevel() const;

	void SetNavLogLevel(spdlog::level::level_enum level);
	spdlog::level::level_enum GetNavLogLevel() const;

	int GetNavDistance() const { return 15; }

	// transfer types
	void RefreshTransferTypes();
	bool IsSupportedTransferType(int transferTypeIndex) const;
	bool IsDisabledTransferType(int transferTypeIndex) const;
	void SetDisabledTransferType(int transferTypeIndex, bool disabled);

private:
	void LoadDisabledTransferTypes();

private:
	std::string m_configFile;
	YAML::Node m_configNode;

	std::vector<std::string> m_disabledTransferTypesPrefs; // user pref
	std::vector<bool> m_supportedTransferTypes;            // hardcoded disabled, converted to index when data is loaded
	std::vector<bool> m_disabledTransferTypes;             // converted to index when data is loaded

	std::shared_ptr<spdlog::sinks::sink> m_chatSink;
	std::array<MQColor, (size_t)ConfiguredColor::MaxColors> m_configuredColors;

	spdlog::level::level_enum m_navLogLevel = spdlog::level::err;
};

extern EasyFindConfiguration* g_configuration;
