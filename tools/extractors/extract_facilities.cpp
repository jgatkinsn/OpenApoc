#include "common/ufo2p.h"
#include "extractors.h"

#include "framework/framework.h"
#include "game/tileview/voxel.h"

namespace OpenApoc
{

void InitialGameStateExtractor::extractFacilities(GameState &state, Difficulty difficulty)
{
	auto &data = this->ufo2p;
	LogInfo("Number of facility strings: %zu", data.facility_names->count());
	LogInfo("Number of facility data chunks: %zu", data.facility_data->count());

	// Start at 2, as 'earth' and 'corridor' are handled specially, this aren't really 'facilities'
	// in openapoc terms
	for (int i = 2; i < data.facility_names->count(); i++)
	{
		UString id = data.get_facility_id(i);
		auto f = data.facility_data->get(i);

		LogInfo(
		    "Facility %d: %s cost %d image_offset %d size %d build_time %d maint %d capacity %d", i,
		    id.c_str(), (int)f.cost, (int)f.image_offset, (int)f.size, (int)f.build_time,
		    (int)f.maintainance_cost, (int)f.capacity);
		LogInfo("u1 0x%04x u2 0x%04x", (unsigned)f.unknown1, (unsigned)f.unknown2);

		auto facilityType = mksp<FacilityType>();
		facilityType->name = data.facility_names->get(i);
		facilityType->buildCost = f.cost;
		facilityType->buildTime = f.build_time;
		facilityType->weeklyCost = f.maintainance_cost;
		facilityType->capacityAmount = f.capacity;
		facilityType->size = f.size;
		facilityType->sprite = fw().data->load_image(UString::format(
		    "PCK:xcom3/UFODATA/BASE.PCK:xcom3/UFODATA/BASE.TAB:%d:xcom3/UFODATA/BASE.PCX",
		    (int)f.image_offset));

		state.facility_types[id] = facilityType;
	}
}

} // namespace OpenApoc