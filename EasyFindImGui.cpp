
#include "EasyFind.h"
#include "EasyFindConfiguration.h"

#include "imgui/ImGuiUtils.h"
#include "imgui/ImGuiTextEditor.h"

static imgui::TextEditor* s_luaCodeViewer = nullptr;
static bool s_focusWindow = false;

void DrawEasyFindSettingsPanel();

static void DrawFindZoneConnectionData(const CFindLocationWnd::FindZoneConnectionData& data)
{
	ImGui::Text("Type:"); ImGui::SameLine(0.0f, 4.0f);
	ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", FindLocationTypeToString(data.type));

	ImGui::Text("Zone ID: %d", data.zoneId);

	ImGui::Text("Zone Name:"); ImGui::SameLine(0.0f, 4.0f);
	EQZoneInfo* pZoneInfo = pWorldData->GetZone(data.zoneId);
	if (pZoneInfo)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%s)", pZoneInfo->LongName, pZoneInfo->ShortName);
	}
	else
	{
		ImGui::TextColored(MQColor(127, 127, 127).ToImColor(), "(null)");
	}

	if (data.zoneIdentifier > 0)
	{
		ImGui::Text("Zone Identifier: %d", data.zoneIdentifier);
	}

	ImGui::Text("Location:"); ImGui::SameLine(0.0f, 4.0f);
	ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "(%.2f, %.2f, %.2f)", data.location.X, data.location.Y, data.location.Z);

	if (data.type == FindLocation_Switch)
	{
		ImGui::Text("Switch ID: %d", data.id);
		if (data.id >= 0)
		{
			EQSwitch* pSwitch = GetSwitchByID(data.id);
			ImGui::Text("Switch Name:"); ImGui::SameLine(0.0f, 4.0f);
			ImGui::TextColored(pSwitch ? MQColor(0, 255, 0).ToImColor() : MQColor(127, 127, 217).ToImColor(),
				"%s", pSwitch ? pSwitch->Name : "(null)");
		}
	}

	ImGui::Text("Sub ID: %d", data.subId);
}

static void DrawEasyFindZoneConnections()
{
	// refs can change, so we need two ways to determine if we're still on the selected item.
	static int selectedRef = -1;
	static CFindLocationWnd::FindableReference selectedRefData = { FindLocation_Unknown, 0 };
	bool changedRef = false;

	bool foundSelectedRef = false;
	CFindLocationWndOverride* findLocWnd = pFindLocationWnd.get_as<CFindLocationWndOverride>();

	ImGui::BeginGroup();
	{
		ImGui::BeginChild("Entry List", ImVec2(275, 0), true);

		if (ImGui::BeginTable("##Entries", 2, ImGuiTableFlags_ScrollY))
		{
			ImGui::TableSetupColumn("Category");
			ImGui::TableSetupColumn("Description");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			if (findLocWnd)
			{
				for (int i = 0; i < findLocWnd->findLocationList->GetItemCount(); ++i)
				{
					int refId = (int)findLocWnd->findLocationList->GetItemData(i);
					CFindLocationWnd::FindableReference* ref = findLocWnd->referenceList.FindFirst(refId);
					if (!ref) continue;

					static char label[256];

					ImGui::PushID(refId);

					CXStr category = findLocWnd->findLocationList->GetItemText(i, 0);
					CXStr description = findLocWnd->findLocationList->GetItemText(i, 1);

					ImGui::TableNextRow();
					ImGui::TableNextColumn();

					ImU32 textColor = findLocWnd->GetColorForReference(refId).ToImU32();

					ImGui::PushStyleColor(ImGuiCol_Text, textColor);

					bool selected = (selectedRef == refId || *ref == selectedRefData);
					if (selected)
					{
						changedRef = test_and_set(selectedRef, refId);
						selectedRefData = *ref;

						foundSelectedRef = true;
					}

					if (ImGui::Selectable(category.c_str(), &selected, ImGuiSelectableFlags_SpanAllColumns))
					{
						if (selected)
						{
							changedRef = test_and_set(selectedRef, refId);
							selectedRefData = *ref;

							foundSelectedRef = true;
						}
					}

					ImGui::TableNextColumn();
					ImGui::Text("%s", description.c_str());
					ImGui::PopStyleColor();

					ImGui::PopID();
				}
			}

			ImGui::EndTable();
		}

		ImGui::EndChild();
	}
	ImGui::EndGroup();

	if (!foundSelectedRef)
	{
		selectedRef = -1;
		selectedRefData = { FindLocation_Unknown, 0 };
	}

	ImGui::SameLine();

	ImGui::BeginGroup();
	{
		ImGui::BeginChild("Entry Viewer");

		if (selectedRef != -1 && findLocWnd)
		{
			CFindLocationWnd::FindableReference* ref = findLocWnd->referenceList.FindFirst(selectedRef);
			if (ref)
			{
				const CFindLocationWnd::FindPlayerData* playerData = nullptr;
				const CFindLocationWnd::FindZoneConnectionData* zoneConnectionData = nullptr;

				CFindLocationWndOverride::RefData* customRefData = findLocWnd->GetCustomRefData(selectedRef);

				if (ref->type == FindLocation_Player)
				{
					// Find the FindPlayerData with this playerId
					for (const CFindLocationWnd::FindPlayerData& data : findLocWnd->unfilteredPlayerList)
					{
						if (data.spawnId == ref->index)
						{
							playerData = &data;
							break;
						}
					}
				}
				else if (ref->type == FindLocation_Switch || ref->type == FindLocation_Location)
				{
					zoneConnectionData = &findLocWnd->unfilteredZoneConnectionList[ref->index];
				}

				// Render a title
				char title[256];

				if (playerData)
				{
					if (!playerData->description.empty())
						sprintf_s(title, "%s - %s", playerData->description.c_str(), playerData->name.c_str());
					else
						strcpy_s(title, playerData->name.c_str());
				}
				else if (zoneConnectionData)
				{
					EQZoneInfo* pZoneInfo = pWorldData->GetZone(zoneConnectionData->zoneId);
					const char* zoneName = pZoneInfo ? pZoneInfo->LongName : "(null)";

					if (zoneConnectionData->zoneIdentifier)
						sprintf_s(title, "Zone Connection - %s - %d", zoneName, zoneConnectionData->zoneIdentifier);
					else
						sprintf_s(title, "Zone Connection - %s", zoneName);
				}

				ImGui::TextColored(MQColor(255, 255, 0).ToImColor(), title);
				ImGui::Separator();

				ImGui::Text("Reference ID: %d", selectedRef);
				ImGui::Text("Status:"); ImGui::SameLine(0.0f, 4.0f);
				if (customRefData)
				{
					if (customRefData->type == CFindLocationWndOverride::CustomRefType::Added)
						ImGui::TextColored(g_configuration->GetColor(ConfiguredColor::AddedLocation).ToImColor(), "Added by EasyFind");
					else if (customRefData->type == CFindLocationWndOverride::CustomRefType::Modified)
						ImGui::TextColored(g_configuration->GetColor(ConfiguredColor::ModifiedLocation).ToImColor(), "Modified by EasyFind");
				}
				else
				{
					ImGui::TextColored(MQColor(127, 127, 127).ToImColor(), "Unmodified");
				}

				if (ImGui::Button("EasyFind"))
				{
					findLocWnd->FindLocationByRefNum(selectedRef, false);
				}

				ImGui::SameLine();
				if (ImGui::Button("Group EasyFind"))
				{
					findLocWnd->FindLocationByRefNum(selectedRef, true);
				}

				ImGui::NewLine();
				ImGui::TextColored(MQColor("#D040FF").ToImColor(), "Find Location Data:");
				ImGui::Separator();

				bool showModifiedData = (customRefData && customRefData->type == CFindLocationWndOverride::CustomRefType::Modified
					&& (ref->type == FindLocation_Switch || ref->type == FindLocation_Location));
				bool showLocationData = true;
				bool closeTabBar = false;

				if (showModifiedData)
				{
					closeTabBar = ImGui::BeginTabBar("##LocationDataTabs");
					if (closeTabBar)
					{
						if (!ImGui::BeginTabItem("Modified Data"))
							showLocationData = false;
					}
					else
					{
						showLocationData = false;
						showModifiedData = false;
					}
				}

				if (showLocationData)
				{
					if (ref->type == FindLocation_Player)
					{
						int playerId = ref->index;

						// Find the FindPlayerData with this playerId
						if (playerData)
						{
							ImGui::Text("Name: %s", playerData->name.c_str());
							ImGui::Text("Description: %s", playerData->description.c_str());
							ImGui::Text("Spawn ID: %d", playerData->spawnId);
							ImGui::Text("Race: %d", playerData->race);
							ImGui::Text("Class: %d", playerData->Class);
						}
						else
						{
							ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Could not find player '%d'", playerId);
						}
					}
					else if (ref->type == FindLocation_Switch || ref->type == FindLocation_Location)
					{
						const CFindLocationWnd::FindZoneConnectionData& data = findLocWnd->unfilteredZoneConnectionList[ref->index];

						DrawFindZoneConnectionData(data);
					}
					else
					{
						ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Unhandled location type!");
					}

					if (showModifiedData)
					{
						ImGui::EndTabItem();
					}
				}

				if (showModifiedData)
				{
					if (ImGui::BeginTabItem("Original Data"))
					{
						if (const CFindLocationWnd::FindZoneConnectionData* origData = findLocWnd->GetOriginalZoneConnectionData(ref->index))
						{
							DrawFindZoneConnectionData(*origData);
						}
						else
						{
							ImGui::TextColored(MQColor(255, 0, 0).ToImColor(), "Could not find original data!");
						}

						ImGui::EndTabItem();
					}

					ImGui::EndTabBar();
				}

				if (customRefData && customRefData->data)
				{
					ImGui::NewLine();
					ImGui::TextColored(MQColor("#D040FF").ToImColor(), "EasyFind Data:");
					ImGui::Separator();

					if (!s_luaCodeViewer)
					{
						s_luaCodeViewer = new imgui::TextEditor();
						s_luaCodeViewer->SetLanguageDefinition(imgui::texteditor::LanguageDefinition::Lua());
						s_luaCodeViewer->SetPalette(imgui::TextEditor::GetDarkPalette());
						s_luaCodeViewer->SetReadOnly(true);
						s_luaCodeViewer->SetRenderLineNumbers(false);
						s_luaCodeViewer->SetRenderCursor(false);
						s_luaCodeViewer->SetShowWhitespace(false);
					}

					const FindableLocation* data = customRefData->data;

					ImGui::Text("Type:"); ImGui::SameLine(0.0f, 4.0f);
					ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", LocationTypeToString(data->easyfindType));

					if (!data->name.empty())
					{
						ImGui::Text("Name:"); ImGui::SameLine(0.0f, 4.0f);
						ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", data->name.c_str());
					}

					if (!data->spawnName.empty())
					{
						ImGui::Text("Spawn Name:"); ImGui::SameLine(0.0f, 4.0f);
						ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s", data->spawnName.c_str());
					}

					ImGui::Text("Location:"); ImGui::SameLine(0.0f, 4.0f);
					ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "(%.2f, %.2f, %.2f)", data->location.x, data->location.y, data->location.z);

					ImGui::Text("Switch ID: %d", data->switchId);
					ImGui::Text("Switch Name: %s", data->switchName.c_str());

					EQZoneInfo* pZoneInfo = pWorldData->GetZone(data->zoneId);
					const char* zoneName = pZoneInfo ? pZoneInfo->ShortName : "(null)";

					ImGui::Text("Target Zone: %s (%d)", zoneName, data->zoneId);
					if (data->zoneIdentifier > 0)
					{
						ImGui::Text("Zone Identifier: %d", data->zoneIdentifier);
					}

					if (!data->translocatorKeyword.empty())
					{
						ImGui::Text("Translocator Keyword: %s", data->translocatorKeyword.c_str());
					}

					ImGui::Text("Replace Original:"); ImGui::SameLine(0.0f, 4.0f);
					ImGui::TextColored((data->replace ? MQColor(0, 255, 0) : MQColor(255, 0, 0)).ToImColor(), "%s", data->replace ? "Yes" : "No");

					if (!data->luaScript.empty())
					{
						ImGui::NewLine();
						ImGui::Text("Lua Script");
						ImGui::Separator();
						ImGui::PushFont(imgui::ConsoleFont);

						if (changedRef)
						{
							s_luaCodeViewer->SetText(data->luaScript);
						}

						s_luaCodeViewer->Render("Script", ImGui::GetContentRegionAvail());
						ImGui::PopFont();
					}
				}
			}
		}

		ImGui::EndChild();
	}
	ImGui::EndGroup();
}

static void DrawEasyFindZonePathGeneration()
{
	static char fromZone[256] = { 0 };
	ImGui::InputText("Starting Zone", fromZone, 256);

	if (ImGui::Button("Use Current##StartZone"))
	{
		if (EQZoneInfo* pZone = pWorldData->GetZone(ZoneGuideManagerClient::Instance().currentZone))
			strcpy_s(fromZone, pZone->ShortName);
	}

	static char toZone[256] = { 0 };
	ImGui::InputText("Destination Zone", toZone, 256);

	if (ImGui::Button("Use Current##DestZone"))
	{
		if (EQZoneInfo* pZone = pWorldData->GetZone(ZoneGuideManagerClient::Instance().currentZone))
			strcpy_s(toZone, pZone->ShortName);
	}

	if (ImGui::Button("Swap"))
	{
		static char tempZone[256];
		strcpy_s(tempZone, fromZone);
		strcpy_s(fromZone, toZone);
		strcpy_s(toZone, tempZone);
	}

	ImGui::Separator();

	EQZoneInfo* pFromZone = pWorldData->GetZone(GetZoneID(fromZone));
	ImGui::Text("From Zone:"); ImGui::SameLine(0.0f, 4.0f);
	if (pFromZone)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%d)", pFromZone->LongName, pFromZone->Id);
	}
	else
	{
		ImGui::TextColored(MQColor(255, 255, 255, 127).ToImColor(), "(none)");
	}

	EQZoneInfo* pToZone = pWorldData->GetZone(GetZoneID(toZone));
	ImGui::Text("To Zone:"); ImGui::SameLine(0.0f, 4.0f);
	if (pToZone)
	{
		ImGui::TextColored(MQColor(0, 255, 0).ToImColor(), "%s (%d)", pToZone->LongName, pToZone->Id);
	}
	else
	{
		ImGui::TextColored(MQColor(255, 255, 255, 127).ToImColor(), "(none)");
	}

	static std::vector<ZonePathData> s_zonePathTest;
	static std::string message;

	if (ImGui::Button("Generate"))
	{
		if (pFromZone && pToZone)
		{
			s_zonePathTest = GeneratePathToZone(pFromZone->Id, pToZone->Id, message);
		}
	}

	ImGui::SameLine();

	if (ImGui::Button("Clear"))
	{
		message.clear();
		s_zonePathTest.clear();
	}

	if (!message.empty())
	{
		ImGui::TextColored(ImColor(255, 255, 0), "%s", message.c_str());
	}

	if (!s_zonePathTest.empty())
	{
		if (ImGui::BeginTable("##Entries", 2))
		{
			ImGui::TableSetupColumn("Zone");
			ImGui::TableSetupColumn("Transfer type");
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			for (const ZonePathData& data : s_zonePathTest)
			{
				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%s", pWorldData->GetZone(data.zoneId)->LongName);

				ImGui::TableNextColumn();
				ImGui::Text("%s", ZoneGuideManagerClient::Instance().GetZoneTransferTypeNameByIndex(data.transferTypeIndex).c_str());
			}

			ImGui::EndTable();
		}

		if (ImGui::Button("Set Active"))
		{
			SetActiveZonePath(s_zonePathTest, false);
		}
		if (ImGui::Button("Travel"))
		{
			SetActiveZonePath(s_zonePathTest, true);
		}
	}
}

void DrawEasyFindSettingsPanel()
{
	bool changed = false;
	bool debugLogging = g_configuration->GetLogLevel() == spdlog::level::debug;
	if (ImGui::Checkbox("##debugLogging", &debugLogging))
	{
		g_configuration->SetLogLevel(debugLogging ? spdlog::level::debug : spdlog::level::info);
		g_configuration->SaveSettings();
	}
	ImGui::SameLine();
	ImGui::Text("Enable Debug Logging");


}

static void DrawEasyFindSettingsPanel_MQSettings()
{
	DrawEasyFindSettingsPanel();

	ImGui::NewLine();

	if (ImGui::Button("Open EasyFind Window"))
	{
		g_showWindow = true;
		s_focusWindow = true;
	}
}

void ImGui_Initialize()
{
	AddSettingsPanel("plugins/EasyFind", DrawEasyFindSettingsPanel_MQSettings);
}

void ImGui_Shutdown()
{
	RemoveSettingsPanel("plugins/EasyFind");

	delete s_luaCodeViewer;
}

void ImGui_OnUpdate()
{
	if (!g_showWindow)
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 440), ImGuiCond_FirstUseEver);
	if (s_focusWindow)
	{
		s_focusWindow = false;
		ImGui::SetNextWindowFocus();
	}
	if (ImGui::Begin("EasyFind", &g_showWindow, ImGuiWindowFlags_MenuBar))
	{
		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
				if (ImGui::MenuItem("Reload Configuration"))
				{
					g_configuration->ReloadSettings();
				}

				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		if (ImGui::BeginTabBar("EasyFindTabBar", ImGuiTabBarFlags_None))
		{
			if (ImGui::BeginTabItem("Zone Connections"))
			{
				DrawEasyFindZoneConnections();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Zone Path Generation"))
			{
				DrawEasyFindZonePathGeneration();
				ImGui::EndTabItem();
			}

			if (ImGui::BeginTabItem("Settings"))
			{
				DrawEasyFindSettingsPanel();
				ImGui::EndTabItem();
			}

			ImGui::EndTabBar();
		}
	}
	ImGui::End();
}
