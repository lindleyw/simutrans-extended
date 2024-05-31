/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>

#include "../simdebug.h"

#include "tunnelbauer.h"

#include "../gui/minimap.h"

#include "../simworld.h"
#include "../player/simplay.h"
#include "../player/finance.h"
#include "../simtool.h"

#include "../descriptor/tunnel_desc.h"

#include "../boden/tunnelboden.h"
#include "../boden/wege/strasse.h"

#include "../dataobj/scenario.h"
#include "../dataobj/environment.h"
#include "../dataobj/marker.h"

#include "../obj/tunnel.h"
#include "../obj/leitung2.h"
#include "../obj/signal.h"
#include "../obj/wayobj.h"

#include "../gui/messagebox.h"
#include "../gui/tool_selector.h"

#include "wegbauer.h"
#include "../tpl/stringhashtable_tpl.h"
#include "../tpl/vector_tpl.h"
#include "../world/terraformer.h"


karte_ptr_t tunnel_builder_t::welt;

static stringhashtable_tpl<tunnel_desc_t *, N_BAGS_MEDIUM> tunnel_by_name;


void tunnel_builder_t::register_desc(tunnel_desc_t *desc)
{
	// avoid duplicates with same name
	if( const tunnel_desc_t *old_desc = tunnel_by_name.remove(desc->get_name()) ) {
		dbg->doubled( "tunnel", desc->get_name() );
		tool_t::general_tool.remove( old_desc->get_builder() );
		delete old_desc->get_builder();
		// we cannot delete old_desc, since then xref-resolving will crash
	}
	// add the tool
	tool_build_tunnel_t *tool = new tool_build_tunnel_t();
	tool->set_icon( desc->get_cursor()->get_image_id(1) );
	tool->cursor = desc->get_cursor()->get_image_id(0);
	tool->set_default_param( desc->get_name() );
	tool_t::general_tool.append( tool );
	desc->set_builder( tool );
	tunnel_by_name.put(desc->get_name(), desc);
}

stringhashtable_tpl <tunnel_desc_t *, N_BAGS_MEDIUM> * tunnel_builder_t::get_all_tunnels()
{
	return &tunnel_by_name;
}

const tunnel_desc_t *tunnel_builder_t::get_desc(const char *name)
{
	return (name ? tunnel_by_name.get(name) : NULL);
}


/**
 * Find a matching tunnel
 */
const tunnel_desc_t *tunnel_builder_t::get_tunnel_desc(const waytype_t wtyp, const sint32 min_speed, const uint16 time)
{
	const tunnel_desc_t *find_desc = NULL;

	for(auto const& i : tunnel_by_name) {
		tunnel_desc_t* const desc = i.value;
		if(  desc->get_waytype()==wtyp  ) {
			if(  desc->is_available(time)  ) {
				if(  find_desc==NULL  ||
					(find_desc->get_topspeed()<min_speed  &&  find_desc->get_topspeed()<desc->get_topspeed())  ||
					(desc->get_topspeed()>=min_speed  &&  desc->get_maintenance()<find_desc->get_maintenance())
				) {
					find_desc = desc;
				}
			}
		}
	}
	return find_desc;
}


static bool compare_tunnels(const tunnel_desc_t* a, const tunnel_desc_t* b)
{
	int cmp = a->get_topspeed() - b->get_topspeed();
	if(cmp==0) {
		cmp = (int)a->get_intro_year_month() - (int)b->get_intro_year_month();
	}
	if(cmp==0) {
		cmp = strcmp(a->get_name(), b->get_name());
	}
	return cmp<0;
}


/**
 * Fill menu with icons of given waytype
 */
void tunnel_builder_t::fill_menu(tool_selector_t* tool_selector, const waytype_t wtyp, sint16 /*sound_ok*/)
{
	// check if scenario forbids this
	if (!welt->get_scenario()->is_tool_allowed(welt->get_active_player(), TOOL_BUILD_TUNNEL | GENERAL_TOOL, wtyp)) {
		return;
	}

	const uint16 time=welt->get_timeline_year_month();
	vector_tpl<const tunnel_desc_t*> matching(tunnel_by_name.get_count());

	for(auto const& i : tunnel_by_name) {
		tunnel_desc_t* const desc = i.value;
		if(  desc->get_waytype()==wtyp  &&  desc->is_available(time)  ) {
			matching.insert_ordered(desc, compare_tunnels);
		}
	}
	// now sorted ...
	for(tunnel_desc_t const* const i : matching) {
		tool_selector->add_tool_selector(i->get_builder());
	}
}


/* now construction stuff */


koord3d tunnel_builder_t::find_end_pos(player_t *player, koord3d pos, koord zv, const tunnel_desc_t *desc, bool full_tunnel, const char** msg)
{
	const grund_t *gr;
	leitung_t *lt;
	waytype_t waytyp = desc->get_waytype();
	// use the is_allowed_step routine of way_builder_t, needs an instance
	way_builder_t bauigel(player);
	bauigel.init_builder( (desc->get_is_half_height() ? way_builder_t::low_clearence_flag : (way_builder_t::bautyp_t)0) |  way_builder_t::tunnel_flag | (way_builder_t::bautyp_t)waytyp, way_builder_t::weg_search( waytyp, 1, 0, type_flat ), desc);
	sint32 dummy;

	bool firstTile=true;

	while(true) {
		pos = pos + zv;
		if(!welt->is_within_limits(pos.get_2d())) {
			return koord3d::invalid;
		}

		// check if ground is below tunnel level
		gr = welt->lookup_kartenboden(pos.get_2d());

		// steep slopes and we are appearing at the top of one
		if(  gr->get_hoehe() == pos.z-1  &&  env_t::pak_height_conversion_factor==1  ) {
			const slope_t::type new_slope = slope_type(-zv);
			sint8 hsw = pos.z + corner_sw(new_slope);
			sint8 hse = pos.z + corner_se(new_slope);
			sint8 hne = pos.z + corner_ne(new_slope);
			sint8 hnw = pos.z + corner_nw(new_slope);

			terraformer_t raise(terraformer_t::raise, welt);
			raise.add_node(pos.x, pos.y, hsw, hse, hne, hnw);
			raise.generate_affected_tile_list();

			// if we can adjust height here we can build an entrance so don't need checks below
			return pos;
		}

		if(  gr->get_hoehe() < pos.z  ){
			return koord3d::invalid;
		}

		// check water level
		if (gr->is_water()  &&  welt->lookup_hgt(pos.get_2d()) <= pos.z) {
			return koord3d::invalid;
		}

		if (const char* err = welt->get_scenario()->is_work_allowed_here(player, TOOL_BUILD_TUNNEL|GENERAL_TOOL, waytyp, pos)) {
			if (msg) {
				*msg = err;
			}
			return koord3d::invalid;
		}

		// next tile
		gr = welt->lookup_with_checking_down_way_slope(pos);
		if(  !gr  &&  env_t::pak_height_conversion_factor==2 && !desc->get_is_half_height() ) {
			// check for one above
			gr = welt->lookup(pos + koord3d(0,0,1));
		}

		if(gr) {
			// if there is a tunnel try to connect
			if(  gr->ist_tunnel() ) {
				if(  gr->get_vmove(ribi_type(-zv))!=pos.z) {
					// wrong slope
					return koord3d::invalid;
				}
				// fake tunnel tile
				tunnelboden_t from(pos - zv, slope_t::flat);
				if (bauigel.is_allowed_step(&from, gr, &dummy)) {
					return gr->get_pos();
				}
				else {
					return koord3d::invalid;
				}
			}
			const uint8 slope = gr->get_grund_hang();
			const slope_t::type new_slope = slope_type(-zv) * (welt->get_settings().get_way_height_clearance() == 2 && !desc->get_is_half_height() ? 2 : 1);

			if(  gr->ist_karten_boden()  &&  ( slope!=new_slope  ||  pos.z!=gr->get_pos().z )  ) {
				// lower terrain to match - most of time shouldn't need to raise
				// however player might have manually altered terrain so check this anyway
				sint8 hsw = pos.z + corner_sw(new_slope);
				sint8 hse = pos.z + corner_se(new_slope);
				sint8 hne = pos.z + corner_ne(new_slope);
				sint8 hnw = pos.z + corner_nw(new_slope);

				terraformer_t raise(terraformer_t::raise, welt);
				terraformer_t lower(terraformer_t::lower, welt);

				raise.add_node(pos.x, pos.y, hsw, hse, hne, hnw);
				lower.add_node(pos.x, pos.y, hsw, hse, hne, hnw);

				raise.generate_affected_tile_list();
				lower.generate_affected_tile_list();

				if (!player || raise.can_raise_all(player, player->is_public_service())!= NULL || lower.can_lower_all(player, player->is_public_service())!=NULL) {
					// returned non-null therefore error reported
					return koord3d::invalid;
				}

				// if we can adjust height here we can build an entrance so don't need checks below
				return pos;
			}


			if(  gr->get_typ() != grund_t::boden  ||  slope != new_slope  ||  gr->is_halt()  ||  ((waytyp != powerline_wt) ? gr->get_leitung() != NULL : gr->hat_wege())  ) {
				// must end on boden_t and correct slope and not on halts
				// ways cannot end on powerlines, powerlines cannot end on ways
				return koord3d::invalid;
			}
			if(  gr->has_two_ways()  &&  waytyp != road_wt  ) {
				// Only road tunnels allowed here.
				return koord3d::invalid;
			}

			ribi_t::ribi ribi = 0;
			if(waytyp != powerline_wt) {
				ribi = gr->get_weg_ribi_unmasked(waytyp);
			}
			else {
				if(gr->get_leitung()) {
					ribi = gr->get_leitung()->get_ribi();
				}
			}

			if(  ribi && koord(ribi) == zv  ) {
				// There is already a way (with correct ribi)
				return pos;
			}
			if(  !ribi  ) {
				// End of the slope - Missing end rail or has no ribis
				// we still consider if we interfere with a way
				if(waytyp != powerline_wt) {
					if(  !gr->hat_wege()  ||  gr->hat_weg(waytyp)  ) {
						return pos;
					}
				}
				else {
					lt = gr->find<leitung_t>();
					if(!gr->hat_wege() || lt) {
						return pos;
					}
				}
			}
			return koord3d::invalid;  // Was im Weg (slope hillside or so)
		}

		// stop if we only want to check tile behind tunnel mouth
		if (!full_tunnel) {
			return pos;
		}

		//verify permissions to build tunnel
		if(!gr && !firstTile){
			tunnelboden_t from(pos - zv,slope_t::flat);
			tunnelboden_t to(pos, slope_t::flat);
			if(!bauigel.is_allowed_step(&from,&to,&dummy)){
				return koord3d::invalid;
			}
		}
		firstTile=false;

		// All free - keep looking
	}
}


const char *tunnel_builder_t::build( player_t *player, koord pos, const tunnel_desc_t *desc, bool full_tunnel, overtaking_mode_t overtaking_mode, const way_desc_t *way_desc)
{
	assert( desc );

	const grund_t *gr = welt->lookup_kartenboden(pos);
	if(gr==NULL) {
		return "Tunnel must start on single way!";
	}

	koord zv;
	const waytype_t waytyp = desc->get_waytype();
	const slope_t::type slope = gr->get_grund_hang();

	if(  waytyp != powerline_wt  ) {
		const weg_t *weg = gr->get_weg(waytyp);

		if(  gr->get_typ() != grund_t::boden  ||  gr->is_halt()  ||  gr->get_leitung()) {
			return "Tunnel must start on single way!";
		}
		// If there is a way on this tile, it must have the right ribis.
		if(  weg  &&  (weg->get_ribi_unmasked() & ~ribi_t::backward( ribi_type(slope) ))  ) {
			return "Tunnel must start on single way!";
		}
	}
	else {
		leitung_t *lt = gr->find<leitung_t>();
		if(  gr->get_typ() != grund_t::boden  ||  gr->hat_wege()  ) {
			return "Tunnel must start on single way!";
		}
		if(  lt  &&  (lt->get_ribi() & ~ribi_t::backward( ribi_type(slope) ))  ) {
			return "Tunnel must start on single way!";
		}
	}
	if(  !slope_t::is_single(slope)  ) {
		return "Tunnel muss an\neinfachem\nHang beginnen!\n";
	}

	// for conversion factor 1, must be single height, for conversion factor 2, must be double
	if(  !desc->check_way_slope(slope)  ) {
		return "Tunnel muss an\neinfachem\nHang beginnen!\n";
	}

	if(  gr->has_two_ways()  &&  waytyp != road_wt  ) {
		return "Tunnel must start on single way!";
	}
	zv = koord(slope);

	// Search tunnel end and check intermediate tiles
	const char *err = NULL;
	koord3d end = koord3d::invalid;

	if(player && !player->can_afford(get_total_cost(gr->get_pos(),desc)))
	{
		return "That would exceed\nyour credit limit.";
	}
	else {
		end = find_end_pos(player, gr->get_pos(), zv, desc, full_tunnel, &err);
		if (err) {
			return err;
		}
	}


	if(!welt->is_within_limits(end.get_2d())) {
		return "Tunnel must start on single way!";
	}

	// check ownership
	const grund_t *end_gr = welt->lookup(end);
	if (end_gr) {
		if (weg_t *weg_end = end_gr->get_weg(waytyp)) {
			if (weg_end->is_deletable(player)!=NULL) {
				return NOTICE_OWNED_BY_OTHER_PLAYER;
			}
			if(  full_tunnel  &&  end_gr->get_typ() == grund_t::tunnelboden  ) {
				full_tunnel = false;
			}
		}
	}

	// Begin and end found, we can build

	slope_t::type end_slope = slope_type(-zv);
	if( env_t::pak_height_conversion_factor == 2 && !desc->get_is_half_height()){
		end_slope *= 2;
	}
	if(  full_tunnel  &&  (!end_gr  ||  end_gr->get_grund_hang()!=end_slope)  ) {
		// end slope not at correct height - we have already checked in find_end_pos that we can change this
		sint8 hsw = end.z + corner_sw(end_slope);
		sint8 hse = end.z + corner_se(end_slope);
		sint8 hne = end.z + corner_ne(end_slope);
		sint8 hnw = end.z + corner_nw(end_slope);

		int n = 0;

		terraformer_t raise(terraformer_t::raise, welt);
		terraformer_t lower(terraformer_t::lower, welt);

		raise.add_node(end.x, end.y, hsw, hse, hne, hnw);
		lower.add_node(end.x, end.y, hsw, hse, hne, hnw);

		raise.generate_affected_tile_list();
		lower.generate_affected_tile_list();

		if (err) return 0;

// TODO: this is rather hackish as 4 seems to come from nowhere but works most of the time
// feel free to change if you have a better idea!
		n = (raise.apply() + lower.apply()) / 4;
		player_t::book_construction_costs(player, welt->get_settings().cst_alter_land * n, end.get_2d(), desc->get_waytype());
	}

	if(!build_tunnel(player, gr->get_pos(), end, zv, desc, overtaking_mode, way_desc)) {
		return "Ways not connected";
	}

	if(desc->get_waytype() == road_wt)
	{
		welt->set_recheck_road_connexions();
	}
	return NULL;
}


bool tunnel_builder_t::build_tunnel(player_t *player, koord3d start, koord3d end, koord zv, const tunnel_desc_t *desc, overtaking_mode_t overtaking_mode, const way_desc_t *way_desc)
{
	ribi_t::ribi ribi = 0;
	koord3d pos = start;
	sint64 cost = 0;
	waytype_t waytyp = desc->get_waytype();

	DBG_MESSAGE("tunnel_builder_t::build()","build from (%d,%d,%d) to (%d,%d,%d) ", pos.x, pos.y, pos.z, end.x, end.y, end.z );

	// now we search for a matching way for the tunnel's top speed
	// The tunnel ways are no longer properly encoded, with the result that way_desc is garbled
	// when fetched here and crashes the game. Since tunnel ways are deprecated in Extended,
	// and this bug is likely to be hard to fix, disalbe this for time time being.
	//@jamespetts January 2017
	/*if(way_desc == NULL)
	{
		way_desc = desc->get_way_desc();
	}*/

	if (!env_t::networkmode && way_desc == NULL)
	{
		// The last selected way will not have been set if this is not in network mode.
		way_desc = tool_build_way_t::defaults[waytyp & 63];
	}

	if(way_desc == NULL || way_desc->get_styp() != type_flat)
	{
		way_desc = way_builder_t::weg_search(waytyp, desc->get_topspeed(), desc->get_max_axle_load(), welt->get_timeline_year_month(), type_flat, desc->get_wear_capacity());
	}

	build_tunnel_portal(player, pos, zv, desc, way_desc, cost, start != end, overtaking_mode, true);

	ribi = ribi_type(-zv);

	// move on
	pos = pos + zv;

	// calc back image to remove wall blocking tunnel portal for active underground view
	if(grund_t::underground_mode) {
		grund_t *gr = welt->lookup_kartenboden(pos.get_2d());
		gr->calc_image();
		gr->set_flag(grund_t::dirty);
	}

	if(  end == start  ) {
		// already finished
		return true;
	}

	// if end is tunnel then connect
	grund_t *gr_end = welt->lookup(end);
	if (gr_end) {
		if (gr_end->ist_tunnel()) {
			gr_end->weg_erweitern(desc->get_waytype(), ribi);
		}
		else if (gr_end->ist_karten_boden()) {
			// if end is above ground construct an exit
			build_tunnel_portal(player, end, -zv, desc, way_desc, cost, true, overtaking_mode, false);
			gr_end = NULL; // invalid - replaced by tunnel ground
			// calc new back image for the ground
			if (end!=start && grund_t::underground_mode) {
				grund_t *gr = welt->lookup_kartenboden(pos.get_2d()-zv);
				gr->calc_image();
				gr->set_flag(grund_t::dirty);
			}
		}
		else {
			// good luck
			assert(0);
		}
	}

	//build way in tunnel
	way_builder_t builder(player);
	builder.init_builder((way_builder_t::bautyp_t)desc->get_waytype()  | way_builder_t::tunnel_flag | (desc->get_is_half_height() ? way_builder_t::low_clearence_flag : (way_builder_t::bautyp_t)0),way_desc,desc);
	builder.calc_straight_route(start,end);
	builder.set_overtaking_mode(overtaking_mode);
	builder.build();

	player_t::book_construction_costs(player, -cost, (start-zv).get_2d(), desc->get_waytype());
	return true;
}


void tunnel_builder_t::build_tunnel_portal(player_t *player, koord3d end, koord zv, const tunnel_desc_t *desc, const way_desc_t *way_desc, sint64 &cost, bool connect_inside, overtaking_mode_t overtaking_mode, bool beginning)
{
	grund_t *alter_boden = welt->lookup(end);
	ribi_t::ribi ribi = 0;
	if(desc->get_waytype()!=powerline_wt) {
		ribi = alter_boden->get_weg_ribi_unmasked(desc->get_waytype());
	}
	if (connect_inside) {
		ribi |= ribi_type(zv);
	}

	tunnelboden_t *tunnel = new tunnelboden_t( end, alter_boden->get_grund_hang());
	tunnel->obj_add(new tunnel_t(end, player, desc));

	weg_t *weg = NULL;
	if(desc->get_waytype()!=powerline_wt) {
		weg = alter_boden->get_weg( desc->get_waytype() );
	}
	// take care of everything on that tile ...
	tunnel->take_obj_from( alter_boden );
	welt->access(end.get_2d())->kartenboden_setzen( tunnel );
	if(desc->get_waytype() != powerline_wt) {
		if(weg) {
			// has already a way
			tunnel->weg_erweitern(desc->get_waytype(), ribi);
		}
		else {
			// needs still one
			weg = weg_t::alloc( desc->get_waytype() );
			if(  way_desc  ) {
				weg->set_desc( way_desc );
			}
			else
			{
				// set_desc will set the maintenance cost of this way.
				player_t::add_maintenance( player, -weg->get_desc()->get_maintenance(), weg->get_desc()->get_finance_waytype() );
			}
			tunnel->neuen_weg_bauen( weg, ribi, player );
		}
		const grund_t* gr = welt->lookup(weg->get_pos());
		const slope_t::type hang = gr ? gr->get_weg_hang() : slope_t::flat;
		if(hang != slope_t::flat)
		{
			const uint slope_height = (hang & 7) ? 1 : 2;
			if(slope_height == 1)
			{
				weg->set_max_speed(desc->get_topspeed_gradient_1());
			}
			else
			{
				weg->set_max_speed(desc->get_topspeed_gradient_2());
			}
		}
		else
		{
			weg->set_max_speed(desc->get_topspeed());
		}
		weg->set_max_axle_load( desc->get_max_axle_load() );
		if(  desc->get_waytype()==road_wt  ) {
			strasse_t* str = (strasse_t*)weg;
			assert(weg);
			str->set_overtaking_mode(overtaking_mode, player);
			if(  desc->get_waytype()==road_wt  &&  overtaking_mode<=oneway_mode  ) {
				if(  beginning  ) {
					str->set_ribi_mask_oneway(ribi_type(-zv));
				} else {
					str->set_ribi_mask_oneway(ribi_type(zv));
				}
			}
		}

	}
	else {
		leitung_t *lt = tunnel->get_leitung();
		if(!lt) {
			lt = new leitung_t(tunnel->get_pos(), player);
			lt->set_desc(way_desc);
			tunnel->obj_add( lt );
			player_t::add_maintenance( player, -way_desc->get_maintenance(), powerline_wt );
		}
		else {
			// subtract twice maintenance: once for the already existing powerline
			// once since leitung_t::finish_rd will add it again
			player_t::add_maintenance( player, -2*lt->get_desc()->get_maintenance(), powerline_wt );
		}
		lt->finish_rd();
	}

	// remove sidewalk
	weg_t *str = tunnel->get_weg( road_wt );
	if( str  &&  str->hat_gehweg()) {
		str->set_gehweg(false);
	}

	tunnel->calc_image();
	tunnel->set_flag(grund_t::dirty);

	// Auto-connect to a way outside the new tunnel mouth
	grund_t *ground_outside = welt->lookup(end-zv);
	if( !ground_outside ) {
		ground_outside = welt->lookup(end-zv+koord3d(0,0,-1));
		if(  ground_outside  &&  ground_outside->get_grund_hang() != tunnel->get_grund_hang()  ) {
			// Not the correct slope.
			ground_outside = NULL;
		}
	}
	if( ground_outside) {
		weg_t *way_outside = ground_outside->get_weg( desc->get_waytype() );
		if( way_outside ) {
			// use the check_owner routine of way_builder_t (not player_t!), needs an instance
			way_builder_t bauigel(player);
			bauigel.init_builder( (way_builder_t::bautyp_t)desc->get_waytype(), way_outside->get_desc());
			sint32 dummy;
			if(bauigel.is_allowed_step(tunnel, ground_outside, &dummy)) {
				tunnel->weg_erweitern(desc->get_waytype(), ribi_type(-zv));
				ground_outside->weg_erweitern(desc->get_waytype(), ribi_type(zv));
			}
		}
		if (desc->get_waytype()==water_wt  &&  ground_outside->is_water()) {
			// connect to the sea
			tunnel->weg_erweitern(desc->get_waytype(), ribi_type(-zv));
			ground_outside->calc_image(); // to recalculate ribis
		}
	}

	player_t::add_maintenance( player,  get_total_maintenance(end,desc), desc->get_finance_waytype() );
	cost += (sint64)get_total_cost(end,desc);
}


const char *tunnel_builder_t::remove(player_t *player, koord3d start, waytype_t waytyp, bool remove_all )
{
	marker_t& marker = marker_t::instance(welt->get_size().x, welt->get_size().y, karte_t::marker_index);
	slist_tpl<koord3d>  end_list;
	slist_tpl<koord3d>  part_list;
	slist_tpl<koord3d>  tmp_list;
	koord3d   pos = start;

	// First check if all tunnel parts can be removed
	tmp_list.insert(pos);
	grund_t *from = welt->lookup(pos);
	marker.mark(from);
	waytype_t delete_waytyp = waytyp==powerline_wt ? invalid_wt : waytyp;

	do {
		pos = tmp_list.remove_first();

		grund_t *from = welt->lookup(pos);
		grund_t *to;
		koord zv = koord::invalid;

		if(from->ist_karten_boden()) {
			// Der Grund ist Tunnelanfang/-ende - hier darf nur in
			// eine Richtung getestet werden.
			zv = koord(from->get_grund_hang());
			end_list.insert(pos);
		}
		else {
			part_list.insert(pos);
		}

		if(  from->kann_alle_obj_entfernen(player)  ) {
			return "Der Tunnel ist nicht frei!\n";
		}

		ribi_t::ribi waytype_ribi = ribi_t::none;
		if(  waytyp == powerline_wt  ) {
			if(  from->get_leitung()  ) {
				waytype_ribi = from->get_leitung()->get_ribi();
			}
		}
		else {
			waytype_ribi = from->get_weg_ribi_unmasked(delete_waytyp);
		}
		if(  !remove_all  &&  ribi_t::is_threeway(waytype_ribi)  ) {
			return "This tunnel branches. You can try Control+Click to remove.";
		}

		// Nachbarn raussuchen
		for(int r = 0; r < 4; r++) {
			if((zv == koord::invalid || zv == koord::nesw[r]) &&
				from->get_neighbour(to, delete_waytyp, ribi_t::nesw[r]) &&
				!marker.is_marked(to) &&
				(waytyp != powerline_wt || to->get_leitung()))
			{
				tmp_list.insert(to->get_pos());
				marker.mark(to);
			}
		}
	} while (!tmp_list.empty());

	// Now we can delete the tunnel grounds
	while (!part_list.empty()) {
		pos = part_list.remove_first();
		grund_t *gr = welt->lookup(pos);
		// remove the second way first in the tunnel
		if(gr->get_weg_nr(1)) {
			gr->remove_everything_from_way(player,gr->get_weg_nr(1)->get_waytype(),ribi_t::none);
		}
		gr->remove_everything_from_way(player,waytyp,ribi_t::none); // removes stop and signals correctly
		// remove everything else
		gr->obj_loesche_alle(player);
		gr->mark_image_dirty();
		welt->access(pos.get_2d())->boden_entfernen(gr);
		delete gr;

		minimap_t::get_instance()->calc_map_pixel( pos.get_2d() );
	}

	// And now we can delete the tunnel ends
	while (!end_list.empty()) {
		pos = end_list.remove_first();

		grund_t *gr = welt->lookup(pos);
		if(waytyp == powerline_wt) {
			// remove tunnel portals
			tunnel_t *t = gr->find<tunnel_t>();
			if(t) {
				t->cleanup(player);
				delete t;
			}
			if (leitung_t *lt = gr->get_leitung()) {
				// remove single powerlines
				if ( (lt->get_ribi()  & ~ribi_type(gr->get_grund_hang())) == ribi_t::none ) {
					lt->cleanup(player);
					delete lt;
				}
			}
		}
		else {
			ribi_t::ribi mask = gr->get_grund_hang()!=slope_t::flat ? ~ribi_type(gr->get_grund_hang()) : ~ribi_type(slope_t::opposite(gr->get_weg_hang()));

			// remove the second way first in the tunnel
			if(gr->get_weg_nr(1)) {
				gr->remove_everything_from_way(player,gr->get_weg_nr(1)->get_waytype(),gr->get_weg_nr(1)->get_ribi_unmasked() & mask);
			}
			// removes single signals, bridge head, pedestrians, stops, changes catenary etc
			ribi_t::ribi ribi = gr->get_weg_ribi_unmasked(waytyp) & mask;

			tunnel_t *t = gr->find<tunnel_t>();
			uint8 broad_type = t->get_broad_type();
			gr->remove_everything_from_way(player,waytyp,ribi); // removes stop and signals correctly

			// remove tunnel portals
			t = gr->find<tunnel_t>();
			if(t) {
				t->cleanup(player);
				delete t;
			}

			if( broad_type ) {
				slope_t::type hang = gr->get_grund_hang();
				ribi_t::ribi dir = ribi_t::rotate90( ribi_type( hang ) );
				if( broad_type & 1 ) {
					const grund_t *gr_l = welt->lookup(pos + dir);
					tunnel_t* tunnel_l = gr_l ? gr_l->find<tunnel_t>() : NULL;
					if( tunnel_l ) {
						tunnel_l->calc_image();
					}
				}
				if( broad_type & 2 ) {
					const grund_t *gr_r = welt->lookup(pos - dir);
					tunnel_t* tunnel_r = gr_r ? gr_r->find<tunnel_t>() : NULL;
					if( tunnel_r ) {
						tunnel_r->calc_image();
					}
				}
			}

			// corrects the ways
			weg_t *weg=gr->get_weg_nr(0);
			if(weg) {
				// fails if it was previously the last ribi
				weg->set_desc(weg->get_desc());
				weg->set_ribi( ribi );
				if(gr->get_weg_nr(1)) {
					gr->get_weg_nr(1)->set_ribi( ribi );
				}
			}
		}

		// then add the new ground, copy everything and replace the old one
		grund_t *gr_new = new boden_t(pos, gr->get_grund_hang());
		welt->access(pos.get_2d())->kartenboden_setzen(gr_new);

		if(gr_new->get_leitung()) {
			gr_new->get_leitung()->finish_rd();
		}

		// recalc image of ground
		grund_t *kb = welt->access(pos.get_2d()+koord(gr_new->get_grund_hang()))->get_kartenboden();
		kb->calc_image();
		kb->set_flag(grund_t::dirty);
	}
	return NULL;
}

bool tunnel_builder_t::get_is_under_building(koord3d pos, const tunnel_desc_t *desc){
	if(!welt->lookup_kartenboden(pos.get_2d())->get_building()){
		return false;
	}
	if(desc->get_wtyp()==road_wt && desc->get_wtyp()!=water_wt){
		return true;
	}
	//exception to track tunnel beneath the road (allow two subway/tube tunnels beneath city street)
	for(uint8 r=0; r<4; r++){
		if(const grund_t *gr2 = welt->lookup(pos+koord(((ribi_t::ribi)(1<<r))))){
			if(const tunnel_t *t2 = gr2->find<tunnel_t>()){
				if(t2->get_desc()==desc){
					if(weg_t* way = welt->lookup_kartenboden(gr2->get_pos().get_2d())->get_weg_nr(0)){
						if(way->get_waytype()==road_wt){
							return false;
						}
					}
				}
			}
		}
	}
	return true;
}

uint32 tunnel_builder_t::get_total_cost(koord3d pos, const tunnel_desc_t *desc){
	const grund_t *gr = welt->lookup_kartenboden(pos.get_2d());
	uint32 total=desc->get_value();
	if(gr->is_water()){
		total += desc->get_subsea_cost();
	}

	if(get_is_under_building(pos,desc)){
		total += desc->get_subbuilding_cost();
	}

	if(get_is_below_waterline(pos)){
		total += desc->get_subwaterline_cost();
	}

	if(welt->lookup_kartenboden(pos.get_2d())->get_weg_nr(0)){
		total += desc->get_subway_cost();
	}

	uint32 depth = welt->lookup_hgt(pos.get_2d()) - pos.z;
	total += depth * desc->get_depth_cost();
	total += depth * depth * desc->get_depth2_cost();

	return total;
}

uint32 tunnel_builder_t::get_total_maintenance(koord3d pos, const tunnel_desc_t *desc){
	uint32 total=desc->get_maintenance();
	if(welt->lookup_kartenboden(pos.get_2d())->is_water() && desc->get_subsea_allowed()){
		total+=desc->get_subsea_maintenance();
	}
	if(get_is_below_waterline(pos)){
		total+=desc->get_subwaterline_maintenance();
	}
	return total;
}

bool tunnel_builder_t::get_is_below_waterline(koord3d pos){
	return welt->get_water_hgt(pos.get_2d()) > pos.z || welt->get_groundwater() > pos.z;
}
