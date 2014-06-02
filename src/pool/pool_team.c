/**
 * @file src/pool/pool_team.c
 *
 * %Team pool routines.
 */

#include <assert.h>
#include <string.h>

#include "pool_team.h"

#include "pool.h"
#include "../team.h"

enum {
	TEAM_INDEX_MAX = 16
};

/** variable_35EE. */
static Team g_teamArray[TEAM_INDEX_MAX];

/** variable_85DC. */
static Team *g_teamFindArray[TEAM_INDEX_MAX];

/** variable_35F2. */
static uint16 g_teamFindCount;

/**
 * @brief   Get the Team from the pool with the indicated index.
 * @details f__104B_0354_0023_5A6D.
 */
Team *
Team_Get_ByIndex(uint16 index)
{
	assert(index < TEAM_INDEX_MAX);
	return &g_teamArray[index];
}

/**
 * @brief   Start finding Teams in g_teamFindArray.
 * @details f__104B_00C2_0030_20A6 and f__104B_00D5_001D_2B68.
 *          Removed global find struct for when find=NULL.
 */
Team *
Team_FindFirst(PoolFindStruct *find, enum HouseType houseID)
{
	assert(houseID < HOUSE_MAX || houseID == HOUSE_INVALID);

	find->houseID = (houseID < HOUSE_MAX) ? houseID : HOUSE_INVALID;
	find->type    = 0xFFFF;
	find->index   = 0xFFFF;

	return Team_FindNext(find);
}

/**
 * @brief   Continue finding Teams in g_teamFindArray.
 * @details 104B_00F8_002E_3820 and f__104B_0110_0016_6D19.
 *          Removed global find struct for when find=NULL.
 */
Team *
Team_FindNext(PoolFindStruct *find)
{
	if (find->index >= g_teamFindCount && find->index != 0xFFFF)
		return NULL;

	/* First, go to the next index. */
	find->index++;

	for (; find->index < g_teamFindCount; find->index++) {
		Team *t = g_teamFindArray[find->index];

		if (t == NULL)
			continue;

		if (find->houseID == HOUSE_INVALID || find->houseID == t->houseID)
			return t;
	}

	return NULL;
}

/**
 * @brief   Initialise the Team array.
 * @details f__104B_005D_001C_39F6.
 */
void
Team_Init(void)
{
	memset(g_teamArray, 0, sizeof(g_teamArray));
	memset(g_teamFindArray, 0, sizeof(g_teamFindArray));
	g_teamFindCount = 0;

	/* ENHANCEMENT -- Ensure the index is always valid. */
	for (unsigned int i = 0; i < TEAM_INDEX_MAX; i++) {
		g_teamArray[i].index = i;
	}
}

/**
 * @brief   Recount all Teams, rebuilding g_teamFindArray.
 * @details f__104B_0006_0011_631B.
 */
void
Team_Recount(void)
{
	g_teamFindCount = 0;

	for (unsigned int index = 0; index < TEAM_INDEX_MAX; index++) {
		Team *t = Team_Get_ByIndex(index);

		if (t->flags.used) {
			g_teamFindArray[g_teamFindCount] = t;
			g_teamFindCount++;
		}
	}
}

/**
 * @brief   Allocate a Team.
 * @details f__104B_0171_0020_7F19.
 */
Team *
Team_Allocate(uint16 index)
{
	Team *t = NULL;

	/* Find the Team. */
	if (index < TEAM_INDEX_MAX) {
		t = Team_Get_ByIndex(index);
		if (t->flags.used)
			return NULL;
	}
	else {
		assert(index == TEAM_INDEX_INVALID);

		/* Find the first unused index. */
		for (index = 0; index < TEAM_INDEX_MAX; index++) {
			t = Team_Get_ByIndex(index);
			if (!t->flags.used)
				break;
		}
		if (index == TEAM_INDEX_MAX)
			return NULL;
	}
	assert(t != NULL);

	/* Initialise the Team. */
	memset(t, 0, sizeof(Team));
	t->index      = index;
	t->flags.used = true;

	g_teamFindArray[g_teamFindCount] = t;
	g_teamFindCount++;

	return t;
}

/**
 * @brief   Free a Team.
 * @details Introduced.
 */
#if 0
void
Team_Free(Team *t)
{
	unsigned int i;

	memset(&t->flags, 0, sizeof(t->flags));

	/* Find the Team to remove. */
	for (i = 0; i < g_teamFindCount; i++) {
		if (g_teamFindArray[i] == t)
			break;
	}

	/* We should always find an entry. */
	assert(i < g_teamFindCount);

	g_teamFindCount--;

	/* If needed, close the gap. */
	if (i < g_teamFindCount) {
		memmove(&g_teamFindArray[i], &g_teamFindArray[i + 1],
				(g_teamFindCount - i) * sizeof(g_teamFindArray[0]));
	}
}
#endif