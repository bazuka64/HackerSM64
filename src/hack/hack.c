#include "PR/gbi.h"
#include "PR/ultratypes.h"
#include "types.h"
#include "engine/graph_node.h"
#include "game/geo_misc.h"
#include "game/area.h"
#include "textures.h"
#include "string.h"
#include "engine/math_util.h"

#include "hack.h"

// struct Surface の vertex1/2/3 から normal と originOffset を再計算する
static void surface_recalc_normal(struct Surface* s) {
	f32 ax = s->vertex2[0] - s->vertex1[0];
	f32 ay = s->vertex2[1] - s->vertex1[1];
	f32 az = s->vertex2[2] - s->vertex1[2];
	f32 bx = s->vertex3[0] - s->vertex1[0];
	f32 by = s->vertex3[1] - s->vertex1[1];
	f32 bz = s->vertex3[2] - s->vertex1[2];
	f32 nx = ay * bz - az * by;
	f32 ny = az * bx - ax * bz;
	f32 nz = ax * by - ay * bx;
	f32 len = sqrtf(nx * nx + ny * ny + nz * nz);
	if (len > 0.0f) {
		s->normal.x = nx / len;
		s->normal.y = ny / len;
		s->normal.z = nz / len;
		s->originOffset = -(s->normal.x * s->vertex1[0]
			+ s->normal.y * s->vertex1[1]
			+ s->normal.z * s->vertex1[2]);
	}
}

struct Vertex {
	s16 x, y, z;
};
struct Triangle {
	u16 indices[3];
};
struct Quad {
	struct Triangle tris[4];
};

struct Vertex* vertices;
struct Quad* quads;
Vtx* vertices_dl;
struct Surface** surface_pointer_array;

void hook_from_free_level_pool(void) {

	vertices = main_pool_alloc((VERT_NUM_ALL + QUAD_NUM_ALL) * sizeof(struct Vertex), MEMORY_POOL_LEFT);
	for (int j = 0; j < VERT_NUM_LINE; j++) {
		for (int i = 0; i < VERT_NUM_LINE; i++) {
			int idx = j * VERT_NUM_LINE + i;
			vertices[idx].x = -FLOOR_HALF_SIZE + i * QUAD_SIZE;
			vertices[idx].y = 0;
			vertices[idx].z = -FLOOR_HALF_SIZE + j * QUAD_SIZE;
		}
	}
	for (int j = 0; j < QUAD_NUM_LINE; j++) {
		for (int i = 0; i < QUAD_NUM_LINE; i++) {
			int idx = VERT_NUM_ALL + j * QUAD_NUM_LINE + i;
			vertices[idx].x = -FLOOR_HALF_SIZE + QUAD_SIZE / 2 + i * QUAD_SIZE;
			vertices[idx].y = 0;
			vertices[idx].z = -FLOOR_HALF_SIZE + QUAD_SIZE / 2 + j * QUAD_SIZE;
		}
	}

	quads = main_pool_alloc(QUAD_NUM_ALL * sizeof(struct Quad), MEMORY_POOL_LEFT);
	for (int j = 0; j < QUAD_NUM_LINE; j++) {
		for (int i = 0; i < QUAD_NUM_LINE; i++) {
			struct Quad* quad = &quads[j * QUAD_NUM_LINE + i];
			quad->tris[0].indices[0] = j * VERT_NUM_LINE + i;
			quad->tris[0].indices[1] = VERT_NUM_ALL + j * QUAD_NUM_LINE + i;
			quad->tris[0].indices[2] = j * VERT_NUM_LINE + i + 1;

			quad->tris[1].indices[0] = j * VERT_NUM_LINE + i;
			quad->tris[1].indices[1] = (j + 1) * VERT_NUM_LINE + i;
			quad->tris[1].indices[2] = VERT_NUM_ALL + j * QUAD_NUM_LINE + i;

			quad->tris[2].indices[0] = VERT_NUM_ALL + j * QUAD_NUM_LINE + i;
			quad->tris[2].indices[1] = (j + 1) * VERT_NUM_LINE + i;
			quad->tris[2].indices[2] = (j + 1) * VERT_NUM_LINE + i + 1;

			quad->tris[3].indices[0] = VERT_NUM_ALL + j * QUAD_NUM_LINE + i;
			quad->tris[3].indices[1] = (j + 1) * VERT_NUM_LINE + i + 1;
			quad->tris[3].indices[2] = j * VERT_NUM_LINE + i + 1;
		}
	}

	vertices_dl = main_pool_alloc((VERT_NUM_ALL + QUAD_NUM_ALL) * sizeof(Vtx), MEMORY_POOL_LEFT);
	for (int i = 0; i < VERT_NUM_ALL + QUAD_NUM_ALL; i++) {
		s16 x = vertices[i].x;
		s16 y = vertices[i].y;
		s16 z = vertices[i].z;
		make_vertex(vertices_dl, i, x, y, z, x * 2, z * 2, 255, 255, 255, 255);
	}

	Collision* col = main_pool_alloc((2 + (VERT_NUM_ALL + QUAD_NUM_ALL) * 3 + 2 + QUAD_NUM_ALL * 16 + 2) * sizeof(Collision), MEMORY_POOL_LEFT);
	gAreas[1].terrainData = col;
	*col++ = COL_INIT();
	*col++ = COL_VERTEX_INIT(VERT_NUM_ALL + QUAD_NUM_ALL);
	memcpy(col, vertices, (VERT_NUM_ALL + QUAD_NUM_ALL) * 6);
	col += (VERT_NUM_ALL + QUAD_NUM_ALL) * 3;

	*col++ = SURFACE_NOT_SLIPPERY;
	*col++ = QUAD_NUM_ALL * 4; // tri_count
	for (int j = 0; j < QUAD_NUM_LINE; j++) {
		for (int i = 0; i < QUAD_NUM_LINE; i++) {
			struct Quad* quad = &quads[j * QUAD_NUM_LINE + i];
			for (int k = 0;k < 4;k++) {
				*col++ = quad->tris[k].indices[0];
				*col++ = quad->tris[k].indices[1];
				*col++ = quad->tris[k].indices[2];
				*col++ = 0;
			}
		}
	}
	*col++ = COL_TRI_STOP();
	*col++ = COL_END();

	// サーフェスポインタアレイ
	surface_pointer_array = main_pool_alloc(sizeof(struct Surface*) * QUAD_NUM_ALL * 4, MEMORY_POOL_LEFT);
}

Gfx* hook_from_geo_asm(s32 callContext, struct GraphNode* node, Mat4 mtxf) {
	Gfx* head = NULL;

	if (callContext == GEO_CONTEXT_RENDER && vertices_dl != NULL) {
		// Texture setup (10) + per quad (2 gSPVertex + 2 gSP1Triangle = 4) * GRID_N^2 + cleanup (2)
		head = alloc_display_list((10 + QUAD_NUM_ALL * 7 + 2) * sizeof(Gfx));
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

		for (int j = 0; j < QUAD_NUM_LINE; j++) {
			for (int i = 0; i < QUAD_NUM_LINE; i++) {
				struct Quad* quad = &quads[j * QUAD_NUM_LINE + i];
				u16 upper_left = quad->tris[0].indices[0];
				u16 center = quad->tris[0].indices[1];
				u16 lower_left = quad->tris[2].indices[1];
				gSPVertex(dl++, &vertices_dl[upper_left], 2, 0);
				gSPVertex(dl++, &vertices_dl[lower_left], 2, 2);
				gSPVertex(dl++, &vertices_dl[center], 1, 4);
				gSP1Triangle(dl++, 0, 4, 1, 0);
				gSP1Triangle(dl++, 0, 2, 4, 0);
				gSP1Triangle(dl++, 4, 2, 3, 0);
				gSP1Triangle(dl++, 4, 3, 1, 0);
			}
		}

		gSPTexture(dl++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
		gSPEndDisplayList(dl++);
	}

	return head;
}

void hook_from_ground_pound_land(struct MarioState* m) {
	u16 tri_id = m->floor->id;
	u16 quad_id = tri_id / 4;
	struct Quad* quad = &quads[quad_id];
#define deep 50

	u16 upper_left = quad->tris[0].indices[0];
	u16 center = quad->tris[0].indices[1];
	u16 upper_right = quad->tris[0].indices[2];
	u16 lower_left = quad->tris[2].indices[1];
	u16 lower_right = quad->tris[2].indices[2];
	vertices_dl[upper_left].v.ob[1] -= deep;
	vertices_dl[center].v.ob[1] -= deep;
	vertices_dl[upper_right].v.ob[1] -= deep;
	vertices_dl[lower_left].v.ob[1] -= deep;
	vertices_dl[lower_right].v.ob[1] -= deep;

	// up
	if (quad_id / QUAD_NUM_LINE > 0) {
		struct Quad* up_quad = &quads[quad_id - QUAD_NUM_LINE];
		u16 up_center = up_quad->tris[0].indices[1];
		vertices_dl[up_center].v.ob[1] -= deep / 2;
	}
	// down
	if (quad_id / QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		struct Quad* down_quad = &quads[quad_id + QUAD_NUM_LINE];
		u16 down_center = down_quad->tris[0].indices[1];
		vertices_dl[down_center].v.ob[1] -= deep / 2;
	}
	// left
	if (quad_id % QUAD_NUM_LINE > 0) {
		struct Quad* left_quad = &quads[quad_id - 1];
		u16 left_center = left_quad->tris[0].indices[1];
		vertices_dl[left_center].v.ob[1] -= deep / 2;
	}
	// right
	if (quad_id % QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		struct Quad* right_quad = &quads[quad_id + 1];
		u16 right_center = right_quad->tris[0].indices[1];
		vertices_dl[right_center].v.ob[1] -= deep / 2;
	}

	// collision change
	surface_pointer_array[quad_id * 4 + 0]->vertex1[1] -= deep;
	surface_pointer_array[quad_id * 4 + 0]->vertex2[1] -= deep;
	surface_pointer_array[quad_id * 4 + 0]->vertex3[1] -= deep;
	surface_pointer_array[quad_id * 4 + 0]->lowerY -= deep;
	surface_pointer_array[quad_id * 4 + 0]->upperY -= deep;
	surface_pointer_array[quad_id * 4 + 0]->originOffset += deep;

	surface_pointer_array[quad_id * 4 + 1]->vertex1[1] -= deep;
	surface_pointer_array[quad_id * 4 + 1]->vertex2[1] -= deep;
	surface_pointer_array[quad_id * 4 + 1]->vertex3[1] -= deep;
	surface_pointer_array[quad_id * 4 + 1]->lowerY -= deep;
	surface_pointer_array[quad_id * 4 + 1]->upperY -= deep;
	surface_pointer_array[quad_id * 4 + 1]->originOffset += deep;

	surface_pointer_array[quad_id * 4 + 2]->vertex1[1] -= deep;
	surface_pointer_array[quad_id * 4 + 2]->vertex2[1] -= deep;
	surface_pointer_array[quad_id * 4 + 2]->vertex3[1] -= deep;
	surface_pointer_array[quad_id * 4 + 2]->lowerY -= deep;
	surface_pointer_array[quad_id * 4 + 2]->upperY -= deep;
	surface_pointer_array[quad_id * 4 + 2]->originOffset += deep;

	surface_pointer_array[quad_id * 4 + 3]->vertex1[1] -= deep;
	surface_pointer_array[quad_id * 4 + 3]->vertex2[1] -= deep;
	surface_pointer_array[quad_id * 4 + 3]->vertex3[1] -= deep;
	surface_pointer_array[quad_id * 4 + 3]->lowerY -= deep;
	surface_pointer_array[quad_id * 4 + 3]->upperY -= deep;
	surface_pointer_array[quad_id * 4 + 3]->originOffset += deep;

	// up
	if (quad_id / QUAD_NUM_LINE > 0) {
		u16 up_quad = quad_id - QUAD_NUM_LINE;
		// tri0
		surface_pointer_array[up_quad * 4 + 0]->vertex2[1] -= deep / 2;
		surface_pointer_array[up_quad * 4 + 0]->lowerY -= deep / 2;
		surface_recalc_normal(surface_pointer_array[up_quad * 4 + 0]);
		// tri1
		surface_pointer_array[up_quad * 4 + 1]->vertex2[1] -= deep;
		surface_pointer_array[up_quad * 4 + 1]->vertex3[1] -= deep / 2;
		surface_pointer_array[up_quad * 4 + 1]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_quad * 4 + 1]);
		// tri2
		surface_pointer_array[up_quad * 4 + 2]->vertex1[1] -= deep / 2;
		surface_pointer_array[up_quad * 4 + 2]->vertex2[1] -= deep;
		surface_pointer_array[up_quad * 4 + 2]->vertex3[1] -= deep;
		surface_pointer_array[up_quad * 4 + 2]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_quad * 4 + 2]);
		// tri3
		surface_pointer_array[up_quad * 4 + 3]->vertex1[1] -= deep / 2;
		surface_pointer_array[up_quad * 4 + 3]->vertex2[1] -= deep;
		surface_pointer_array[up_quad * 4 + 3]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_quad * 4 + 3]);
	}
	//down
	if (quad_id / QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		u16 down_quad = quad_id + QUAD_NUM_LINE;
		// tri0
		surface_pointer_array[down_quad * 4 + 0]->vertex1[1] -= deep;
		surface_pointer_array[down_quad * 4 + 0]->vertex2[1] -= deep / 2;
		surface_pointer_array[down_quad * 4 + 0]->vertex3[1] -= deep;
		surface_pointer_array[down_quad * 4 + 0]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[down_quad * 4 + 0]);
		// tri1
		surface_pointer_array[down_quad * 4 + 1]->vertex1[1] -= deep;
		surface_pointer_array[down_quad * 4 + 1]->vertex3[1] -= deep / 2;
		surface_pointer_array[down_quad * 4 + 1]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[down_quad * 4 + 1]);
		// tri2
		surface_pointer_array[down_quad * 4 + 2]->vertex1[1] -= deep / 2;
		surface_pointer_array[down_quad * 4 + 2]->lowerY -= deep / 2;
		surface_recalc_normal(surface_pointer_array[down_quad * 4 + 2]);
		// tri3
		surface_pointer_array[down_quad * 4 + 3]->vertex1[1] -= deep / 2;
		surface_pointer_array[down_quad * 4 + 3]->vertex3[1] -= deep;
		surface_pointer_array[down_quad * 4 + 3]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[down_quad * 4 + 3]);
	}
	//left
	if (quad_id % QUAD_NUM_LINE > 0) {
		u16 left_quad = quad_id - 1;
		// tri0
		surface_pointer_array[left_quad * 4 + 0]->vertex2[1] -= deep / 2;
		surface_pointer_array[left_quad * 4 + 0]->vertex3[1] -= deep;
		surface_pointer_array[left_quad * 4 + 0]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[left_quad * 4 + 0]);
		// tri1
		surface_pointer_array[left_quad * 4 + 1]->vertex3[1] -= deep / 2;
		surface_pointer_array[left_quad * 4 + 1]->lowerY -= deep / 2;
		surface_recalc_normal(surface_pointer_array[left_quad * 4 + 1]);
		// tri2
		surface_pointer_array[left_quad * 4 + 2]->vertex1[1] -= deep / 2;
		surface_pointer_array[left_quad * 4 + 2]->vertex3[1] -= deep;
		surface_pointer_array[left_quad * 4 + 2]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[left_quad * 4 + 2]);
		// tri3
		surface_pointer_array[left_quad * 4 + 3]->vertex1[1] -= deep / 2;
		surface_pointer_array[left_quad * 4 + 3]->vertex2[1] -= deep;
		surface_pointer_array[left_quad * 4 + 3]->vertex3[1] -= deep;
		surface_pointer_array[left_quad * 4 + 3]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[left_quad * 4 + 3]);
	}
	//right
	if (quad_id % QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		u16 right_quad = quad_id + 1;
		// tri0
		surface_pointer_array[right_quad * 4 + 0]->vertex1[1] -= deep;
		surface_pointer_array[right_quad * 4 + 0]->vertex2[1] -= deep / 2;
		surface_pointer_array[right_quad * 4 + 0]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[right_quad * 4 + 0]);
		// tri1
		surface_pointer_array[right_quad * 4 + 1]->vertex1[1] -= deep;
		surface_pointer_array[right_quad * 4 + 1]->vertex2[1] -= deep;
		surface_pointer_array[right_quad * 4 + 1]->vertex3[1] -= deep / 2;
		surface_pointer_array[right_quad * 4 + 1]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[right_quad * 4 + 1]);
		// tri2
		surface_pointer_array[right_quad * 4 + 2]->vertex1[1] -= deep / 2;
		surface_pointer_array[right_quad * 4 + 2]->vertex2[1] -= deep;
		surface_pointer_array[right_quad * 4 + 2]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[right_quad * 4 + 2]);
		// tri3
		surface_pointer_array[right_quad * 4 + 3]->vertex1[1] -= deep / 2;
		surface_pointer_array[right_quad * 4 + 3]->lowerY -= deep / 2;
		surface_recalc_normal(surface_pointer_array[right_quad * 4 + 3]);
	}
	// upper left
	if(quad_id / QUAD_NUM_LINE > 0 && quad_id % QUAD_NUM_LINE > 0) {
		u16 up_left_quad = quad_id - QUAD_NUM_LINE - 1;
		// tri2
		surface_pointer_array[up_left_quad * 4 + 2]->vertex3[1] -= deep;
		surface_pointer_array[up_left_quad * 4 + 2]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_left_quad * 4 + 2]);
		// tri3
		surface_pointer_array[up_left_quad * 4 + 3]->vertex2[1] -= deep;
		surface_pointer_array[up_left_quad * 4 + 3]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_left_quad * 4 + 3]);
	}
	// upper right
	if(quad_id / QUAD_NUM_LINE > 0 && quad_id % QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		u16 up_right_quad = quad_id - QUAD_NUM_LINE + 1;
		// tri1
		surface_pointer_array[up_right_quad * 4 + 1]->vertex2[1] -= deep;
		surface_pointer_array[up_right_quad * 4 + 1]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_right_quad * 4 + 1]);
		// tri2
		surface_pointer_array[up_right_quad * 4 + 2]->vertex2[1] -= deep;
		surface_pointer_array[up_right_quad * 4 + 2]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[up_right_quad * 4 + 2]);
	}
	// lower left
	if(quad_id / QUAD_NUM_LINE < QUAD_NUM_LINE - 1 && quad_id % QUAD_NUM_LINE > 0) {
		u16 lower_left_quad = quad_id + QUAD_NUM_LINE - 1;
		// tri0
		surface_pointer_array[lower_left_quad * 4 + 0]->vertex3[1] -= deep;
		surface_pointer_array[lower_left_quad * 4 + 0]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[lower_left_quad * 4 + 0]);
		// tri3
		surface_pointer_array[lower_left_quad * 4 + 3]->vertex3[1] -= deep;
		surface_pointer_array[lower_left_quad * 4 + 3]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[lower_left_quad * 4 + 3]);
	}
	// lower right
	if(quad_id / QUAD_NUM_LINE < QUAD_NUM_LINE - 1 && quad_id % QUAD_NUM_LINE < QUAD_NUM_LINE - 1) {
		u16 lower_right_quad = quad_id + QUAD_NUM_LINE + 1;
		// tri0
		surface_pointer_array[lower_right_quad * 4 + 0]->vertex1[1] -= deep;
		surface_pointer_array[lower_right_quad * 4 + 0]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[lower_right_quad * 4 + 0]);
		// tri1
		surface_pointer_array[lower_right_quad * 4 + 1]->vertex1[1] -= deep;
		surface_pointer_array[lower_right_quad * 4 + 1]->lowerY -= deep;
		surface_recalc_normal(surface_pointer_array[lower_right_quad * 4 + 1]);
	}
}