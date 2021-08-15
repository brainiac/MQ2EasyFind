
#pragma once

enum class ConfiguredColor
{
	AddedLocation,
	ModifiedLocation,
};

class EasyFindConfiguration
{
public:
	EasyFindConfiguration();
	~EasyFindConfiguration();

	void LoadSettings();
	void LoadZoneSettings();

	void ReloadSettings();

	MQColor GetColor(ConfiguredColor color) const;

	void MigrationCommand();

private:
	void SaveConfigurationFile();
	void GenerateFindableLocations(FindableLocations& findableLocations, std::vector<ParsedFindableLocation>&& parsedLocations);

	bool MigrateIniFileData();

private:
	std::string m_configFile;

	MQColor m_addedLocationColor;
	MQColor m_modifiedLocationColor;
};

extern EasyFindConfiguration* g_configuration;
