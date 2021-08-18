
#pragma once

#include "EasyFind.h"

#pragma warning( push )
#pragma warning( disable:4996 )
#include <yaml-cpp/yaml.h>
#pragma warning( pop )

#include <string>

class ZoneConnections
{
public:
	ZoneConnections(const std::string& configDirectory);
	~ZoneConnections();

	const std::string& GetConfigDir() const { return m_configDirectory; }

	void Load();
	void LoadZoneConnections();

	void GenerateFindableLocations(FindableLocations& findableLocations, std::vector<ParsedFindableLocation>&& parsedLocations);

	bool MigrateIniData();

private:
	std::string m_configDirectory;
	YAML::Node m_zoneConnectionsConfig;
};

extern ZoneConnections* g_zoneConnections;
