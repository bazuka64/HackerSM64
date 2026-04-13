#include "game/object_list_processor.h"
#include "behavior_data.h"
#include "game/farcall_helpers.h"

void bhv_risk_of_rain_init() {

	for (int i = 0;i < 10;i++) {
	label:
		const int size = 8000;
		s16 x = random_linear_offset(-size, 2 * size);
		s16 y = random_linear_offset(-size, 2 * size);
		s16 z = random_linear_offset(-size, 2 * size);
		struct Surface* floor = NULL;
		f32 height = find_floor(x, y, z, &floor);
		if (!floor || floor->type == SURFACE_DEATH_PLANE) {
			goto label;
		}
		struct Object* chest = spawn_object(o, MODEL_TREASURE_CHEST_BASE, bhvTreasureChestBottom);
		chest->oPosX = x;
		chest->oPosY = height;
		chest->oPosZ = z;
	}
}

void bhv_risk_of_rain_loop() {

}