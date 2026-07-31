//
// Copyright(C) 1984-2000 David L. Clark
// Copyright(C) 2001-2005 Simon Howard
//
// You can redistribute and/or modify this program under the terms of the
// GNU General Public License version 2 as published by the Free Software
// Foundation, or any later version. This program is distributed WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE.
//
//
//        swcollsn -      SW collision resolution
//

#include "swcollsn.h"
#include "sw.h"
#include "swend.h"
#include "swinit.h"
#include "swmain.h"
#include "swmove.h"
#include "swobject.h"
#include "swsound.h"
#include "swsplat.h"

static int ComputeValour(OBJECTS *ob);
static void tstcrash(OBJECTS *obp);

#define MAX_COLLISION_EVENTS 4096

static OBJECTS *killed[MAX_COLLISION_EVENTS];
static OBJECTS *killer[MAX_COLLISION_EVENTS];
static int killptr;

static bool CollisionAlreadyQueued(OBJECTS *ob1, OBJECTS *ob2)
{
	int i;

	for (i = 0; i < killptr; ++i) {
		if ((killed[i] == ob1 && killer[i] == ob2) ||
		    (killed[i] == ob2 && killer[i] == ob1)) {
			return true;
		}
	}
	return false;
}

static bool CollisionCanMatter(OBJECTS *ob1, OBJECTS *ob2)
{
	if (ob1->ob_type == EXPLOSION || ob2->ob_type == EXPLOSION) {
		return false;
	}
	if (ob1->ob_type == WALKER && ob2->ob_type == WALKER) {
		return false;
	}
	if ((ob1->ob_type == WALKER && ob2->ob_type == CAR) ||
	    (ob2->ob_type == WALKER && ob1->ob_type == CAR) ||
	    (ob1->ob_type == CAR && ob2->ob_type == CAR)) {
		return false;
	}
	if (ob1->ob_type == TARGET && ob2->ob_type == TARGET) {
		return false;
	}
	if ((ob1->ob_type == TARGET &&
	     (ob2->ob_type == WALKER || ob2->ob_type == CAR)) ||
	    (ob2->ob_type == TARGET &&
	     (ob1->ob_type == WALKER || ob1->ob_type == CAR))) {
		return false;
	}
	if (ob1->ob_type == STARBURST || ob2->ob_type == STARBURST) {
		OBJECTS *other =
		    ob1->ob_type == STARBURST ? ob2 : ob1;
		return other->ob_type == WALKER ||
		       other->ob_type == MISSILE ||
		       other->ob_type == BOMB ||
		       other->ob_type == GRENADE;
	}
	return true;
}

static void QueueSweptWalkerHit(OBJECTS *shot)
{
	OBJECTS *walker, *best = NULL;
	int old_x = shot->ob_x - shot->ob_dx;
	int min_x = imin(old_x, shot->ob_x);
	int max_x = imax(old_x, shot->ob_x);
	int best_distance = 999999;

	for (walker = objtop; walker != NULL; walker = walker->ob_next) {
		int distance;
		int walker_ymin;

		if (walker->ob_type != WALKER ||
		    walker->ob_state != WALKER_ABANDONED ||
		    walker == shot->ob_owner) {
			continue;
		}
		if (max_x < walker->ob_x ||
		    min_x > walker->ob_x + walker->ob_symbol->w - 1) {
			continue;
		}
		walker_ymin = walker->ob_y - walker->ob_symbol->h + 1;
		if (!in_range(walker_ymin, shot->ob_y, walker->ob_y)) {
			continue;
		}
		distance = abs(walker->ob_x - old_x);
		if (distance < best_distance) {
			best = walker;
			best_distance = distance;
		}
	}

	if (best != NULL && !CollisionAlreadyQueued(shot, best) &&
	    killptr < MAX_COLLISION_EVENTS - 1) {
		killed[killptr] = shot;
		killer[killptr++] = best;
		killed[killptr] = best;
		killer[killptr++] = shot;
	}
}

static int collsdx[MAX_PLYR];
static int collsdy[MAX_PLYR];
static OBJECTS *collsno[MAX_PLYR];
static int collptr;
static int collxadj, collyadj;

// #define COLL_DEBUG

bool CollisionTest(OBJECTS *ob1, OBJECTS *ob2)
{
	int x, y;
	int x1, y1, x2, y2;
	int w, h;
	uint8_t *data1, *data2;

	if ((ob1->ob_type == PLANE && ob1->ob_state >= FINISHED) ||
	    (ob2->ob_type == PLANE && ob2->ob_state >= FINISHED) ||
	    (ob1->ob_type == PLANE && ob1->ob_control_delay > 0) ||
	    (ob2->ob_type == PLANE && ob2->ob_control_delay > 0) ||
	    (ob1->ob_type == EXPLOSION && ob2->ob_type == EXPLOSION)) {
		return false;
	}

	// (x1, y1) are the coords of the area we are testing in ob1
	// (x2, y2) are the coords of the area in ob2
	// (w, h) is the size of the area

	// x:
	if (ob1->ob_x < ob2->ob_x) {
		x1 = ob2->ob_x - ob1->ob_x;
		x2 = 0;
		w = clamp_max(ob1->ob_symbol->w - x1, ob2->ob_symbol->w);
	} else {
		x1 = 0;
		x2 = ob1->ob_x - ob2->ob_x;
		w = clamp_max(ob2->ob_symbol->w - x2, ob1->ob_symbol->w);
	}

	// no intersection?
	if (w <= 0) {
		return false;
	}

	// y:
	if (ob1->ob_y < ob2->ob_y) {
		y1 = 0;
		y2 = ob2->ob_y - ob1->ob_y;
		h = clamp_max(ob2->ob_symbol->h - y2, ob1->ob_symbol->h);
	} else {
		y1 = ob1->ob_y - ob2->ob_y;
		y2 = 0;
		h = clamp_max(ob1->ob_symbol->h - y1, ob2->ob_symbol->h);
	}

	// no intersection?
	if (h <= 0) {
		return false;
	}

#ifdef COLL_DEBUG
	fprintf(stderr, "collision test: (%i, %i) at (%i, %i)/(%i, %i)\n", w, h,
	        x1, y1, x2, y2);

	fprintf(stderr, "info: (%i, %i)/(%i, %i)  (%i, %i)/(%i, %i)\n",
	        ob1->ob_x, ob1->ob_y, ob1->ob_symbol->w, ob1->ob_symbol->h,
	        ob2->ob_x, ob2->ob_y, ob2->ob_symbol->w, ob2->ob_symbol->h);
#endif

	data1 = ob1->ob_symbol->data + ob1->ob_symbol->w * y1 + x1;
	data2 = ob2->ob_symbol->data + ob2->ob_symbol->w * y2 + x2;

	for (y = 0; y < h; ++y) {
		uint8_t *d1 = data1, *d2 = data2;

		for (x = 0; x < w; ++x) {
			if (*d1 && *d2) {
				return true;
			}

			++d1;
			++d2;
		}

		data1 += ob1->ob_symbol->w;
		data2 += ob2->ob_symbol->w;
	}

	return false;
}

/* Determine the object that receives a score if 'ob' is destroyed, and whether
 * the score should be subtracted rather than added (iff reverse nonzero) */
static OBJECTS *GetScoreObject(OBJECTS *ob, int *reverse)
{
	OBJECTS *retval;

	if (playmode != PLAYMODE_ASYNCH) {
		retval = planes[0];
		*reverse = ob->ob_faction == FACTION_PLAYER1 ||
		           ob->ob_faction == FACTION_NONE;
	} else {
		// TODO: Support more than two factions.
		retval = planes[ob->ob_faction == FACTION_PLAYER1 ? 1 : 0];
		// TODO: This always penalizes player 1 when animals are
		// killed, even if it was player 2 that killed the animal.
		*reverse = ob->ob_faction == FACTION_NONE;
	}

	return retval;
}

static void scoretarg(OBJECTS *obp, int score)
{
	OBJECTS *ob;
	int reverse_score;

	ob = GetScoreObject(obp, &reverse_score);
	if (reverse_score) {
		ob->ob_score.score -= score;
	} else {
		ob->ob_score.score += score;
	}
}

static bool IsHumanFaction(faction_t f)
{
	int i;

	for (i = 0; i < MAX_PLYR; i++) {
		if (planes[i] != NULL && planes[i]->ob_faction == f &&
		    planes[i]->ob_movef == moveplyr) {
			return true;
		}
	}

	return false;
}

// CheckForWinner examines the numtarg[] array to see if there is any
// faction that has destroyed all targets owned by all other factions.
static faction_t CheckForWinner(void)
{
	faction_t i, j;

	for (i = FACTION_PLAYER1; i < NUM_FACTIONS; i++) {
		// Computer planes can't win.
		if (playmode != PLAYMODE_BATTLEFIELD &&
		    !IsHumanFaction(i)) {
			continue;
		}

		for (j = FACTION_PLAYER1; j < NUM_FACTIONS; j++) {
			if (i != j && numtarg[j] > 0) {
				break;
			}
		}
		if (j >= NUM_FACTIONS) {
			return i;
		}
	}

	return FACTION_NONE;
}

static void TargetDestroyed(OBJECTS *ob, obtype_t type)
{
	faction_t winner;
	int reverse;
	OBJECTS *so = GetScoreObject(ob, &reverse);

	if (!reverse &&
	    (type == BOMB || type == GRENADE || type == SHOT ||
	     type == MISSILE || type == PLANE)) {
		so->ob_flightscore.killscore += 4;
		so->ob_flightscore.valour += 3 * ComputeValour(ob);
	}

	scoretarg(ob, ob->ob_orient == TARGET_OIL_TANK ? 200 : 100);

	// Battlefield tanks use TARGET sprites and collision rules, but they
	// are units rather than territory.
	if (playmode == PLAYMODE_BATTLEFIELD &&
	    ob->ob_orient == TARGET_TANK &&
	    ob->ob_original_ob->orient != TARGET_TANK) {
		return;
	}
	--numtarg[ob->ob_faction];
	winner = CheckForWinner();
	if (winner != FACTION_NONE) {
		endgame(winner);
	}
}

static bool scorepenalty(obtype_t ttype, OBJECTS *ob, int score)
{
	OBJECTS *obt;

	obt = ob;
	if (ttype == SHOT || ttype == BOMB || ttype == GRENADE ||
	    ttype == MISSILE ||
	    (ttype == PLANE &&
	     (obt->ob_state == FLYING || obt->ob_state == WOUNDED ||
	      (obt->ob_state == FALLING && obt->ob_hitcount == FALLCOUNT)) &&
	     !obt->ob_athome)) {
		scoretarg(obt, score);
		return true;
	}
	return false;
}

static const int crtdepth[8] = {1, 2, 2, 3, 3, 2, 2, 1};

static void crater(OBJECTS *ob)
{
	int i, x, y, ymin, ymax;
	int xmin, xmax;

	xmin = ob->ob_x + (ob->ob_symbol->w - 8) / 2;
	xmax = xmin + 7;

	for (x = xmin, i = 0; x <= xmax; ++x, ++i) {
		if (!in_range(0, x, currgame->gm_max_x - 1)) {
			continue;
		}
		ymax = ground[x];
		ymin = ymax - crtdepth[i] + 1;
		y = clamp_min(20, currgame->gm_ground[x] - 20);
		if (ymin <= y) {
			ymin = y + 1;
		}
		ground[x] = ymin - 1;
	}
}

/* Determine whether the parameter is a shot and hasn't moved very much yet;
** Used to avoid having planes hitting themselves */
static bool IsYoungShot(OBJECTS *ob)
{
	return ob && ob->ob_type == SHOT && ob->ob_life >= BULLIFE - 1;
}

static void PowerupCollected(OBJECTS *powerup, OBJECTS *plane)
{
	powerup->ob_state = FINISHED;

	switch (powerup->ob_orient) {
	case POWERUP_AMMO:
		plane->ob_rounds =
		    clamp_max(plane->ob_rounds + (MAXROUNDS / 2), MAXROUNDS);
		break;
	case POWERUP_AMMO_BIG:
		plane->ob_rounds = MAXROUNDS;
		break;
	case POWERUP_FUEL:
		plane->ob_life =
		    clamp_max(plane->ob_life + (MAXFUEL / 2), MAXFUEL);
		break;
	case POWERUP_FUEL_BIG:
		plane->ob_life = MAXFUEL;
		break;
	case POWERUP_BOMB:
		plane->ob_bombs =
		    clamp_max(plane->ob_bombs + (MAXBOMBS / 2), MAXBOMBS);
		break;
	case POWERUP_BOMB_BIG:
		plane->ob_bombs = MAXBOMBS;
		break;
	default:
		break;
	}
}

static void swkill(OBJECTS *ob1, OBJECTS *ob2)
{
	OBJECTS *ob, *obt;
	int i;
	obtype_t ttype;
	obstate_t state;

	ob = ob1;
	obt = ob2;
	ttype = obt ? obt->ob_type : GROUND;
	if ((ttype == BIRD || ttype == FLOCK) && ob->ob_type != PLANE) {
		return;
	}

	switch (ob->ob_type) {

	case BOMB:
	case GRENADE:
	case MISSILE:
		initexpl(ob, 0);
		ob->ob_life = -1;
		if (!obt) {
			crater(ob);
		}
		stopsound(ob);
		return;

	case SHOT:
	case GROUND_SHOT:
		if (obt == ob->ob_owner) {
			return;
		}
		/* cr 2005-04-28: Don't stop the shot if it just
		 * launched from its presumed originator */
		if (!(obt && obt->ob_type == PLANE && IsYoungShot(ob))) {
			ob->ob_life = 1;
		}
		return;

	case STARBURST:
		if (ttype == MISSILE || ttype == BOMB || !obt) {
			ob->ob_life = 1;
		}
		return;

	case EXPLOSION:
		if (!obt) {
			ob->ob_life = 1;
			stopsound(ob);
		}
		return;

	case BALLOON:
		if (ob->ob_state != FLYING) {
			return;
		}
		if (ttype != PLANE && ttype != SHOT && ttype != BOMB) {
			return;
		}

		ob->ob_state = FINISHED;
		ob->ob_onmap = false;
		ob->ob_life = -1;
		initexpl(ob, 0);

		TargetDestroyed(ob, ttype);
		return;

	case POWERUP:
	case TARGET:
		if (ob->ob_state != STANDING) {
			return;
		}
		if (ttype == PLANE && obt &&
		    obt->ob_faction == ob->ob_faction) {
			return;
		}
		// The pilot walks past scenery; touching it must not destroy it.
		if (ttype == WALKER || ttype == CAR) {
			return;
		}
		// Mobile tanks pass scenery without crushing it.
		if (ttype == TARGET) {
			return;
		}
		if (ttype == GROUND_SHOT &&
		    (playmode != PLAYMODE_BATTLEFIELD || !obt ||
		     obt->ob_owner->ob_faction == ob->ob_faction)) {
			return;
		}
		if (ttype == EXPLOSION || ttype == STARBURST) {
			return;
		}

		if (ttype == SHOT ||
		    (playmode == PLAYMODE_BATTLEFIELD &&
		     ttype == GROUND_SHOT)) {
			ob->ob_hitcount += TARGHITCOUNT;
			if (ob->ob_hitcount <= (TARGHITCOUNT * (gamenum + 1))) {
				return;
			}
		}

		ob->ob_state = FINISHED;
		ob->ob_onmap = false;

		if (ob->ob_type == POWERUP && obt->ob_movef == moveplyr &&
		    obt->ob_type == PLANE && PlaneIsFlying(obt->ob_state)) {
			PowerupCollected(ob, obt);
		} else {
			initexpl(ob, 0);
			if (ob->ob_type == TARGET) {
				TargetDestroyed(ob, ttype);
			}
		}
		return;

	case PLANE:
		state = ob->ob_state;

		if (obt && obt->ob_faction == ob->ob_faction &&
		    (ttype == WALKER || ttype == TARGET || ttype == CAR ||
		     (playmode == PLAYMODE_BATTLEFIELD &&
		      ttype == PLANE))) {
			return;
		}
		if (ttype == WALKER && obt->ob_movef != move_walker) {
			return;
		}
		if (ttype == GROUND_SHOT && obt->ob_owner == ob) {
			return;
		}
		/* cr 2005-04-28: Avoid having planes hit themselves */
		if (IsYoungShot(obt)) {
			return;
		}

		if (state == CRASHED) {
			return;
		}

		if (ob->ob_endsts == WINNER) {
			return;
		}

		// Plane flying into powerup does not cause a crash:
		if (ttype == POWERUP) {
			return;
		}

		if (ttype == STARBURST || (ttype == BIRD && ob->ob_athome)) {
			return;
		}

		if (!obt) {
			if (state == FALLING) {
				stopsound(ob);
				initexpl(ob, 1);
				crater(ob);
			} else if (state < FINISHED) {
				scorepln(ob, ttype);
				initexpl(ob, 1);
				crater(ob);
			}

			crashpln(ob);
			return;
		}

		if (state >= FINISHED) {
			return;
		}

		if (state == FALLING) {
			if (ob == consoleplayer) {
				if (ttype == SHOT) {
					swwindshot();
				} else if (ttype == OX) {
					swsplatox();
				} else if (ttype == BIRD || ttype == FLOCK) {
					swsplatbird();
				}
			}
			return;
		}

		if (ttype == SHOT || ttype == GROUND_SHOT || ttype == BIRD ||
		    ttype == OX ||
		    ttype == FLOCK) {
			if (ob == consoleplayer) {
				if (ttype == SHOT || ttype == GROUND_SHOT) {
					swwindshot();
				} else if (ttype == OX) {
					swsplatox();
				} else {
					swsplatbird();
				}
			}

			// Wounding is only allowed if the player is actually
			// in the air. If an enemy plane scores a hit on a
			// player sitting on the runway, that is a successful
			// raid; otherwise, the damage would be repaired
			// immediately.
			if (conf_wounded && ttype != GROUND_SHOT &&
			    !ob->ob_athome) {
				if (ttype == SHOT) {
					ob->ob_flightscore.combatwound = true;
				}
				if (state == FLYING) {
					ob->ob_state = WOUNDED;
					return;
				}
				if (state == STALLED) {
					ob->ob_state = WOUNDSTALL;
					return;
				}
			}
		} else {
			initexpl(ob, 1);
			if (ttype == PLANE) {
				collxadj = -collxadj;
				collyadj = -collyadj;
				collsdx[collptr] =
				    ((ob->ob_dx + obt->ob_dx) >> 1) + collxadj;
				collsdy[collptr] =
				    ((ob->ob_dy + obt->ob_dy) >> 1) + collyadj;
				collsno[collptr++] = ob;
			}
		}

		hitpln(ob);
		scorepln(ob, ttype);
		return;

	case BIRD:
		ob->ob_life = scorepenalty(ttype, obt, 25) ? -1 : -2;
		return;

	case FLOCK:
		if (ttype != FLOCK && ttype != BIRD && ob->ob_state == FLYING) {
			for (i = 0; i < 8; ++i)
				initbird(ob, i);
			ob->ob_life = -1;
			ob->ob_state = FINISHED;
		}
		return;

	case OX:
		if (ob->ob_state != STANDING) {
			return;
		}
		if (ttype == EXPLOSION || ttype == STARBURST) {
			return;
		}
		scorepenalty(ttype, obt, 200);
		ob->ob_state = FINISHED;
		return;

	case WALKER:
		if (ob->ob_state != WALKER_ABANDONED) {
			return;
		}
		if (ttype == PLANE && obt &&
		    obt->ob_faction == ob->ob_faction) {
			return;
		}
		if (ttype == GROUND_SHOT &&
		    (obt->ob_owner == ob ||
		     obt->ob_owner->ob_faction == ob->ob_faction)) {
			return;
		}
		// Enemy infantry can be run down by a plane. The player's pilot
		// is only vulnerable to an aircraft that is actually falling.
		if (ttype == PLANE && obt && ob->ob_movef == move_walker &&
		    obt->ob_state != FALLING) {
			return;
		}
		if (ttype != SHOT && ttype != GROUND_SHOT && ttype != BOMB &&
		    ttype != GRENADE && ttype != MISSILE &&
		    ttype != STARBURST) {
			if (ttype != PLANE || !obt) {
				return;
			}
		}
		if (ob->ob_athome) {
			return;
		}
		ob->ob_hitcount--;
		if (ob->ob_hitcount <= 0) {
			ob->ob_hitcount = 0;
			ob->ob_state = FINISHED;
			initblood(ob);
		}
		ob->ob_dy = 2;
		return;

	case GRENADE_PICKUP:
		if (ob->ob_state == STANDING && ttype == WALKER && obt &&
		    obt->ob_movef == move_walker &&
		    obt->ob_state == WALKER_ABANDONED) {
			obt->ob_bombs += ob->ob_bombs;
			ob->ob_state = FINISHED;
			ob->ob_onmap = false;
		}
		return;

	case CAR:
	{
		OBJECTS *vehicle;

		// Cars pass through buildings without damaging them. Rifle
		// bullets cannot penetrate the vehicle, but
		// aircraft gunfire and a direct aircraft collision can.
		if (ttype == TARGET || ttype == POWERUP ||
		    ttype == GROUND_SHOT ||
		    ttype == WALKER || ttype == CAR) {
			return;
		}
		if (ttype == PLANE || ttype == SHOT) {
			vehicle = eject_driver_and_destroy_car(ob);
			initexpl(vehicle, 0);
			TargetDestroyed(vehicle, ttype);
		} else if (ttype == BOMB || ttype == GRENADE ||
		           ttype == MISSILE) {
			kill_car_driver(ob);
		}
		return;
	}

	default:
		return;
	}
}

void swcollsn(void)
{
	OBJECTS *ob, *obp, **obkd, **obkr;
	int xmax, ymin, ymax, i;
	obtype_t otype;

	collptr = killptr = 0;
	collxadj = 2;
	collyadj = 1;
	if (countmove & 1) {
		collxadj = -collxadj;
		collyadj = -collyadj;
	}

	for (ob = topobj.ob_xnext; ob != &botobj; ob = ob->ob_xnext) {
		xmax = ob->ob_x + ob->ob_symbol->w - 1;
		ymax = ob->ob_y;
		ymin = ymax - ob->ob_symbol->h + 1;

		for (obp = ob->ob_xnext; obp != &botobj && obp->ob_x <= xmax;
		     obp = obp->ob_xnext) {

			if (obp->ob_y >= ymin &&
			    (obp->ob_y - obp->ob_symbol->h + 1) <= ymax &&
			    CollisionCanMatter(ob, obp) &&
			    CollisionTest(ob, obp) &&
			    killptr < MAX_COLLISION_EVENTS - 1) {
				killed[killptr] = ob;
				killer[killptr] = obp;
				++killptr;
				killed[killptr] = obp;
				killer[killptr] = ob;
				++killptr;
			}
		}

		otype = ob->ob_type;

		if ((otype == PLANE &&
		     !(playmode == PLAYMODE_BATTLEFIELD &&
		       ob->ob_athome && ob->ob_control_delay > 0) &&
		     ob->ob_state != FINISHED &&
		     ob->ob_state != WAITING &&
		     ob->ob_y <
		         (ground[clamp_range(0, ob->ob_x + 8,
		                             currgame->gm_max_x - 1)] +
		          24)) ||
		    ((otype == BOMB || otype == GRENADE || otype == MISSILE) &&
		     ob->ob_y <
		         (ground[clamp_range(0, ob->ob_x + 4,
		                             currgame->gm_max_x - 1)] +
		          12))) {
			tstcrash(ob);
		}
	}

	for (ob = objtop; ob != NULL; ob = ob->ob_next) {
		if (ob->ob_type == SHOT || ob->ob_type == GROUND_SHOT) {
			QueueSweptWalkerHit(ob);
		}
	}

	obkd = killed;
	obkr = killer;
	for (i = 0; i < killptr; ++i, ++obkd, ++obkr)
		swkill(*obkd, *obkr);

	obkd = collsno;
	for (i = 0; i < collptr; ++i, ++obkd) {
		ob = *obkd;
		ob->ob_dx = collsdx[i];
		ob->ob_dy = collsdy[i];
	}
}

static void tstcrash(OBJECTS *obp)
{
	sopsym_t *sym = obp->ob_symbol;
	int x, y;

	for (x = 0; x < sym->w; ++x) {
		int ground_x =
		    clamp_range(0, x + obp->ob_x, currgame->gm_max_x - 1);
		y = obp->ob_y - ground[ground_x];

		// out of range?
		if (y >= sym->h) {
			continue;
		}

		// check for collision at this point
		if (y < 0 || sym->data[y * sym->w + x]) {

			// collision!
			if (killptr < MAX_COLLISION_EVENTS) {
				killed[killptr] = obp;
				killer[killptr] = NULL;
				++killptr;
			}

			return;
		}
	}
}

static int ComputeValour(OBJECTS *ob)
{
	int reverse;
	OBJECTS *so = GetScoreObject(ob, &reverse);
	int x_home;
	int distance;
	int valour = 0;
	int fuelfraction;

	if (reverse) {
		return 0;
	}

	x_home = so->ob_original_ob->x;

	distance = abs(x_home - so->ob_x);

	if (distance < 500) {
		valour = 0;
	} else {
		valour = (distance - 500) / 350;
	}

	if (ob->ob_life > 0) {
		fuelfraction = (MAXFUEL / ob->ob_life);
	} else {
		fuelfraction = 1000;
	}

	if (fuelfraction > 9) {
		valour++;
	}

	if (PlaneIsWounded(so->ob_state)) {
		valour = (valour + 1) * 3;
	} else {
		valour *= 2;
	}

	return valour;
}

void scorepln(OBJECTS *ob, obtype_t type)
{
	int had_taken_off = ob->ob_life < (MAXFUEL - (MAXFUEL / 100));

	scoretarg(ob, 50);

	if (type == BOMB || type == GRENADE || type == SHOT ||
	    type == MISSILE || type == PLANE) {
		int reverse;
		OBJECTS *scobj = GetScoreObject(ob, &reverse);
		if (!reverse) {
			if (had_taken_off) {
				if (type != PLANE) {
					scobj->ob_flightscore.planekills++;
				}
				scobj->ob_flightscore.valour +=
				    4 * (2 + ComputeValour(ob));
			}

			scobj->ob_flightscore.killscore += 3;
		}
	}
}

//
// 2003-02-14: Code was checked into version control; no further entries
// will be added to this log.
//
// sdh 14/2/2003: change license header to GPL
// sdh 28/07/2002: removed old collision detection code
// sdh 28/06/2002: new collision detection code: look at the sprite data
//                 rather than drawing to the screen. old code is still there
//                 under a #define but will eventually be removed.
// sdh 27/06/2002: move to new sopsym_t for symbols
// sdh 28/10/2001: option to disable wounded planes
// sdh 24/10/2001: fix score display, fix auxdisp buffer
// sdh 21/10/2001: use new obtype_t and obstate_t
// sdh 21/10/2001: rearranged file headers, added cvs tags
// sdh 21/10/2001: reformatted with indent, adjusted some code by
//                 hand to make more readable
// sdh 19/10/2001: removed externs, these are now in headers
// sdh 18/10/2001: converted all functions to ANSI-style arguments
//
// 87-04-05        Missile and starburst support
// 87-03-31        Missiles.
// 87-03-13        Splatted bird symbol.
// 87-03-12        More than 1 bullet to kill target.
// 87-03-12        Wounded airplanes.
// 87-03-11        No explosion on bird-plane collision
// 87-03-09        Microsoft compiler.
// 84-10-31        Atari
// 84-06-12        PCjr Speed-up
// 84-02-02        Development
//
