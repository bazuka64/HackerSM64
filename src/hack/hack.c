#include "PR/gbi.h"
#include "PR/ultratypes.h"
#include "types.h"
#include "engine/graph_node.h"
#include "game/geo_misc.h"
#include "game/area.h"
#include "textures.h"

#define GRID_SIZE 100
#define FLOOR_SIZE 1000
#define GRID_N     (FLOOR_SIZE * 2 / GRID_SIZE)
#define GRID_COLS  (GRID_N + 1)
#define GRID_VERTS (GRID_COLS * GRID_COLS)

struct Vertex {
	s16 x, y, z;
};
struct Vertex* vertices;

Vtx* vertices_dl;

void hook_from_free_level_pool(void) {
	vertices = main_pool_alloc(GRID_VERTS * sizeof(struct Vertex), MEMORY_POOL_LEFT);
	for (int j = 0; j < GRID_COLS; j++) {
		for (int i = 0; i < GRID_COLS; i++) {
			int idx = j * GRID_COLS + i;
			vertices[idx].x = -FLOOR_SIZE + i * GRID_SIZE;
			vertices[idx].y = 0;
			vertices[idx].z = -FLOOR_SIZE + j * GRID_SIZE;
		}
	}

	vertices_dl = main_pool_alloc(GRID_VERTS * sizeof(Vtx), MEMORY_POOL_LEFT);
	for (int j = 0; j < GRID_COLS; j++) {
		for (int i = 0; i < GRID_COLS; i++) {
			int idx = j * GRID_COLS + i;
			s16 x = vertices[idx].x;
			s16 y = vertices[idx].y;
			s16 z = vertices[idx].z;
			make_vertex(vertices_dl, idx, x, y, z, x * 2, z * 2, 255, 255, 255, 255);
		}
	}

	// Flat collision: single quad covering the whole floor
	Collision* col = main_pool_alloc((2 + GRID_VERTS * 3 + 2 + GRID_N * GRID_N * 8 + 2) * sizeof(Collision), MEMORY_POOL_LEFT);
	gAreas[1].terrainData = col;
	*col++ = COL_INIT();
	*col++ = COL_VERTEX_INIT(GRID_VERTS);
	for (int j = 0; j < GRID_COLS; j++) {
		for (int i = 0; i < GRID_COLS; i++) {
			int idx = j * GRID_COLS + i;
			s16 x = vertices[idx].x;
			s16 y = vertices[idx].y;
			s16 z = vertices[idx].z;
			*col++ = x; *col++ = y; *col++ = z;
		}
	}
	*col++ = SURFACE_DEFAULT;
	*col++ = GRID_N * GRID_N * 2; // tri_count
	for (int j = 0; j < GRID_N; j++) {
		for (int i = 0; i < GRID_N; i++) {
			*col++ = j * GRID_COLS + i;
			*col++ = (j + 1) * GRID_COLS + i;
			*col++ = (j + 1) * GRID_COLS + i + 1;
			*col++ = 0;
			*col++ = j * GRID_COLS + i;
			*col++ = (j + 1) * GRID_COLS + i + 1;
			*col++ = j * GRID_COLS + i + 1;
			*col++ = 0;
		}
	}
	*col++ = COL_TRI_STOP();
	*col++ = COL_END();
}

Gfx* hook_from_geo_asm(s32 callContext, struct GraphNode* node, Mat4 mtxf) {
	Gfx* head = NULL;

	if (callContext == GEO_CONTEXT_RENDER && vertices_dl != NULL) {
		// Texture setup (10) + per quad (2 gSPVertex + 2 gSP1Triangle = 4) * GRID_N^2 + cleanup (2)
		head = alloc_display_list((10 + GRID_N * GRID_N * 4 + 2) * sizeof(Gfx));
		Gfx* dl = head;

		gDPPipeSync(dl++);
		gSPTexture(dl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
		gDPSetCombineMode(dl++, G_CC_DECALRGBA, G_CC_DECALRGBA);
		gDPSetTextureImage(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, generic_09005800);
		gDPSetTile(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0,
			G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD,
			G_TX_WRAP | G_TX_NOMIRROR, G_TX_NOMASK, G_TX_NOLOD);
		gDPLoadSync(dl++);
		gDPLoadBlock(dl++, G_TX_LOADTILE, 0, 0, (32 * 32) - 1, CALC_DXT(32, G_IM_SIZ_16b_BYTES));
		gDPPipeSync(dl++);
		gDPSetTile(dl++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 0, G_TX_RENDERTILE, 0,
			G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD,
			G_TX_WRAP | G_TX_NOMIRROR, 5, G_TX_NOLOD);
		gDPSetTileSize(dl++, G_TX_RENDERTILE, 0, 0, (32 - 1) << 2, (32 - 1) << 2);

		for (int j = 0; j < GRID_N; j++) {
			for (int i = 0; i < GRID_N; i++) {
				gSPVertex(dl++, &vertices_dl[j * GRID_COLS + i], 2, 0);
				gSPVertex(dl++, &vertices_dl[(j + 1) * GRID_COLS + i], 2, 2);
				gSP1Triangle(dl++, 0, 2, 3, 0);
				gSP1Triangle(dl++, 0, 3, 1, 0);
			}
		}

		gSPTexture(dl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
		gSPEndDisplayList(dl++);
	}

	return head;
}
