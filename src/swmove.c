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
//        swmove   -      SW move all objects and players
//

#include <assert.h>

#include "sw.h"
#include "swauto.h"
#include "swcollsn.h"
#include "swend.h"
#include "swinit.h"
#include "swmain.h"
#include "swmove.h"
#include "swobject.h"
#include "swsound.h"
#include "swsplat.h"
#include "swsymbol.h"
#include "swtitle.h"

// If the player manages to keep the plane in the air this long, they
// have got the hang of the controls and don't need any more help.
#define SUCCESSFUL_FLIGHT_TIME (8 /* seconds */ * FPS)

static bool movepln(OBJECTS *ob);
static void interpret(OBJECTS *ob, int key);

static bool quit;
static int last_ground_time = 0;
bool successful_flight = false;
static void clear_owned_ordnance(OBJECTS *owner);
static bool move_hidden_car(OBJECTS *ob);
static void update_battlefield(void);
static int tank_ground_y(OBJECTS *tank);
static bool move_battlefield_soldier(OBJECTS *soldier);

#define GROUND_PLAYER_PLANE_NOTICE_RANGE 240

void swmove(void)
{
	OBJECTS *ob, *obn;

	if (deltop) {
		delbot->ob_next = objfree;
		objfree = deltop;
		deltop = delbot = NULL;
	}

	++dispcnt;

	if (dispcnt >= keydelay) {
		dispcnt = 0;
	}

	ob = objtop;
	while (ob) {
		obn = ob->ob_next;
		ob->ob_drwflg = (*ob->ob_movef)(ob);
		ob = obn;
	}

	++countmove;
	if (playmode == PLAYMODE_BATTLEFIELD) {
		update_battlefield();
	}
	if (consoleplayer->ob_athome) {
		last_ground_time = countmove;
	} else if (countmove - last_ground_time > SUCCESSFUL_FLIGHT_TIME) {
		successful_flight = true;
	}
}

static void nearpln(OBJECTS *ob)
{
	OBJECTS *obt, *obc;
	int obx;

	obx = ob->ob_x;

	for (obt = objtop; obt != NULL; obt = obt->ob_next) {
		// TODO: Planes currently target any plane of a different
		// faction. In the future it would be nice to support
		// alliances between factions.
		if (obt->ob_type != PLANE ||
		    obt->ob_faction == ob->ob_faction) {
			continue;
		}

		if (obt->ob_movef == movecomp) {
			if (playmode != PLAYMODE_COMPUTER ||
			    in_range(obt->ob_original_ob->territory_l, obx,
			             obt->ob_original_ob->territory_r)) {
				obc = obt->ob_target;
				if (!obc || abs(obx - obt->ob_x) <
				                abs(obc->ob_x - obt->ob_x)) {
					obt->ob_target = ob;
				}
			}
		}
	}
}

static void topup(int *counter, int max)
{
	if (*counter == max) {
		return;
	}
	if (max < 20) {
		if (!(countmove % 20)) {
			++*counter;
		}
	} else {
		*counter += max / 100;
	}
	*counter = clamp_max(*counter, max);
}

static void refuel(OBJECTS *ob)
{
	// sdh 26/10/2001: top up stuff, if anything happens update
	// the gauges (now a single function)
	topup(&ob->ob_life, MAXFUEL);
	topup(&ob->ob_rounds, MAXROUNDS);
	topup(&ob->ob_bombs, MAXBOMBS);
	topup(&ob->ob_missiles, MAXMISSILES);
	topup(&ob->ob_bursts, MAXBURSTS);
}

static int symangle(OBJECTS *ob)
{
	int dx, dy;

	dx = ob->ob_dx;
	dy = ob->ob_dy;
	if (dx == 0) {
		if (dy < 0) {
			return 6;
		} else if (dy > 0) {
			return 2;
		} else {
			return 6;
		}
	} else if (dx > 0) {
		if (dy < 0) {
			return 7;
		} else if (dy > 0) {
			return 1;
		} else {
			return 0;
		}
	} else if (dy < 0) {
		return 5;
	} else if (dy > 0) {
		return 3;
	} else {
		return 4;
	}
}

// Sound callback function for planes
static void PlaneSoundCallback(OBJECTS *ob)
{
	if (ob->ob_firing) {
		sound(S_SHOT, 0, ob);
	} else {
		switch (ob->ob_state) {
		case FALLING:
			if (ob->ob_dy >= 0) {
				sound(S_HIT, 0, ob);
			} else {
				sound(S_FALLING, ob->ob_y, ob);
			}
			break;

		case FLYING:
			sound(S_PLANE, -ob->ob_speed, ob);
			break;

		case STALLED:
		case WOUNDED:
		case WOUNDSTALL:
			sound(S_HIT, 0, ob);
			break;

		default:
			break;
		}
	}
}

static void GroundActorSoundCallback(OBJECTS *ob)
{
	if (ob->ob_bdelay >= 10) {
		sound(S_SHOT, 0, ob);
	}
}

static void CarSoundCallback(OBJECTS *ob)
{
	sound(S_PLANE, ob->ob_dx != 0 ? -2 : -1, ob);
}

bool moveplyr(OBJECTS *ob)
{
	int multkey;

	compplane = false;
	plyrplane = player == ob->ob_plrnum;

	endstat = consoleplayer->ob_endsts;

	if (endstat) {
		--endcount;
		if (endcount <= 0) {
			if (playmode != PLAYMODE_ASYNCH && !quit) {
				swrestart();
				return true;
			}
			swend(NULL, true);
		}
	}

	// get move command for this tic
	multkey =
	    latest_player_commands[ob->ob_plrnum][countmove % MAX_NET_LAG];
	if (ob->ob_control_delay > 0) {
		--ob->ob_control_delay;
		multkey = 0;
	}

	// Thanks to Kodath duMatri for fixing this :)
	if ((multkey & K_HARRYKEYS) != 0 && ob->ob_orient) {
		if (multkey & (K_FLAPU | K_FLAPD)) {
			multkey ^= K_FLAPU | K_FLAPD;
		}
	}

	interpret(ob, multkey);

	if (ob->ob_state == CRASHED && ob->ob_hitcount <= 0) {

		OBJECTS *walker;

		if (endstat != WINNER && ob->ob_life <= QUIT) {
			if (!endstat) {
				loser(ob);
			}
		} else {
			int i;

			// Reuse the plane object. Apart from preserving the player's
			// score and the planes[] entry, this avoids changing the object
			// list while moveplyr() is being called from that list.
			walker = ob;
			for (i = 0; i < num_planes; ++i) {
				if (planes[i]->ob_target == walker) {
					planes[i]->ob_target = NULL;
				}
			}
			walker->ob_type = WALKER;
			walker->ob_state = WALKER_ABANDONED;
			walker->ob_movef = move_walker;
			walker->ob_symbol = &symbol_walker[0].sym[0];
			walker->ob_faction = ob->ob_faction;
			walker->ob_clr = ob->ob_faction;
			walker->ob_hitcount = WALKER_MAXHITS;
			walker->ob_x =
			    clamp_range(0, ob->ob_x, currgame->gm_max_x - 1);
			walker->ob_y = 0;
			walker->ob_orig_y = ob->ob_orig_y;
			walker->ob_original_ob = ob->ob_original_ob;
			walker->ob_plrnum = ob->ob_plrnum;
			walker->ob_sound = NULL;
			walker->ob_soundf = NULL;
			walker->ob_flaps = 0;
			walker->ob_life = ob->ob_life;
			walker->ob_bombs = 2;
			walker->ob_missiles = 0;
			walker->ob_bursts = 0;
			walker->ob_ldx = 0;
			walker->ob_ldy = 0;
			walker->ob_dx = 0;
			walker->ob_dy = 0;
			walker->ob_angle = 0;
			walker->ob_orient = 0;
			walker->ob_speed = 0;
			walker->ob_accel = 0;
			walker->ob_athome = false;
			walker->ob_home = false;
			walker->ob_onmap = true;
			walker->ob_endsts = 0;
			walker->ob_target = NULL;
			walker->ob_owner = NULL;
			walker->ob_mfiring = NULL;
			walker->ob_bdelay = 0;
			walker->ob_mdelay = 0;
			walker->ob_bsdelay = 0;
			walker->ob_firing = NULL;
			walker->ob_bfiring = 0;
			walker->ob_bombing = false;
			walker->ob_goingsun = false;
			walker->ob_rounds = 0;
			walker->ob_missiletarget = NULL;

			walker->ob_y =
			    (int)ground[walker->ob_x] + walker->ob_symbol->h - 1;
			walker->ob_ly = 0;
			updateobjpos(walker);
			if (walker == consoleplayer) {
				swclearsplats();
			}

			endcount = 0;
			if (endstat == WINNER) {
				if (ctlbreak()) {
					swend(NULL, true);
				}
			}
			return true;
		}
	}

	return movepln(ob);
}

static void interpret(OBJECTS *ob, int key)
{
	obstate_t state;

	ob->ob_flaps = 0;
	ob->ob_bombing = ob->ob_bfiring = 0;
	ob->ob_mfiring = ob->ob_firing = NULL;

	state = ob->ob_state;

	if (PlaneIsKilled(state) && state != FALLING) {
		return;
	}

	if (state != FALLING) {
		if (endstat) {
			if (endstat == LOSER && plyrplane) {
				gohome(ob);
			}
			return;
		}

		if (key & K_BREAK) {
			ob->ob_life = QUIT;
			ob->ob_home = false;
			if (ob->ob_athome) {
				ob->ob_state = state = CRASHED;
				ob->ob_hitcount = 0;
			}
			if (plyrplane) {
				quit = true;
			}
		}

		if (key & K_HOME) {
			if (state == FLYING || state == WOUNDED) {
				ob->ob_home = true;
			}
		}
	}

	if (!PlaneIsWounded(state) || (countmove & 1) != 0) {
		if (key & K_FLAPU) {
			++ob->ob_flaps;
			ob->ob_home = false;
		}

		if (key & K_FLAPD) {
			--ob->ob_flaps;
			ob->ob_home = false;
		}

		// We don't allow flipping upside down while sitting
		// on the runway, that would be silly (this was a bug
		// in the original game).
		if ((key & K_FLIP) && !ob->ob_athome) {
			ob->ob_orient = !ob->ob_orient;
			ob->ob_home = false;
		}

		if (key & K_DEACC) {
			if (ob->ob_accel) {
				--ob->ob_accel;
			}
			ob->ob_home = false;
		}

		if (key & K_ACCEL) {
			if (ob->ob_accel < MAX_THROTTLE) {
				++ob->ob_accel;
			}
			ob->ob_home = false;
		}
	}

	if ((key & K_SHOT) && state < FINISHED) {
		ob->ob_firing = ob;
	}

	if ((key & K_MISSILE) && state < FINISHED) {
		ob->ob_mfiring = ob;
	}

	if ((key & K_BOMB) && state < FINISHED) {
		ob->ob_bombing = true;
	}

	if ((key & K_STARBURST) && state < FINISHED) {
		ob->ob_bfiring = true;
	}

	if (key & K_SOUND) {
		if (plyrplane) {
			if (soundflg) {
				sound(0, 0, NULL);
				swsound();
			}
			soundflg = !soundflg;
		}
	}

	if (ob->ob_home) {
		gohome(ob);
	}
}

bool movecomp(OBJECTS *ob)
{
	OBJECTS *ground_target;
	int rc;
	int distance;
	bool nearby_ground_player;

	compplane = true;
	plyrplane = false;

	ob->ob_flaps = 0;
	ob->ob_bfiring = ob->ob_bombing = false;
	ob->ob_mfiring = NULL;

	endstat = ob->ob_endsts;
	ground_target = NULL;
	nearby_ground_player = false;
	if (((consoleplayer->ob_type == WALKER &&
	      consoleplayer->ob_state == WALKER_ABANDONED) ||
	     (consoleplayer->ob_type == CAR &&
	      consoleplayer->ob_state == FLYING)) &&
	    consoleplayer->ob_faction != ob->ob_faction) {
		distance = abs(ob->ob_x - consoleplayer->ob_x) +
		           abs(ob->ob_y - consoleplayer->ob_y);
		nearby_ground_player =
		    distance > 0 &&
		    distance < GROUND_PLAYER_PLANE_NOTICE_RANGE;
		if (nearby_ground_player) {
			ob->ob_target = consoleplayer;
			ground_target = consoleplayer;
		} else if (ob->ob_target == consoleplayer) {
			ob->ob_target = NULL;
		}
	}
	for (OBJECTS *candidate = objtop; candidate != NULL;
	     candidate = candidate->ob_next) {
		int candidate_distance;

		if (((playmode == PLAYMODE_BATTLEFIELD) &&
		     !((candidate->ob_type == TARGET &&
		        candidate->ob_state == STANDING) ||
		       (candidate->ob_type == WALKER &&
		        candidate->ob_state == WALKER_ABANDONED) ||
		       (candidate->ob_type == CAR &&
		        candidate->ob_state == FLYING))) ||
		    ((playmode != PLAYMODE_BATTLEFIELD) &&
		     (candidate->ob_type != TARGET ||
		      candidate->ob_orient != TARGET_TANK ||
		      candidate->ob_state != STANDING)) ||
		    candidate->ob_faction == ob->ob_faction ||
		    candidate->ob_faction == FACTION_NONE) {
			continue;
		}
		candidate_distance = abs(ob->ob_x - candidate->ob_x) +
		                     abs(ob->ob_y - candidate->ob_y);
		if (candidate_distance > 0 &&
		    candidate_distance < GROUND_PLAYER_PLANE_NOTICE_RANGE &&
		    (ground_target == NULL ||
		     candidate_distance <
		         abs(ob->ob_x - ground_target->ob_x) +
		             abs(ob->ob_y - ground_target->ob_y))) {
			ground_target = candidate;
			ob->ob_target = candidate;
		}
	}
	if (playmode == PLAYMODE_BATTLEFIELD) {
		for (int i = 0; i < num_planes; ++i) {
			OBJECTS *candidate = planes[i];
			int candidate_distance;

			if (candidate == ob ||
			    candidate->ob_faction == ob->ob_faction ||
			    candidate->ob_state >= FINISHED ||
			    candidate->ob_state == CRASHED) {
				continue;
			}
			candidate_distance =
			    abs(ob->ob_x - candidate->ob_x) +
			    abs(ob->ob_y - candidate->ob_y);
			if (candidate_distance < 400 &&
			    (ground_target == NULL ||
			     candidate_distance <
			         abs(ob->ob_x - ground_target->ob_x) +
			             abs(ob->ob_y - ground_target->ob_y))) {
				ground_target = candidate;
				ob->ob_target = candidate;
			}
		}
	}
	if (ob->ob_control_delay > 0) {
		--ob->ob_control_delay;
		rc = movepln(ob);
		return rc;
	}

	if (!dispcnt) {
		ob->ob_firing = NULL;
	}

	switch (ob->ob_state) {

	case WOUNDED:
	case WOUNDSTALL:
		if (countmove & 1) {
			break;
		}

	case FLYING:
	case STALLED:
		if (endstat) {
			gohome(ob);
			break;
		}
		if (!dispcnt) {
			swauto(ob);
		}
		if (ground_target != NULL &&
		    ground_target->ob_type == TARGET &&
		    (playmode == PLAYMODE_BATTLEFIELD ||
		     ground_target->ob_orient == TARGET_TANK) &&
		    abs(ob->ob_x - ground_target->ob_x) <= 28 &&
		    ob->ob_y > ground_target->ob_y + 12) {
			ob->ob_bombing = true;
		}
		if (consoleplayer->ob_type == WALKER &&
		    nearby_ground_player && (countmove % 10) == 0) {
				initshot(ob, consoleplayer);
				ob->ob_firing = consoleplayer;
		}
		break;

	case CRASHED:
		ob->ob_firing = NULL;
		if (ob->ob_hitcount <= 0 && !endstat) {
			OBJECTS *airfield;
			int best_distance = 161;

			airfield = NULL;
			for (OBJECTS *candidate = objtop; candidate != NULL;
			     candidate = candidate->ob_next) {
				int d;
				if (candidate->ob_type != TARGET ||
				    candidate->ob_orient != TARGET_HANGAR ||
				    candidate->ob_faction != ob->ob_faction) {
					continue;
				}
				d = abs(candidate->ob_x - ob->ob_original_ob->x);
				if (d < best_distance) {
					airfield = candidate;
					best_distance = d;
				}
			}
			if (playmode != PLAYMODE_BATTLEFIELD &&
			    airfield != NULL &&
			    airfield->ob_state != STANDING) {
				ob->ob_state = FINISHED;
				ob->ob_onmap = false;
				deletex(ob);
				return false;
			}
			clear_owned_ordnance(ob);
			initcomp(ob, ob->ob_original_ob);
			ob->ob_control_delay = FPS;
		}
		break;

	default:
		ob->ob_firing = NULL;
		break;
	}

	rc = movepln(ob);

	return rc;
}

static bool stallpln(OBJECTS *ob)
{
	ob->ob_ldx = ob->ob_ldy = ob->ob_orient = ob->ob_dx = 0;
	ob->ob_angle = 7 * ANGLES / 8;
	ob->ob_speed = 0;
	ob->ob_dy = 0;
	ob->ob_hitcount = STALLCOUNT;
	ob->ob_state = ob->ob_state == WOUNDED ? WOUNDSTALL : STALLED;
	ob->ob_athome = false;

	return true;
}

static bool movepln(OBJECTS *ob)
{
	int nangle, nspeed, limit, update;
	obstate_t state, newstate;
	int x, y, stalled;

	static const signed int gravity[] = {0, -1, -2, -3, -4, -3, -2, -1,
	                                     0, 1,  2,  3,  4,  3,  2,  1};

	state = ob->ob_state;
	ob->ob_soundf = PlaneSoundCallback;

	switch (state) {
	case FINISHED:
	case WAITING:
		return false;

	case CRASHED:
		--ob->ob_hitcount;
		break;

	case FALLING:
		ob->ob_hitcount -= 2;
		if ((ob->ob_dy < 0) && ob->ob_dx) {
			if (ob->ob_orient ^ (ob->ob_dx < 0)) {
				ob->ob_hitcount -= ob->ob_flaps;
			} else {
				ob->ob_hitcount += ob->ob_flaps;
			}
		}

		if (ob->ob_hitcount <= 0) {
			if (ob->ob_dy < 0) {
				if (ob->ob_dx < 0) {
					++ob->ob_dx;
				} else if (ob->ob_dx > 0) {
					--ob->ob_dx;
				} else {
					++ob->ob_orient;
				}
			}

			if (ob->ob_dy > -10) {
				--ob->ob_dy;
			}
			ob->ob_hitcount = FALLCOUNT;
		}
		ob->ob_angle = symangle(ob) * 2;
		if (ob->ob_dy <= 0) {
			initsound(ob, S_FALLING);
		}
		break;

	case STALLED:
		newstate = FLYING;
		goto commonstall;

	case WOUNDSTALL:
		newstate = WOUNDED;

	commonstall:
		stalled = ob->ob_angle != (3 * ANGLES / 4) ||
		          ob->ob_speed < gminspeed;
		if (!stalled) {
			ob->ob_state = state = newstate;
		}
		goto controlled;

	case FLYING:
	case WOUNDED:
		stalled = ob->ob_y >= MAX_Y;
		if (stalled) {
			if (playmode == PLAYMODE_NOVICE) {
				ob->ob_angle = (3 * ANGLES / 4);
				stalled = false;
			} else {
				stallpln(ob);
				state = ob->ob_state;
			}
		}

	controlled:
		if (ob->ob_goingsun) {
			break;
		}

		if (ob->ob_life <= 0 && !ob->ob_athome &&
		    !PlaneIsKilled(state)) {
			hitpln(ob);
			scorepln(ob, GROUND);
			return movepln(ob);
		}

		if (ob->ob_firing) {
			initshot(ob, NULL);
		}

		if (ob->ob_bombing) {
			initbomb(ob);
		}

		if (ob->ob_mfiring) {
			initmiss(ob);
		}

		if (ob->ob_bfiring) {
			initburst(ob);
		}

		nangle = ob->ob_angle;
		nspeed = ob->ob_speed;
		update = ob->ob_flaps;

		if (update) {
			if (ob->ob_orient) {
				nangle -= update;
			} else {
				nangle += update;
			}
			nangle = (nangle + ANGLES) % ANGLES;
		}

		if (!(countmove & 0x0003)) {
			if (!stalled && nspeed < gminspeed &&
			    playmode != PLAYMODE_NOVICE) {
				--nspeed;
				update = true;
			} else {
				limit =
				    gminspeed + ob->ob_accel + gravity[nangle];
				if (nspeed < limit) {
					++nspeed;
					update = true;
				} else if (nspeed > limit) {
					--nspeed;
					update = true;
				}
			}
		}

		if (update) {
			if (ob->ob_athome) {
				if (ob->ob_accel || ob->ob_flaps) {
					nspeed = gminspeed;
				} else {
					nspeed = 0;
				}

			} else if (nspeed <= 0 && !stalled) {
				if (playmode == PLAYMODE_NOVICE) {
					nspeed = 1;
				} else {
					stallpln(ob);
					return movepln(ob);
				}
			}

			ob->ob_speed = nspeed;
			ob->ob_angle = nangle;

			if (stalled) {
				ob->ob_dx = ob->ob_ldx = ob->ob_ldy = 0;
				ob->ob_dy = -nspeed;
			} else {
				setdxdy(ob, nspeed * COS(nangle),
				        nspeed * SIN(nangle));
			}
		}

		if (stalled) {
			--ob->ob_hitcount;
			if (ob->ob_hitcount <= 0) {
				ob->ob_orient = !ob->ob_orient;
				ob->ob_angle =
				    ((3 * ANGLES / 2) - ob->ob_angle) % ANGLES;
				ob->ob_hitcount = STALLCOUNT;
			}
		}

		if (!compplane) {
			ob->ob_life -= ob->ob_speed;
		} else if (ob->ob_life > 100) { /* Just for statistics */
			ob->ob_life -= ob->ob_speed;
		}

		if (ob->ob_speed) {
			ob->ob_athome = false;
		}
		break;
	default:
		break;
	}

	if (ob->ob_endsts == WINNER && ob->ob_goingsun) {
		ob->ob_symbol = &symbol_plane_win[endcount / 18].sym[0];
	} else if (ob->ob_state == FINISHED) {
		ob->ob_symbol = NULL;
	} else if (ob->ob_state == FALLING && !ob->ob_dx && ob->ob_dy < 0) {
		ob->ob_symbol = &symbol_plane_hit[ob->ob_orient % 4].sym[0];
	} else if (ob->ob_orient) {
		// Flipped:
		int a = (16 - ob->ob_angle) % 16;
		ob->ob_symbol = &symbol_plane[a % 4].sym[4 + a / 4];
	} else {
		ob->ob_symbol =
		    &symbol_plane[ob->ob_angle % 4].sym[ob->ob_angle / 4];
	}

	movexy(ob, &x, &y);

	if (!in_range(0, x, currgame->gm_max_x - 16)) {
		x = clamp_range(0, x, currgame->gm_max_x - 16);
		updateobjpos(ob);
	}

	if (!compplane && consoleplayer->ob_endsts == PLAYING &&
	    !PlaneIsKilled(ob->ob_state)) {
		nearpln(ob);
	}

	if (ob->ob_bdelay) {
		--ob->ob_bdelay;
	}
	if (ob->ob_mdelay) {
		--ob->ob_mdelay;
	}
	if (ob->ob_bsdelay) {
		--ob->ob_bsdelay;
	}

	if (!compplane && ob->ob_athome && ob->ob_state == FLYING) {
		refuel(ob);
	}

	if (in_range(0, y, MAX_Y - 1)) {
		if (ob->ob_state == FALLING || PlaneIsWounded(ob->ob_state)) {
			initsmok(ob);
		}
		return plyrplane || ob->ob_state < FINISHED;
	}

	return false;
}

static void adjustfall(OBJECTS *ob)
{
	--ob->ob_life;
	if (ob->ob_life <= 0) {
		if (ob->ob_dy < 0) {
			if (ob->ob_dx < 0) {
				++ob->ob_dx;
			} else if (ob->ob_dx > 0) {
				--ob->ob_dx;
			}
		}
		if (ob->ob_dy > -10) {
			--ob->ob_dy;
		}
		ob->ob_life = BOMBLIFE;
	}
}

bool moveshot(OBJECTS *ob)
{
	int x, y;

	--ob->ob_life;

	if (ob->ob_life <= 0) {
		deallobj(ob);
		return false;
	}

	movexy(ob, &x, &y);

	if (!in_range(0, x, currgame->gm_max_x - 1) ||
	    !in_range((int) ground[x] + 1, y, MAX_Y - 1)) {
		deallobj(ob);
		return false;
	}

	ob->ob_symbol = &symbol_pixel;
	return true;
}

static void BombSoundCallback(OBJECTS *ob)
{
	if (ob->ob_dy <= 0) {
		sound(S_BOMB, -ob->ob_y, ob);
	}
}

bool movebomb(OBJECTS *ob)
{
	int x, y;
	int ang;

	ob->ob_soundf = BombSoundCallback;

	if (ob->ob_life < 0) {
		deallobj(ob);
		ob->ob_state = FINISHED;
		return false;
	}

	adjustfall(ob);

	if (ob->ob_dy <= 0) {
		initsound(ob, S_BOMB);
	}

	movexy(ob, &x, &y);

	if (y < 0 || !in_range(0, x, currgame->gm_max_x - 1)) {
		deallobj(ob);
		stopsound(ob);
		ob->ob_state = FINISHED;
		return false;
	}

	ang = symangle(ob);
	if (ob->ob_type == GRENADE) {
		ob->ob_symbol = &symbol_grenade[0].sym[0];
	} else {
		ob->ob_symbol = &symbol_bomb[ang % 2].sym[ang / 2];
	}

	if (y >= MAX_Y) {
		return false;
	}

	return true;
}

bool movemiss(OBJECTS *ob)
{
	int x, y, angle;
	OBJECTS *obt;

	if (ob->ob_life < 0) {
		deallobj(ob);
		ob->ob_state = FINISHED;
		return false;
	}

	if (ob->ob_state == FLYING) {
		obt = ob->ob_missiletarget;

		if (obt != ob->ob_owner && (ob->ob_life & 1)) {
			if (obt->ob_missiletarget) {
				obt = obt->ob_missiletarget;
			}
			aim(ob, obt->ob_x, obt->ob_y, NULL, false);
			angle = ob->ob_angle =
			    (ob->ob_angle + ob->ob_flaps + ANGLES) % ANGLES;
			setdxdy(ob, ob->ob_speed * COS(angle),
			        ob->ob_speed * SIN(angle));
		}
		movexy(ob, &x, &y);

		--ob->ob_life;

		if (ob->ob_life <= 0 || y >= ((MAX_Y * 3) / 2)) {
			ob->ob_state = FALLING;
			++ob->ob_life;
		}
	} else {
		adjustfall(ob);
		ob->ob_angle = (ob->ob_angle + 1) % ANGLES;
		movexy(ob, &x, &y);
	}

	if (y < 0 || !in_range(0, x, currgame->gm_max_x - 1)) {
		deallobj(ob);
		ob->ob_state = FINISHED;
		return false;
	}

	ob->ob_symbol = &symbol_missile[ob->ob_angle % 4].sym[ob->ob_angle / 4];

	if (y >= MAX_Y) {
		return false;
	}

	return true;
}

bool moveburst(OBJECTS *ob)
{
	int x, y;

	if (ob->ob_life < 0) {
		ob->ob_owner->ob_missiletarget = NULL;
		deallobj(ob);
		return false;
	}

	adjustfall(ob);
	movexy(ob, &x, &y);

	if (!in_range(0, x, currgame->gm_max_x - 1) || y <= (int) ground[x]) {
		ob->ob_owner->ob_missiletarget = NULL;
		deallobj(ob);
		return false;
	}

	ob->ob_owner->ob_missiletarget = ob;
	ob->ob_symbol = &symbol_burst[ob->ob_life & 1].sym[0];

	return y < MAX_Y;
}

static void TargetSoundCallback(OBJECTS *ob)
{
	if (ob->ob_firing) {
		sound(S_SHOT, 0, ob);
	}
}

static OBJECTS *FindEnemyPlane(OBJECTS *ob)
{
	OBJECTS *obp;
	int r;

	// TODO: We can do better than scanning the entire object list.
	for (obp = objtop; obp != NULL; obp = obp->ob_next) {
		// TODO: Targets consider any plane of a different faction
		// to be an enemy. In the future we may want to support
		// alliances between factions.
		if (obp->ob_type != PLANE ||
		    obp->ob_faction == ob->ob_faction) {
			continue;
		}
		// In single player mode, computer planes do not get targeted.
		if (playmode != PLAYMODE_ASYNCH &&
		    obp->ob_faction != FACTION_PLAYER1) {
			continue;
		}
		if (PlaneIsKilled(obp->ob_state)) {
			continue;
		}
		r = range(ob->ob_x, ob->ob_y, obp->ob_x, obp->ob_y);
		if (in_range(1, r, targrnge - 1)) {
			return obp;
		}
	}

	return NULL;
}

// This table determines how often targets fire at enemy planes; there is one
// entry for each target type. An aggression of zero means that type never
// fires; otherwise the lower the value, the more often it fires.
static const int target_aggression[NUM_TARGET_TYPES] = {
    2, // TARGET_HANGAR
    2, // TARGET_FACTORY
    2, // TARGET_OIL_TANK
    2, // TARGET_TANK
    5, // TARGET_TRUCK
    5, // TARGET_TANKER_TRUCK
    0, // TARGET_FLAG
    0, // TARGET_TENT
    2, // TARGET_CUSTOM1
    2, // TARGET_CUSTOM2
    2, // TARGET_CUSTOM3
    5, // TARGET_CUSTOM4
    5, // TARGET_CUSTOM5
    0, // TARGET_CUSTOM_PASSIVE1
    0, // TARGET_CUSTOM_PASSIVE2
    0, // TARGET_CUSTOM_PASSIVE3
    0, // TARGET_CUSTOM_PASSIVE4
    0, // TARGET_CUSTOM_PASSIVE5
    0, // TARGET_RADIO_TOWER
    0, // TARGET_WATER_TOWER
};

bool movetarg(OBJECTS *ob)
{
	sopsym_t *oldsym = ob->ob_symbol;
	int transform = ob->ob_original_ob->transform;
	int aggression;

	ob->ob_soundf = TargetSoundCallback;
	ob->ob_firing = NULL;

	assert(ob->ob_orient < arrlen(target_aggression));
	aggression = target_aggression[ob->ob_orient];

	if (ob->ob_state == STANDING && gamenum > 0 && aggression > 0 &&
	    (gamenum > 1 || (countmove % aggression) == (aggression - 1))) {
		OBJECTS *plane = FindEnemyPlane(ob);
		if (plane) {
			initshot(ob, plane);
			ob->ob_firing = plane;
		}
	}

	ob->ob_hitcount = clamp_min(ob->ob_hitcount - 1, 0);

	if (ob->ob_state == STANDING) {
		ob->ob_symbol = &symbol_targets[ob->ob_orient].sym[transform];
	} else {
		ob->ob_symbol =
		    &symbol_target_hit[ob->ob_orient].sym[transform];
	}

	// Symbol changes on explosion, and the new sprite might be a
	// different size. Stay centered, and if the symbol height changes, we
	// need to adjust Y so the target stays on the ground.
	ob->ob_x += (oldsym->w - ob->ob_symbol->w) / 2;
	ob->ob_y -= oldsym->h - ob->ob_symbol->h;

	return true;
}

bool movepowerup(OBJECTS *ob)
{
	int transform = ob->ob_original_ob->transform;

	if (ob->ob_state == STANDING) {
		ob->ob_symbol = &symbol_powerups[ob->ob_orient].sym[transform];
	} else {
		ob->ob_symbol =
		    &symbol_powerup_collected[ob->ob_orient].sym[transform];
	}

	return true;
}

static void ExplosionSoundCallback(OBJECTS *ob)
{
	if (ob->ob_orient) {
		sound(S_EXPLOSION, ob->ob_hitcount, ob);
	}
}

bool moveexpl(OBJECTS *obp)
{
	OBJECTS *ob;
	int x, y;
	int orient;

	obp->ob_soundf = ExplosionSoundCallback;

	ob = obp;
	orient = ob->ob_orient;
	if (ob->ob_life < 0) {
		if (orient) {
			stopsound(ob);
		}
		deallobj(ob);
		return false;
	}

	--ob->ob_life;

	if (ob->ob_life <= 0) {
		if (ob->ob_dy < 0) {
			if (ob->ob_dx < 0) {
				++ob->ob_dx;
			} else if (ob->ob_dx > 0) {
				--ob->ob_dx;
			}
		}
		if ((ob->ob_orient && ob->ob_dy > -10) ||
		    (!ob->ob_orient && ob->ob_dy > -gminspeed)) {
			--ob->ob_dy;
		}
		ob->ob_life = EXPLLIFE;
	}

	movexy(ob, &x, &y);

	if (!in_range(0, x, currgame->gm_max_x - 1) || y <= (int) ground[x]) {
		if (orient) {
			stopsound(ob);
		}
		deallobj(ob);
		return false;
	}
	++ob->ob_hitcount;

	ob->ob_symbol = &symbol_debris[ob->ob_orient].sym[0];

	return y < MAX_Y;
}

bool movesmok(OBJECTS *obp)
{
	OBJECTS *ob;
	obstate_t planestate;

	ob = obp;

	planestate = ob->ob_owner->ob_state;

	--ob->ob_life;

	if (ob->ob_life <= 0 ||
	    (planestate != FALLING && planestate != CRASHED &&
	     !PlaneIsWounded(planestate))) {
		deallobj(ob);
		return false;
	}
	ob->ob_symbol = &symbol_pixel;

	return true;
}

bool moveflck(OBJECTS *obp)
{
	OBJECTS *ob;
	int x, y;

	ob = obp;

	if (ob->ob_life == -1) {
		deallobj(ob);
		return false;
	}

	--ob->ob_life;

	if (ob->ob_life <= 0) {
		ob->ob_orient = !ob->ob_orient;
		ob->ob_life = FLOCKLIFE;
	}

	// Flocks fly back and forth within their "territory".
	if (ob->ob_x < ob->ob_original_ob->territory_l) {
		ob->ob_dx = abs(ob->ob_dx);
	} else if (ob->ob_x > ob->ob_original_ob->territory_r) {
		ob->ob_dx = -abs(ob->ob_dx);
	}

	movexy(ob, &x, &y);
	ob->ob_symbol = &symbol_flock[ob->ob_orient].sym[0];
	return true;
}

bool moveballoon(OBJECTS *ob)
{
	const original_ob_t *orig = ob->ob_original_ob;
	int x, y, dx, dy, f;
	int step;

	if (ob->ob_life == -1) {
		// TODO: Explosion animation?
		deallobj(ob);
		return false;
	}

	// The spotter in the balloon occasionally fires their pistol. But they
	// don't shoot upwards (bullets never come from the top of the balloon)
	if (ob->ob_state == FLYING && gamenum > 0 && (countmove % 7) == 0) {
		OBJECTS *plane = FindEnemyPlane(ob);
		if (plane && plane->ob_y < ob->ob_y) {
			initshot(ob, plane);
			ob->ob_firing = plane;
		}
	}

	// If we have two balloons next to each other, we don't want them to
	// move perfectly synchronized. So we use the X coordinate as a kind
	// of random element.
	step = countmove + orig->x;

	// We adjust the momentum on each frame in both x and y dimensions,
	// which makes the balloon "float" around in a randomish way. However,
	// since we step through each entry in the sine table, all movements
	// cancel out and we never drift out of the same area of the map.
	dx = SIN(step / 7) * 128;
	dy = SIN(step / 3) * 128;
	ob->ob_dx = dx >> 16;
	ob->ob_ldx = dx & 0xffff;
	ob->ob_dy = dy >> 16;
	ob->ob_ldy = dy & 0xffff;
	movexy(ob, &x, &y);

	// Which way are we drifting?
	f = orig->orient * 3 + (dx >= 20000 ? 2 : dx <= -20000 ? 0 : 1);

	ob->ob_symbol = &symbol_balloon[f].sym[orig->transform];
	return true;
}

static bool checkwall(OBJECTS *obp, int direction)
{
	int check_x, cnt;

	check_x = obp->ob_x;
	for (cnt = 0; cnt < 20; ++cnt) {
		if (!in_range(0, check_x, currgame->gm_max_x - 1)) {
			return true;
		}
		if ((int) ground[check_x] > obp->ob_y + 10) {
			return true;
		}
		if (direction < 0) {
			--check_x;
		} else {
			++check_x;
		}
	}
	return false;
}

bool movebird(OBJECTS *obp)
{
	OBJECTS *ob;
	int x, y;

	ob = obp;

	if (ob->ob_life == -1) {
		deallobj(ob);
		return false;
	} else if (ob->ob_life == -2) {
		ob->ob_dy = -ob->ob_dy;
		ob->ob_dx = (countmove & 7) - 4;
		// Don't move in a direction where we might (continue to?)
		// fly into a wall. Fixes a crasher bug.
		if (checkwall(ob, ob->ob_dx)) {
			ob->ob_dx = -ob->ob_dx;
		}
		ob->ob_life = BIRDLIFE;
	} else {
		--ob->ob_life;

		if (ob->ob_life <= 0) {
			ob->ob_orient = !ob->ob_orient;
			ob->ob_life = BIRDLIFE;
		}
	}

	movexy(ob, &x, &y);

	ob->ob_symbol = &symbol_bird[ob->ob_orient].sym[0];
	if (!in_range(0, x, currgame->gm_max_x - 1) ||
	    !in_range((int) ground[x] + 1, y, MAX_Y - 1)) {
		ob->ob_y -= ob->ob_dy;
		ob->ob_life = -2;
		return false;
	}
	return true;
}

bool moveox(OBJECTS *ob)
{
	int transform = ob->ob_original_ob->transform;
	ob->ob_symbol = &symbol_ox[ob->ob_state != STANDING].sym[transform];
	return true;
}

bool crashpln(OBJECTS *ob)
{
	const original_ob_t *orig_ob = ob->ob_original_ob;

	if (ob->ob_dx < 0) {
		ob->ob_angle = (ob->ob_angle + 2) % ANGLES;
	} else {
		ob->ob_angle = (ob->ob_angle + ANGLES - 2) % ANGLES;
	}

	ob->ob_state = CRASHED;
	ob->ob_athome = false;
	ob->ob_dx = ob->ob_dy = ob->ob_ldx = ob->ob_ldy = ob->ob_speed = 0;

	ob->ob_hitcount = ((abs(orig_ob->x - ob->ob_x) < SAFERESET) &&
	                   (abs(ob->ob_orig_y - ob->ob_y) < SAFERESET))
	                    ? (MAXCRCOUNT << 1)
	                    : MAXCRCOUNT;

	return true;
}

bool hitpln(OBJECTS *obp)
{
	OBJECTS *ob;

	ob = obp;
	ob->ob_ldx = ob->ob_ldy = 0;
	ob->ob_hitcount = FALLCOUNT;
	ob->ob_state = FALLING;
	ob->ob_athome = false;

	return true;
}

static void init_ground_shot(OBJECTS *shooter, int direction)
{
	OBJECTS *shot;

	if (shooter->ob_bdelay > 0) {
		return;
	}
	shot = allocobj();
	shot->ob_type = GROUND_SHOT;
	shot->ob_state = FLYING;
	shot->ob_x = shooter->ob_x + shooter->ob_symbol->w / 2;
	shot->ob_y = shooter->ob_y - shooter->ob_symbol->h / 2;
	shot->ob_dx = direction * 4;
	shot->ob_dy = shot->ob_lx = shot->ob_ly = shot->ob_ldx =
	    shot->ob_ldy = 0;
	shot->ob_life = 14;
	shot->ob_owner = shooter;
	shot->ob_clr = shooter->ob_clr;
	shot->ob_symbol = &symbol_pixel;
	shot->ob_sound = NULL;
	shot->ob_soundf = NULL;
	shot->ob_movef = moveshot;
	shot->ob_onmap = false;
	shooter->ob_bdelay = 12;
	shooter->ob_firing = shot;
	shooter->ob_soundf = GroundActorSoundCallback;
	insertx(shot, shooter);
}

static void init_ground_grenade(OBJECTS *shooter)
{
	OBJECTS *grenade;
	int direction;

	if (shooter->ob_bombs <= 0 || shooter->ob_bdelay > 0) {
		return;
	}
	direction = shooter->ob_orient ? -1 : 1;
	grenade = allocobj();
	grenade->ob_type = GRENADE;
	grenade->ob_state = FALLING;
	if (direction < 0) {
		grenade->ob_x =
		    shooter->ob_x - symbol_grenade[0].sym[0].w - 2;
	} else {
		grenade->ob_x = shooter->ob_x + shooter->ob_symbol->w + 2;
	}
	grenade->ob_x =
	    clamp_range(0, grenade->ob_x, currgame->gm_max_x - 10);
	grenade->ob_y = shooter->ob_y + 5;
	grenade->ob_dx = direction * 2;
	grenade->ob_dy = 2;
	grenade->ob_lx = grenade->ob_ly = grenade->ob_ldx =
	    grenade->ob_ldy = 0;
	grenade->ob_life = BOMBLIFE;
	grenade->ob_owner = shooter;
	grenade->ob_clr = shooter->ob_clr;
	grenade->ob_symbol = &symbol_grenade[0].sym[0];
	grenade->ob_sound = NULL;
	grenade->ob_soundf = NULL;
	grenade->ob_movef = movebomb;
	grenade->ob_onmap = true;
	--shooter->ob_bombs;
	shooter->ob_bdelay = 15;
	insertx(grenade, shooter);
}

static bool is_owned_ordnance(OBJECTS *ob, OBJECTS *owner)
{
	OBJECTS *source;

	switch (ob->ob_type) {
	case BOMB:
	case GRENADE:
	case MISSILE:
	case SHOT:
	case GROUND_SHOT:
	case STARBURST:
	case EXPLOSION:
		break;
	default:
		return false;
	}

	source = ob->ob_owner;
	return source == owner ||
	       (source != NULL && source->ob_owner == owner);
}

static void clear_owned_ordnance(OBJECTS *owner)
{
	OBJECTS *ob, *next;

	for (ob = objtop; ob != NULL; ob = next) {
		next = ob->ob_next;
		if (ob != owner && is_owned_ordnance(ob, owner)) {
			stopsound(ob);
			deallobj(ob);
		}
	}
}

bool move_grenade_pickup(OBJECTS *obp)
{
	if (obp->ob_state == FINISHED) {
		deallobj(obp);
		return false;
	}
	if (obp->ob_state == WAITING && --obp->ob_life <= 0) {
		obp->ob_state = STANDING;
	}
	return true;
}

bool move_blood(OBJECTS *obp)
{
	int x, y;

	if (--obp->ob_life <= 0) {
		deallobj(obp);
		return false;
	}
	if ((countmove & 1) != 0) {
		--obp->ob_dy;
	}
	movexy(obp, &x, &y);
	if (y <= ground[x]) {
		deallobj(obp);
		return false;
	}
	return true;
}

#define BATTLEFIELD_WAVE_TICKS   (60 * FPS)
#define BATTLEFIELD_REPAIR_TICKS (20 * FPS)
#define BATTLEFIELD_ARMY_LIMIT   36

static int battlefield_unit_count(faction_t faction)
{
	OBJECTS *ob;
	int result = 0;

	for (ob = objtop; ob != NULL; ob = ob->ob_next) {
		if (ob->ob_faction != faction || ob->ob_state == FINISHED) {
			continue;
		}
		if ((ob->ob_type == WALKER &&
		     ob->ob_movef == move_battlefield_soldier) ||
		    (ob->ob_type == TARGET &&
		     ob->ob_orient == TARGET_TANK &&
		     ob->ob_movef == move_tank)) {
			++result;
		}
	}
	return result;
}

static void spawn_battlefield_group(faction_t faction, int spawn_x,
	                                OBJECTS *source, int *unit_count)
{
	OBJECTS *ob;
	int i, count, direction;

	if (*unit_count >= BATTLEFIELD_ARMY_LIMIT || source == NULL) {
		return;
	}
	direction = faction == FACTION_PLAYER1 ? 1 : -1;
	count = (rand() & 1) ? 3 : 1;
	for (i = 0; i < count && *unit_count < BATTLEFIELD_ARMY_LIMIT; ++i) {
		ob = allocobj();
		ob->ob_original_ob = source->ob_original_ob;
		ob->ob_faction = faction;
		ob->ob_clr = faction;
		ob->ob_state = STANDING;
		ob->ob_onmap = true;
		ob->ob_x = clamp_range(
		    0, spawn_x + direction * i * 12,
		    currgame->gm_max_x - 20);
		ob->ob_dx = ob->ob_dy = ob->ob_lx = ob->ob_ly =
		    ob->ob_ldx = ob->ob_ldy = 0;
		if (count == 1) {
			ob->ob_type = TARGET;
			ob->ob_orient = TARGET_TANK;
			ob->ob_angle = direction < 0;
			ob->ob_symbol = &symbol_targets[TARGET_TANK].sym[0];
			ob->ob_y = ground[ob->ob_x] + ob->ob_symbol->h - 1;
			ob->ob_movef = move_tank;
		} else {
			ob->ob_type = WALKER;
			ob->ob_state = WALKER_ABANDONED;
			ob->ob_symbol = &symbol_walker[0].sym[0];
			ob->ob_y =
			    ground[ob->ob_x] + ob->ob_symbol->h - 1;
			ob->ob_hitcount = 1;
			ob->ob_bombs = 2;
			ob->ob_movef = move_battlefield_soldier;
		}
		insertx(ob, source);
		++*unit_count;
	}
}

static void spawn_battlefield_wave(void)
{
	OBJECTS *building, *source[NUM_FACTIONS] = {NULL};
	int units[NUM_FACTIONS] = {0};
	faction_t faction;

	units[FACTION_PLAYER1] =
	    battlefield_unit_count(FACTION_PLAYER1);
	units[FACTION_PLAYER2] =
	    battlefield_unit_count(FACTION_PLAYER2);
	for (building = objtop; building != NULL;
	     building = building->ob_next) {
		if (building->ob_type == TARGET &&
		    building->ob_orient != TARGET_TANK &&
		    building->ob_state == STANDING &&
		    (building->ob_faction == FACTION_PLAYER1 ||
		     building->ob_faction == FACTION_PLAYER2)) {
			source[building->ob_faction] = building;
		}
	}
	for (faction = FACTION_PLAYER1; faction <= FACTION_PLAYER2;
	     ++faction) {
		int edge_x = faction == FACTION_PLAYER1
		                 ? 70
		                 : currgame->gm_max_x - 90;
		spawn_battlefield_group(faction, edge_x, source[faction],
		                        &units[faction]);
	}
	for (building = objtop; building != NULL;
	     building = building->ob_next) {
		if (building->ob_type != TARGET ||
		    building->ob_orient == TARGET_TANK ||
		    building->ob_state != STANDING ||
		    (building->ob_faction != FACTION_PLAYER1 &&
		     building->ob_faction != FACTION_PLAYER2)) {
			continue;
		}
		spawn_battlefield_group(
		    building->ob_faction,
		    building->ob_x +
		        (building->ob_faction == FACTION_PLAYER1
		             ? building->ob_symbol->w + 4
		             : -14),
		    building, &units[building->ob_faction]);
	}
}

static bool battlefield_repairer_valid(OBJECTS *building,
	                                    OBJECTS *soldier)
{
	return soldier != NULL && soldier->ob_type == WALKER &&
	       soldier->ob_movef == move_battlefield_soldier &&
	       soldier->ob_state == WALKER_ABANDONED &&
	       abs(soldier->ob_x - building->ob_x) <=
	           building->ob_symbol->w + 12;
}

static void update_battlefield_repairs(void)
{
	OBJECTS *building, *soldier;

	for (building = objtop; building != NULL;
	     building = building->ob_next) {
		if (building->ob_type != TARGET ||
		    building->ob_orient == TARGET_TANK ||
		    building->ob_state != FINISHED) {
			continue;
		}
		if (!battlefield_repairer_valid(building,
		                                building->ob_target)) {
			building->ob_target = NULL;
			building->ob_life = 0;
			for (soldier = objtop; soldier != NULL;
			     soldier = soldier->ob_next) {
				if (battlefield_repairer_valid(building,
				                               soldier)) {
					building->ob_target = soldier;
					break;
				}
			}
		}
		if (building->ob_target == NULL) {
			continue;
		}
		if (++building->ob_life >= BATTLEFIELD_REPAIR_TICKS) {
			building->ob_faction =
			    building->ob_target->ob_faction;
			building->ob_clr = building->ob_faction;
			building->ob_state = STANDING;
			building->ob_onmap = true;
			building->ob_hitcount = building->ob_life = 0;
			building->ob_target = NULL;
			++numtarg[building->ob_faction];
		}
	}
}

static void update_battlefield(void)
{
	if (countmove == 1 ||
	    (countmove % BATTLEFIELD_WAVE_TICKS) == 0) {
		spawn_battlefield_wave();
	}
	update_battlefield_repairs();
}

#define TANK_NOTICE_RANGE 160
#define TANK_FIRE_RANGE    56
#define TANK_SHELL_SPEED   4
#define TANK_SHELL_LIFE    (TANK_FIRE_RANGE / TANK_SHELL_SPEED)

static bool valid_tank_target(OBJECTS *tank, OBJECTS *target)
{
	if (target == NULL || target->ob_faction == tank->ob_faction ||
	    target->ob_faction == FACTION_NONE) {
		return false;
	}
	return (target->ob_type == WALKER &&
	        target->ob_state == WALKER_ABANDONED) ||
	       (target->ob_type == CAR && target->ob_state == FLYING) ||
	       (target->ob_type == TARGET &&
	        target->ob_orient == TARGET_TANK &&
	        target->ob_state == STANDING) ||
	       (playmode == PLAYMODE_BATTLEFIELD &&
	        ((target->ob_type == TARGET &&
	          target->ob_state == STANDING) ||
	         (target->ob_type == PLANE &&
	          target->ob_state < FINISHED &&
	          target->ob_state != CRASHED)));
}

static OBJECTS *find_tank_target(OBJECTS *tank)
{
	OBJECTS *candidate, *best = NULL;
	int distance, best_distance = TANK_NOTICE_RANGE + 1;

	for (candidate = objtop; candidate != NULL;
	     candidate = candidate->ob_next) {
		if (!valid_tank_target(tank, candidate)) {
			continue;
		}
		distance = abs(candidate->ob_x - tank->ob_x) +
		           abs(candidate->ob_y - tank->ob_y);
		if (distance < best_distance) {
			best = candidate;
			best_distance = distance;
		}
	}
	return best;
}

bool move_tank_shell(OBJECTS *shell)
{
	int x, y;

	if (shell->ob_life < 0) {
		deallobj(shell);
		return false;
	}
	if (--shell->ob_life <= 0 ||
	    (shell->ob_dx < 0 && shell->ob_x <= 1) ||
	    (shell->ob_dx > 0 &&
	     shell->ob_x >= currgame->gm_max_x - 11)) {
		initexpl(shell, 0);
		stopsound(shell);
		deallobj(shell);
		return false;
	}
	movexy(shell, &x, &y);
	shell->ob_symbol = &symbol_bomb[0].sym[0];
	return true;
}

static void init_tank_shell(OBJECTS *tank, int direction)
{
	OBJECTS *shell;

	if (tank->ob_bdelay > 0) {
		return;
	}
	shell = allocobj();
	if (shell == NULL) {
		return;
	}
	shell->ob_type = BOMB;
	shell->ob_state = FLYING;
	shell->ob_x = direction < 0
	                  ? tank->ob_x - symbol_bomb[0].sym[0].w - 2
	                  : tank->ob_x + tank->ob_symbol->w + 2;
	shell->ob_x =
	    clamp_range(0, shell->ob_x, currgame->gm_max_x - 10);
	// Keep the shell just above the ground-collision threshold while
	// still lining it up with people and cars.
	shell->ob_y = tank->ob_y - 2;
	shell->ob_dx = direction * TANK_SHELL_SPEED;
	shell->ob_dy = shell->ob_lx = shell->ob_ly = shell->ob_ldx =
	    shell->ob_ldy = 0;
	shell->ob_life = TANK_SHELL_LIFE;
	shell->ob_owner = tank;
	shell->ob_clr = tank->ob_clr;
	shell->ob_orient = direction < 0;
	shell->ob_symbol = &symbol_bomb[0].sym[0];
	shell->ob_sound = NULL;
	shell->ob_soundf = NULL;
	shell->ob_movef = move_tank_shell;
	shell->ob_onmap = true;
	tank->ob_bdelay = 25;
	tank->ob_firing = shell;
	tank->ob_soundf = GroundActorSoundCallback;
	insertx(shell, tank);
}

static int tank_ground_y(OBJECTS *tank)
{
	int x, result = 0;
	int xmax = clamp_max(currgame->gm_max_x - 1,
	                     tank->ob_x + tank->ob_symbol->w - 1);

	for (x = tank->ob_x; x <= xmax; ++x) {
		result = imax(result, ground[x]);
	}
	return result + tank->ob_symbol->h - 1;
}

bool move_tank(OBJECTS *tank)
{
	OBJECTS *target;
	int distance, x, y;

	if (tank->ob_state != STANDING) {
		if (playmode == PLAYMODE_BATTLEFIELD &&
		    tank->ob_original_ob->orient != TARGET_TANK) {
			deallobj(tank);
			return false;
		}
		return movetarg(tank);
	}
	tank->ob_firing = NULL;
	if (tank->ob_bdelay > 0) {
		--tank->ob_bdelay;
	}
	target = find_tank_target(tank);
	tank->ob_target = target;
	tank->ob_dx = 0;
	if (target != NULL) {
		distance = target->ob_x - tank->ob_x;
		tank->ob_angle = distance < 0;
		if (abs(distance) <= TANK_FIRE_RANGE) {
			init_tank_shell(tank, distance < 0 ? -1 : 1);
		} else {
			tank->ob_dx =
			    distance < 0 ? -WALKER_SPEED : WALKER_SPEED;
		}
	} else if (playmode == PLAYMODE_BATTLEFIELD) {
		tank->ob_dx = tank->ob_faction == FACTION_PLAYER1
		                  ? WALKER_SPEED
		                  : -WALKER_SPEED;
		tank->ob_angle = tank->ob_dx < 0;
	}
	movexy(tank, &x, &y);
	tank->ob_y = tank_ground_y(tank);
	tank->ob_ly = tank->ob_dy = 0;
	tank->ob_symbol = &symbol_targets[TARGET_TANK].sym[0];
	tank->ob_hitcount = clamp_min(tank->ob_hitcount - 1, 0);
	return true;
}

static bool valid_battlefield_target(OBJECTS *soldier, OBJECTS *target)
{
	if (target == NULL || target == soldier) {
		return false;
	}
	if (target->ob_type == TARGET &&
	    target->ob_orient != TARGET_TANK &&
	    target->ob_state == FINISHED) {
		return target->ob_target == NULL ||
		       target->ob_target == soldier;
	}
	if (target->ob_faction == soldier->ob_faction ||
	    target->ob_faction == FACTION_NONE) {
		return false;
	}
	return (target->ob_type == WALKER &&
	        target->ob_state == WALKER_ABANDONED) ||
	       (target->ob_type == CAR && target->ob_state == FLYING) ||
	       (target->ob_type == TARGET &&
	        target->ob_state == STANDING) ||
	       (target->ob_type == PLANE &&
	        target->ob_state < FINISHED &&
	        target->ob_state != CRASHED);
}

static OBJECTS *find_battlefield_target(OBJECTS *soldier)
{
	OBJECTS *candidate, *best = NULL;
	int distance, best_distance = 181;

	for (candidate = objtop; candidate != NULL;
	     candidate = candidate->ob_next) {
		if (!valid_battlefield_target(soldier, candidate)) {
			continue;
		}
		distance = abs(candidate->ob_x - soldier->ob_x) +
		           abs(candidate->ob_y - soldier->ob_y);
		if (distance < best_distance) {
			best = candidate;
			best_distance = distance;
		}
	}
	return best;
}

static bool move_battlefield_soldier(OBJECTS *soldier)
{
	OBJECTS *target;
	int distance, x, y, frame = 0;

	if (soldier->ob_state == FINISHED) {
		deallobj(soldier);
		return false;
	}
	if (soldier->ob_bdelay > 0) {
		--soldier->ob_bdelay;
	}
	target = find_battlefield_target(soldier);
	soldier->ob_target = target;
	soldier->ob_dx = 0;
	if (target != NULL) {
		distance = target->ob_x - soldier->ob_x;
		soldier->ob_orient = distance < 0;
		if (target->ob_type == TARGET &&
		    target->ob_state == FINISHED) {
			int repair_x =
			    target->ob_x +
			    (soldier->ob_faction == FACTION_PLAYER1
			         ? -soldier->ob_symbol->w - 3
			         : target->ob_symbol->w + 3);
			distance = repair_x - soldier->ob_x;
			if (abs(distance) > WALKER_SPEED) {
				soldier->ob_dx =
				    distance < 0 ? -WALKER_SPEED
				                 : WALKER_SPEED;
				frame = 1 + ((countmove / 4) & 1);
			}
		} else {
			if (soldier->ob_bombs > 0 &&
			    abs(distance) >= 30 && abs(distance) <= 60) {
				init_ground_grenade(soldier);
			}
			if (abs(distance) <= 80) {
				init_ground_shot(
				    soldier, distance < 0 ? -1 : 1);
			}
			if (abs(distance) > 24) {
				soldier->ob_dx =
				    distance < 0 ? -WALKER_SPEED
				                 : WALKER_SPEED;
				frame = 1 + ((countmove / 4) & 1);
			}
		}
	} else {
		soldier->ob_dx =
		    soldier->ob_faction == FACTION_PLAYER1
		        ? WALKER_SPEED
		        : -WALKER_SPEED;
		soldier->ob_orient = soldier->ob_dx < 0;
		frame = 1 + ((countmove / 4) & 1);
	}
	movexy(soldier, &x, &y);
	soldier->ob_y =
	    clamp_max(ground[soldier->ob_x] +
	                  soldier->ob_symbol->h - 1,
	              SCR_HGHT - 1);
	soldier->ob_ly = soldier->ob_dy = 0;
	soldier->ob_symbol = &symbol_walker[frame].sym[0];
	return true;
}

static OBJECTS *find_enemy_soldier(OBJECTS *seeker, int max_distance)
{
	OBJECTS *candidate, *best = NULL;
	int distance, best_distance = max_distance + 1;

	for (candidate = objtop; candidate != NULL;
	     candidate = candidate->ob_next) {
		if (candidate->ob_type != WALKER ||
		    candidate->ob_state != WALKER_ABANDONED ||
		    candidate->ob_movef == move_walker ||
		    candidate->ob_faction == seeker->ob_faction) {
			continue;
		}
		distance = abs(candidate->ob_x - seeker->ob_x);
		if (distance < best_distance) {
			best = candidate;
			best_distance = distance;
		}
	}
	return best;
}

static bool valid_soldier_target(OBJECTS *target)
{
	return target != NULL &&
	       ((target->ob_type == WALKER &&
	         target->ob_state == WALKER_ABANDONED) ||
	        target->ob_type == CAR ||
	        (target->ob_type == PLANE && target->ob_state != CRASHED &&
	         target->ob_state < FINISHED));
}

bool move_enemy_soldier(OBJECTS *obp)
{
	OBJECTS *target = consoleplayer;
	int distance, frame, orient;

	if (obp->ob_state == FINISHED) {
		obp->ob_bombs = 1;
		obp->ob_type = GRENADE_PICKUP;
		obp->ob_state = WAITING;
		obp->ob_life = 6;
		obp->ob_movef = move_grenade_pickup;
		obp->ob_symbol = &symbol_grenade[0].sym[0];
		obp->ob_y =
		    clamp_max(ground[obp->ob_x] + obp->ob_symbol->h - 1,
		              SCR_HGHT - 1);
		obp->ob_dx = obp->ob_dy = 0;
		updateobjpos(obp);
		return true;
	}

	if (obp->ob_bdelay > 0) {
		--obp->ob_bdelay;
	}
	obp->ob_dx = 0;
	frame = 0;
	if (!valid_soldier_target(obp->ob_target)) {
		obp->ob_target = find_enemy_soldier(obp, 140);
	}
	if (valid_soldier_target(target)) {
		distance = target->ob_x - obp->ob_x;
		if (abs(distance) <= 140) {
			obp->ob_target = target;
		}
	}
	target = obp->ob_target;
	if (valid_soldier_target(target)) {
		distance = target->ob_x - obp->ob_x;
		obp->ob_orient = distance < 0;
		if (obp->ob_bombs > 0 && abs(distance) >= 30 &&
		    abs(distance) <= 60) {
			init_ground_grenade(obp);
		}
		if (abs(distance) <= 80) {
			init_ground_shot(obp, distance < 0 ? -1 : 1);
		}
		if (abs(distance) > 24) {
			obp->ob_dx = distance < 0 ? -WALKER_SPEED : WALKER_SPEED;
			frame = 1 + ((countmove / 4) & 1);
		}
	} else {
		obp->ob_target = NULL;
	}
	movexy(obp, &distance, &orient);
	obp->ob_y =
	    clamp_max(ground[obp->ob_x] + obp->ob_symbol->h - 1,
	              SCR_HGHT - 1);
	obp->ob_ly = obp->ob_dy = 0;
	// sym[4] is a vertical flip used by aircraft, not a left-facing
	// humanoid. Keep soldiers upright in both movement directions.
	orient = 0;
	obp->ob_symbol = &symbol_walker[frame].sym[orient];
	return true;
}

bool move_ally_soldier(OBJECTS *obp)
{
	OBJECTS *target;
	int x, y, frame, distance;

	if (obp->ob_state == FINISHED) {
		deallobj(obp);
		return false;
	}
	if (obp->ob_bdelay > 0) {
		--obp->ob_bdelay;
	}
	target = find_enemy_soldier(obp, 140);
	obp->ob_dx = 0;
	frame = 0;
	if (target != NULL) {
		distance = target->ob_x - obp->ob_x;
		obp->ob_orient = distance < 0;
		if (abs(distance) <= 80) {
			init_ground_shot(obp, distance < 0 ? -1 : 1);
		}
		if (abs(distance) > 24) {
			obp->ob_dx = distance < 0 ? -1 : 1;
			frame = 1 + ((countmove / 4) & 1);
		}
	} else {
		frame = 1 + ((countmove / 6) & 1);
		obp->ob_dx = ((countmove / 80) & 1) ? 1 : -1;
	}
	movexy(obp, &x, &y);
	obp->ob_y =
	    clamp_max(ground[obp->ob_x] + obp->ob_symbol->h - 1,
	              SCR_HGHT - 1);
	obp->ob_dy = obp->ob_ly = 0;
	obp->ob_symbol = &symbol_walker[frame].sym[0];
	return true;
}

static int car_ground_y(OBJECTS *car)
{
	int x;
	int result = 0;
	int xmax = clamp_max(currgame->gm_max_x - 1,
	                     car->ob_x + car->ob_symbol->w - 1);

	for (x = car->ob_x; x <= xmax; ++x) {
		result = imax(result, ground[x]);
	}
	return result + car->ob_symbol->h - 1;
}

static bool move_hidden_car(OBJECTS *ob)
{
	return false;
}

static bool try_enter_car(OBJECTS *walker)
{
	OBJECTS *vehicle;

	for (vehicle = objtop; vehicle != NULL; vehicle = vehicle->ob_next) {
		if (vehicle->ob_type != TARGET ||
		    vehicle->ob_state != STANDING ||
		    (vehicle->ob_orient != TARGET_TRUCK &&
		     vehicle->ob_orient != TARGET_TANKER_TRUCK) ||
		    abs(vehicle->ob_x - walker->ob_x) > 12 ||
		    abs(vehicle->ob_y - walker->ob_y) > 16) {
			continue;
		}

		deletex(vehicle);
		vehicle->ob_state = WAITING;
		vehicle->ob_onmap = false;
		vehicle->ob_movef = move_hidden_car;

		walker->ob_type = CAR;
		walker->ob_state = FLYING;
		walker->ob_movef = move_car;
		walker->ob_target = vehicle;
		walker->ob_angle = vehicle->ob_orient;
		walker->ob_orient = 0;
		walker->ob_symbol = &symbol_targets[walker->ob_angle].sym[0];
		walker->ob_dx = walker->ob_dy = walker->ob_lx = walker->ob_ly =
		    walker->ob_ldx = walker->ob_ldy = 0;
		walker->ob_y = car_ground_y(walker);
		updateobjpos(walker);
		return true;
	}
	return false;
}

static void exit_car(OBJECTS *car)
{
	OBJECTS *vehicle = car->ob_target;
	int i;

	for (i = 0; i < num_planes; ++i) {
		if (planes[i]->ob_target == car) {
			planes[i]->ob_target = NULL;
		}
	}
	stopsound(car);

	vehicle->ob_type = TARGET;
	vehicle->ob_state = STANDING;
	vehicle->ob_movef = movetarg;
	vehicle->ob_x = car->ob_x;
	vehicle->ob_orient = car->ob_angle;
	vehicle->ob_symbol = &symbol_targets[vehicle->ob_orient]
	                         .sym[vehicle->ob_original_ob->transform];
	vehicle->ob_y = car_ground_y(vehicle);
	vehicle->ob_onmap = true;
	insertx(vehicle, &topobj);

	car->ob_type = WALKER;
	car->ob_state = WALKER_ABANDONED;
	car->ob_movef = move_walker;
	car->ob_target = NULL;
	car->ob_soundf = NULL;
	car->ob_symbol = &symbol_walker[0].sym[0];
	car->ob_dx = car->ob_dy = car->ob_lx = car->ob_ly = car->ob_ldx =
	    car->ob_ldy = 0;
	car->ob_y = ground[car->ob_x] + car->ob_symbol->h - 1;
	updateobjpos(car);
}

void kill_car_driver(OBJECTS *car)
{
	exit_car(car);
	car->ob_hitcount = 0;
	car->ob_state = FINISHED;
}

OBJECTS *eject_driver_and_destroy_car(OBJECTS *car)
{
	OBJECTS *vehicle = car->ob_target;

	exit_car(car);
	vehicle->ob_state = FINISHED;
	vehicle->ob_onmap = false;
	return vehicle;
}

bool move_car(OBJECTS *obp)
{
	int key, x, y;

	if (obp->ob_endsts != PLAYING) {
		--endcount;
		if (endcount <= 0) {
			if (playmode != PLAYMODE_ASYNCH && !quit) {
				swrestart();
				return true;
			}
			swend(NULL, true);
		}
		return true;
	}
	obp->ob_soundf = CarSoundCallback;
	key = latest_player_commands[obp->ob_plrnum][countmove % MAX_NET_LAG];
	if (key & K_FLIP) {
		exit_car(obp);
		return true;
	}

	obp->ob_dx = 0;
	if (key & K_FLAPU) {
		obp->ob_dx = -2;
		obp->ob_orient = 1;
	} else if (key & K_FLAPD) {
		obp->ob_dx = 2;
		obp->ob_orient = 0;
	}
	movexy(obp, &x, &y);
	obp->ob_y = car_ground_y(obp);
	obp->ob_dy = obp->ob_ly = 0;
	obp->ob_symbol = &symbol_targets[obp->ob_angle].sym[0];
	return true;
}

bool move_walker(OBJECTS *obp)
{
	int key;
	int x, y;
	int grnd_y;
	int walk_frame = 0;
	int walk_orient;

	if (obp->ob_state == FINISHED && obp->ob_hitcount <= 0) {
		if (playmode != PLAYMODE_ASYNCH &&
		    playmode != PLAYMODE_BATTLEFIELD) {
			++obp->ob_crashcnt;
		}
		if (playmode != PLAYMODE_ASYNCH &&
		    playmode != PLAYMODE_BATTLEFIELD &&
		    obp->ob_crashcnt >= maxcrash) {
			loser(obp);
		} else {
			// Make initpln() treat this as a fresh life and replenish the
			// replacement plane's fuel and ammunition.
			clear_owned_ordnance(obp);
			obp->ob_state = CRASHED;
			initplyr(obp, obp->ob_original_ob);
			obp->ob_movef = moveplyr;
			obp->ob_control_delay = 18;
			initdisp(true);
			return true;
		}
	}

	if (obp->ob_endsts != PLAYING) {
		--endcount;
		if (endcount <= 0) {
			if (playmode != PLAYMODE_ASYNCH && !quit) {
				swrestart();
				return true;
			}
			swend(NULL, true);
		}
		return true;
	}

	x = obp->ob_x;
	y = obp->ob_y;

	// clamp x to valid range
	if (x < 0)
		x = 0;
	if (x >= currgame->gm_max_x)
		x = currgame->gm_max_x - 1;
	obp->ob_x = (unsigned short)x;

	// get ground Y at current position
	grnd_y = (int)ground[x] + obp->ob_symbol->h - 1;

	// get input
	key = latest_player_commands[obp->ob_plrnum][countmove % MAX_NET_LAG];
	if (obp->ob_bdelay > 0) {
		--obp->ob_bdelay;
	}
	if (key & K_SHOT) {
		init_ground_shot(obp, obp->ob_orient ? -1 : 1);
	}
	if (key & K_BOMB) {
		init_ground_grenade(obp);
	}
	if ((key & K_FLIP) && try_enter_car(obp)) {
		return true;
	}

	// handle jump
	if ((key & K_FLIP) && y <= grnd_y && obp->ob_dy <= 0) {
		obp->ob_dy = 4;
	}

	// apply gravity
	if (y > grnd_y || obp->ob_dy > 0) {
		--obp->ob_dy;
	}

	// horizontal movement
	if (key & K_FLAPU) {
		obp->ob_dx = -WALKER_SPEED;
		obp->ob_ldx = 0;
		obp->ob_orient = 1;
		walk_frame = 1 + ((countmove / 4) & 1);
	} else if (key & K_FLAPD) {
		obp->ob_dx = WALKER_SPEED;
		obp->ob_ldx = 0;
		obp->ob_orient = 0;
		walk_frame = 1 + ((countmove / 4) & 1);
	} else {
		obp->ob_dx = 0;
		obp->ob_ldx = 0;
	}

	// move
	movexy(obp, &x, &y);

	// re-clamp x for ground check after movement
	if (x < 0)
		x = 0;
	if (x >= currgame->gm_max_x)
		x = currgame->gm_max_x - 1;
	grnd_y = (int)ground[x] + obp->ob_symbol->h - 1;

	// re-clamp y to ground
	if (y <= grnd_y) {
		obp->ob_y = grnd_y;
		obp->ob_ly = 0;
		obp->ob_dy = 0;
	}

	// update animation frame
	// The alternate orientation in a symset is vertically flipped.
	// Direction still controls shooting and throwing, but the pilot
	// must always use an upright sprite.
	walk_orient = 0;
	if (obp->ob_symbol != &symbol_walker[walk_frame].sym[walk_orient]) {
		obp->ob_symbol = &symbol_walker[walk_frame].sym[walk_orient];
	}

	// check if reached home base
	if (abs(obp->ob_x - obp->ob_original_ob->x) <= WALKER_SPEED) {
		clear_owned_ordnance(obp);
		obp->ob_state = CRASHED;
		initplyr(obp, obp->ob_original_ob);
		obp->ob_movef = moveplyr;
		obp->ob_control_delay = 18;
		initdisp(true);
		return true;
	}

	return true;
}

//
// 2003-02-14: Code was checked into version control; no further entries
// will be added to this log.
//
// sdh 14/2/2003: change license header to GPL
//                autohome on harry keys mode fixed.
// sdh 27/06/2002: move to new sopsym_t for symbols
// sdh 26/03/2002: change CGA_ to Vid_
// sdh 27/10/2001: fix refueling i broke with the guages change yesterday
// sdh 26/10/2001: use new dispguages function
// sdh 21/10/2001: use new obtype_t and obstate_t
// sdh 21/10/2001: reformatted with indent. edited some code by hand to
//                 make it more readable
// sdh 19/10/2001: removed all externs, these are now in headers
//                 shuffled some functions around to shut up compiler
// sdh 18/10/2001: converted all functions in this file to ANSI-style arguments
//
// 87-04-09        Delay between starbursts.
// 87-04-04        Missile and starburst support.
// 87-04-01        Missiles.
// 87-03-31        Allow wounded plane to fly home
// 87-03-30        Novice Player
// 87-03-12        Computer plane heads home at end.
// 87-03-12        Prioritize bombs/shots over flaps.
// 87-03-12        Proper ASYCHRONOUS end of game.
// 87-03-12        Crashed planes stay longer at home.
// 87-03-12        Wounded airplanes.
// 87-03-09        Microsoft compiler.
// 85-10-31        Atari
// 84-02-07        Development
//
