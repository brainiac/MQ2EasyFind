
#pragma once

#include "EasyFind.h"
#include "eqlib/WindowOverride.h"

#include <glm/vec3.hpp>

//----------------------------------------------------------------------------

class CFindLocationWndOverride : public WindowOverride<CFindLocationWndOverride, CFindLocationWnd>
{
public:
	enum class CustomRefType {
		Added,
		Modified,
	};

	struct RefData {
		CustomRefType type = CustomRefType::Added;
		const FindableLocation* data = nullptr;
	};

	//----------------------------------------------------------------------------
	// overrides

	virtual int OnProcessFrame() override;
	virtual bool AboutToShow() override;
	virtual int OnZone() override;
	virtual int WndNotification(CXWnd* sender, uint32_t message, void* data) override;

	uint32_t GetAvailableId();

	//----------------------------------------------------------------------------
	// zone connection handling

	void AddZoneConnection(const FindableLocation& findableLocation);
	void AddCustomLocations(bool initial);
	void RemoveCustomLocations();
	void UpdateListRowColor(int row);

	void UpdateDistanceColumn();

	FindableReference* GetReferenceForListIndex(int index) const;
	CVector3 GetReferencePosition(FindableReference* ref, bool& found);

	// Returns true if we handled the navigation here. Returns false if we couldn't do it
	// and that we should let the path get created so we can navigate to it.
	bool PerformFindWindowNavigation(int refId, bool asGroup);

	bool IsCustomLocationsAdded() const { return sm_customLocationsAdded; }

	MQColor GetColorForReference(int refId);
	RefData* GetCustomRefData(int refId);
	const FindZoneConnectionData* GetOriginalZoneConnectionData(int index);

public:
	void FindLocationByRefNum(int refNum, bool group);

	template <typename T>
	int FindClosestLocation(T&& callback)
	{
		int closestIndex = -1;
		float closestDistance = FLT_MAX;
		CVector3 myPos = { pLocalPlayer->Y, pLocalPlayer->X, pLocalPlayer->Z };

		for (int i = 0; i < findLocationList->GetItemCount(); ++i)
		{
			if (callback(i))
			{
				// Get distance to target.
				FindableReference* ref = GetReferenceForListIndex(i);
				if (ref)
				{
					bool found = false;
					CVector3 pos = GetReferencePosition(ref, found);
					if (found)
					{
						float distance = myPos.GetDistanceSquared(pos);
						if (distance < closestDistance)
						{
							closestDistance = distance;
							closestIndex = i;
						}
					}
				}
			}
		}

		return closestIndex;
	}

	bool FindZoneConnectionByZoneIndex(EQZoneIndex zoneId, bool group);
	bool FindLocation(std::string_view searchTerm, bool group);

	void OnHooked();
	void OnAboutToUnhook();

	static void OnHooked(CFindLocationWndOverride* pWnd) { pWnd->OnHooked(); }
	static void OnAboutToUnhook(CFindLocationWndOverride* pWnd) { pWnd->OnAboutToUnhook(); }

private:
	bool FindLocationByListIndex(int listIndex, bool group);



	// our "member variables" are static because we can't actually add new member variables,
	// but we only ever have one instance of CFindLocationWnd, so this works out to be about the same.

	static inline int sm_distanceColumn = -1;
	static inline std::chrono::steady_clock::time_point sm_lastDistanceUpdate;

	// tracks whether the custom locations have been added to the window or not.
	static inline bool sm_customLocationsAdded = false;

	// container holding our custom ref ids and their types.
	static inline std::map<int, RefData> sm_customRefs;

	// the original zone connections for values that we overwrote.
	static inline std::map<int, FindZoneConnectionData> sm_originalZoneConnections;

	// Holds queued commands in case we try to start a bit too early.
	static inline std::string sm_queuedSearchTerm;
	static inline bool sm_queuedGroupParam = false;
	static inline EQZoneIndex sm_queuedZoneId = 0;
};
