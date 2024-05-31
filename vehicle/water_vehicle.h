/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef VEHICLE_WATER_VEHICLE_H
#define VEHICLE_WATER_VEHICLE_H


#include "vehicle.h"


/**
 * A class for naval vehicles. Manages the look of the vehicles
 * and the navigability of tiles.
 * @see vehicle_t
 */
class water_vehicle_t : public vehicle_t
{
protected:
	// how expensive to go here (for way search)
	int get_cost(const grund_t *, const sint32, ribi_t::ribi) OVERRIDE { return 1; }

	void calc_drag_coefficient(const grund_t *gr) OVERRIDE;

	bool check_next_tile(const grund_t *bd) const OVERRIDE;

	void enter_tile(grund_t*) OVERRIDE;

public:
	waytype_t get_waytype() const OVERRIDE { return water_wt; }

	bool can_enter_tile(const grund_t *gr_next, sint32 &restart_speed, uint8) OVERRIDE;

	bool check_tile_occupancy(const grund_t* gr);

	// returns true for the way search to an unknown target.
	bool is_target(const grund_t *,const grund_t *) OVERRIDE {return 0;}

	// Use special route checks for oceangoing water vehicles
	route_t::route_result_t calc_route(koord3d start, koord3d end, sint32 max_speed, bool is_tall, route_t* route) OVERRIDE;

	water_vehicle_t(loadsave_t *file, bool is_leading, bool is_last);
	water_vehicle_t(koord3d pos, const vehicle_desc_t* desc, player_t* player, convoi_t* cnv);

#ifdef INLINE_OBJ_TYPE
#else
	obj_t::typ get_typ() const OVERRIDE { return water_vehicle; }
#endif

	schedule_t * generate_new_schedule() const OVERRIDE;

};


#endif
