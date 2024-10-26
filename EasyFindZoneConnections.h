
#pragma once

#include "EasyFind.h"

#pragma warning( push )
#pragma warning( disable:4996 )
#include <yaml-cpp/yaml.h>
#pragma warning( pop )

#include <string>

// Information parsed from YAML
struct ParsedTranslocatorDestination
{
	std::string keyword;
	EQZoneIndex zoneId = 0;           // numeric zone id
	int zoneIdentifier = 0;
};

struct ParsedFindableLocation
{
	std::string typeString;
	LocationType type;                // interpreted type
	std::optional<glm::vec3> location;
	std::string name;
	EQZoneIndex zoneId = 0;           // numeric zone id
	int zoneIdentifier = 0;
	int switchId = -1;                // switch num, or -1 if not set
	std::string switchName;           // switch name, or "none"
	std::string luaScript;
	std::string luaScriptFile;
	bool replace = true;
	bool remove = false;
	std::vector<ParsedTranslocatorDestination> translocatorDestinations;

	EQExpansionOwned requiredExpansions = (EQExpansionOwned)0;
	int requiredAchievement = 0;
	std::string requiredAchievementName;

	bool IsZoneConnection() const;

	bool CheckRequirements() const; // returns true if requirements are met.
};
using ParsedFindableLocationsMap = std::map<std::string, std::vector<ParsedFindableLocation>, ci_less>;

//----------------------------------------------------------------------------

struct EZZoneData
{
	EQZoneIndex zoneId;

	// our custom list of findable locations
	std::vector<ParsedFindableLocation> findableLocations;

	// list of removed connections
	std::vector<int> removedConnections;
};

using FindableLocationsMap = std::map<std::string, EZZoneData, ci_less>;

class ZoneConnections
{
public:
	ZoneConnections(const std::string& configDirectory);
	~ZoneConnections();

	const std::string& GetConfigDir() const { return m_easyfindDir; }

	void Load(std::string_view customFile = {});
	void LoadOverride(std::string_view customFile);
	void LoadFindableLocations();

	void ReloadFindableLocations(std::string_view customFile = {});

	void CreateFindableLocations(FindableLocations& findableLocations, std::string_view shortName);

	bool MigrateIniData();

	const EZZoneData& GetZoneData(EQZoneIndex zoneId) const;

	void Pulse();

private:
	std::string m_easyfindDir;
	YAML::Node m_zoneConnectionsConfig;
	YAML::Node m_zoneConnectionsOverrideConfig;

	bool m_transferTypesLoaded = false;
	bool m_zoneDataLoaded = false;

	// Loaded findable locations
	FindableLocationsMap m_findableLocations;

	void LoadFindableLocations_Internal(YAML::Node zoneConnectionsConfig);
};

extern ZoneConnections* g_zoneConnections;
