
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

class ZoneConnections;

class EasyFindConfiguration
{
public:
	EasyFindConfiguration();
	~EasyFindConfiguration();

	void LoadSettings();
	void SaveSettings();

	void LoadZoneConnections();

	void ReloadSettings();
	void ReloadZoneConnections();

	void SetColor(ConfiguredColor color, MQColor value) { m_configuredColors[(int)color] = value; }
	MQColor GetColor(ConfiguredColor color) const { return m_configuredColors[(int)color]; }
	MQColor GetDefaultColor(ConfiguredColor color) const;

	void SetLogLevel(spdlog::level::level_enum level);
	spdlog::level::level_enum GetLogLevel() const;

	void MigrationCommand();

private:
	std::string m_configFile;
	YAML::Node m_configNode;

	std::unique_ptr<ZoneConnections> m_zoneConnections;

	std::shared_ptr<spdlog::sinks::sink> m_chatSink;

	std::array<MQColor, (size_t)ConfiguredColor::MaxColors> m_configuredColors;
};

extern EasyFindConfiguration* g_configuration;
