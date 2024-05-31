/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include <stdio.h>
#include <ctype.h>

#include "../simdebug.h"
#include "../gui/simwin.h"
#include "../simtypes.h"
#include "../simworld.h"
#include "../simhalt.h"
#include "../display/simimg.h"

#include "../utils/cbuffer_t.h"
#include "../gui/messagebox.h"
#include "../descriptor/building_desc.h"
#include "../boden/grund.h"
#include "../obj/gebaeude.h"
#include "../player/simplay.h"
#include "../simdepot.h"
#include "loadsave.h"
#include "translator.h"

#include "schedule.h"

#include "../tpl/slist_tpl.h"

schedule_entry_t schedule_t::dummy_entry(koord3d::invalid, 0, 0, 0, -1, false);

// copy all entries from schedule src to this and adjusts current_stop
void schedule_t::copy_from(const schedule_t *src)
{
	// make sure, we can access both
	if(  src==NULL  ) {
		dbg->fatal("schedule_t::copy_to()","cannot copy from NULL");
	}
	entries.clear();
	for(schedule_entry_t const& i : src->entries) {
		entries.append(i);
	}
	set_current_stop( src->get_current_stop() );

	editing_finished = src->is_editing_finished();
	spacing = src->get_spacing();
	bidirectional = src->is_bidirectional();
	mirrored = src->is_mirrored();
	same_spacing_shift = src->is_same_spacing_shift();
}



bool schedule_t::is_stop_allowed(const grund_t *gr) const
{
	// first: check, if we can go here
	waytype_t const my_waytype = get_waytype();
	bool ok = gr->hat_weg(my_waytype);
	if(  !ok  ) {
		if(  my_waytype==air_wt  ) {
			// everywhere is ok but not on stops (we have to load at airports only ...)
			ok = !gr->get_halt().is_bound();
		}
		else if(  my_waytype==water_wt  &&  gr->get_typ()==grund_t::wasser  ) {
			ok = true;
		}
		else if(  my_waytype==tram_wt  ) {
			// tram rails are track internally
			ok = gr->hat_weg(track_wt);
		}
	}

	if(  ok  ) {
		// ok, we can go here; but we must also check, that we are not entering a foreign depot
		depot_t *dp = gr->get_depot();
		ok &= (dp==NULL  ||  (int)dp->get_tile()->get_desc()->get_extra()==my_waytype);
	}

	return ok;
}



/* returns a valid halthandle if there is a next halt in the schedule;
 * it may however not be allowed to load there, if the owner mismatches!
 */
halthandle_t schedule_t::get_next_halt( player_t *player, halthandle_t halt ) const
{
	if(  entries.get_count()>1  ) {
		for(  uint i=1;  i < entries.get_count();  i++  ) {
			halthandle_t h = haltestelle_t::get_halt( entries[ (current_stop+i) % entries.get_count() ].pos, player );
			if(  h.is_bound()  &&  h != halt  ) {
				return h;
			}
		}
	}
	return halthandle_t();
}


/* returns a valid halthandle if there is a previous halt in the schedule;
 * it may however not be allowed to load there, if the owner mismatches!
 */
halthandle_t schedule_t::get_prev_halt( player_t *player ) const
{
	if(  entries.get_count()>1  ) {
		for(  uint i=1;  i < entries.get_count()-1u;  i++  ) {
			halthandle_t h = haltestelle_t::get_halt( entries[ (current_stop+entries.get_count()-i) % entries.get_count() ].pos, player );
			if(  h.is_bound()  ) {
				return h;
			}
		}
	}
	return halthandle_t();
}


halthandle_t schedule_t::get_origin_halt(player_t *player) const
{
	for (  uint8 i=0; i<entries.get_count(); i++  ) {
		halthandle_t h = haltestelle_t::get_halt(entries[i].pos, player);
		if (h.is_bound()) {
			return h;
		}
	}
	return halthandle_t();
}

halthandle_t schedule_t::get_destination_halt(player_t *player) const
{
	for (  uint8 i=1; i<entries.get_count(); i++  ) {
		halthandle_t h = haltestelle_t::get_halt(entries[entries.get_count()-i].pos, player);
		if (h.is_bound()) {
			return h;
		}
	}
	return halthandle_t();
}



bool schedule_t::insert(const grund_t* gr, uint16 minimum_loading, uint8 waiting_time_shift, sint16 spacing_shift, bool wait_for_time, bool show_failure)
{
	// stored in minivec, so we have to avoid adding too many
	if(entries.get_count() >= 254)
	{
		if(show_failure)
		{
			create_win( new news_img("Maximum 254 stops\nin a schedule!\n"), w_time_delete, magic_none);
		}
		return false;
	}

	if(wait_for_time)
	{
		// "minimum_loading" (wait for load) and wait_for_time are not compatible.
		minimum_loading = 0;
	}

	if(!gr)
	{
		// This can occur in some cases if a depot is not found.
		return false;
	}

	if(  is_stop_allowed(gr)  ) {
		entries.insert_at(current_stop, schedule_entry_t(gr->get_pos(), minimum_loading, waiting_time_shift, spacing_shift, -1, wait_for_time));
		current_stop ++;
		make_current_stop_valid();
		return true;
	}
	else {
		// too many stops or wrong kind of stop
		if(show_failure)
		{
			create_win( new news_img(get_error_msg()), w_time_delete, magic_none);
		}
		return false;
	}
}



bool schedule_t::append(const grund_t* gr, uint16 minimum_loading, uint8 waiting_time_shift, sint16 spacing_shift, bool wait_for_time)
{
	// stored in minivec, so we have to avoid adding too many
	if(entries.get_count()>=254) {
		create_win( new news_img("Maximum 254 stops\nin a schedule!\n"), w_time_delete, magic_none);
		return false;
	}

	if(!gr)
	{
		// This can occur in some cases if a depot is not found.
		return false;
	}

	if(wait_for_time)
	{
		// "minimum_loading" (wait for load) and wait_for_time are not compatible.
		minimum_loading = 0;
	}

	if(is_stop_allowed(gr)) {
		entries.append(schedule_entry_t(gr->get_pos(), minimum_loading, waiting_time_shift, spacing_shift, -1, wait_for_time), 4);
		return true;
	}
	else {
		DBG_MESSAGE("schedule_t::append()","forbidden stop at %i,%i,%i",gr->get_pos().x, gr->get_pos().x, gr->get_pos().z );
		// error
		create_win( new news_img(get_error_msg()), w_time_delete, magic_none);
		return false;
	}
}



// cleanup a schedule
void schedule_t::cleanup()
{

	if(entries.get_count() == 1)
	{
		// Schedules of just one entry are not allowed.
		entries.clear();
	}

	if(  entries.empty()  ) {
		return; // nothing to check
	}

	// first and last must not be the same!
	koord3d lastpos = entries.back().pos;
	// now we have to check all entries ...
	for(  sint32 i=0;  i<entries.get_count();  i++  ) {
		if(  entries[i].pos == lastpos  ) {
			// ignore double entries just one after the other
			entries.remove_at(i);
			if(  i<current_stop  ) {
				current_stop --;
			}
			i--;
		} else if(  entries[i].pos == koord3d::invalid  ) {
			// ignore double entries just one after the other
			entries.remove_at(i);
		}
		else {
			// next pos for check
			lastpos = entries[i].pos;
		}

		if(i > 0 && entries[i].wait_for_time)
		{
			// "minimum_loading" (wait for load) and wait_for_time are not compatible.
			entries[i].minimum_loading = 0;
		}
	}
	make_current_stop_valid();
}



bool schedule_t::remove()
{
	bool ok = entries.remove_at(current_stop);
	make_current_stop_valid();
	return ok;
}



void schedule_t::remove_entry( uint8 delete_enty )
{
	if( current_stop > delete_enty ) {
		current_stop--;
	}
	bool ok = entries.remove_at(delete_enty);
	make_current_stop_valid();
}



void schedule_t::move_entry_forward( uint8 cur )
{
	if( entries.get_count() <= 2 ) {
		return;
	}
	// so we have something to do
	uint8 new_cur = (cur + entries.get_count() - 1) % entries.get_count();
	schedule_entry_t cur_entry = entries[ cur ];
	entries.remove_at( cur );
	if ( cur==0 ) {
		current_stop = (current_stop + entries.get_count() - 1) % entries.get_count();
		entries.append( cur_entry );
	}
	else {
		if( current_stop == cur ) {
			current_stop = new_cur;
		}
		else if( current_stop == new_cur ) {
			current_stop++;
		}
		entries.insert_at( new_cur, cur_entry );
	}
	make_current_stop_valid();
}



void schedule_t::move_entry_backward( uint8 cur )
{
	if( entries.get_count() <= 2 ) {
		return;
	}
	// so we have something to do
	uint8 new_cur = (cur + 1) % entries.get_count();
	schedule_entry_t cur_entry = entries[ cur ];
	entries.remove_at( cur );
	if ( new_cur==0 ) {
		entries.insert_at( 0, cur_entry );
		current_stop = (current_stop + 1) % entries.get_count();
	}
	else {
		if( current_stop == cur ) {
			current_stop = new_cur;
		}
		else if( current_stop == new_cur ) {
			current_stop --;
		}
		entries.insert_at( new_cur, cur_entry );
	}
	make_current_stop_valid();
}



void schedule_t::rdwr(loadsave_t *file)
{
	xml_tag_t f( file, "schedule_t" );

	make_current_stop_valid();

	uint8 size = entries.get_count();
	if(  file->is_version_less(101, 1)  ) {
		uint32 dummy=current_stop;
		file->rdwr_long(dummy);
		current_stop = (uint8)dummy;

		sint32 maxi=size;
		file->rdwr_long(maxi);
		DBG_MESSAGE("schedule_t::rdwr()","read schedule %p with %i entries",this,maxi);
		if(file->is_version_less(86, 10)) {
			// old array had different maxi-counter
			maxi ++;
		}
		size = (uint8)max(0,maxi);
	}
	else {
		file->rdwr_byte(current_stop);
		file->rdwr_byte(size);
		if(  file->is_version_atleast(102, 3) && file->get_extended_version() >= 9  ) {
			file->rdwr_bool(bidirectional);
			file->rdwr_bool(mirrored);
		}
	}
	entries.resize(size);

	if(file->is_version_less(99, 12)) {
		for(  uint8 i=0; i<size; i++  ) {
			koord3d pos;
			uint32 dummy;
			pos.rdwr(file);
			file->rdwr_long(dummy);
			entries.append(schedule_entry_t(pos, (uint8)dummy, 0, 0, true, false));
		}
	}
	else {
		// loading/saving new version
		for(  uint8 i=0;  i<size;  i++  ) {
			if(entries.get_count()<=i) {
				entries.append( schedule_entry_t() );
				entries[i].waiting_time_shift = 0;
				entries[i].spacing_shift = 0;
				entries[i].reverse = -1;
			}
			entries[i].pos.rdwr(file);
			if( file->get_extended_version() >= 10 && file->is_version_atleast(111, 2) ) {
				file->rdwr_short(entries[i].minimum_loading);
				if(entries[i].minimum_loading > 100 && spacing)
				{
					// Loading percentages of over 100 were almost invariably used
					// to set scheduled waiting points in 11.x and earlier.
					entries[i].minimum_loading = 0;
				}
			}
			else
			{
				// Previous versions had minimum_loading as a uint8.
				uint8 old_ladegrad = (uint8)entries[i].minimum_loading;
				file->rdwr_byte(old_ladegrad);
				entries[i].minimum_loading = (uint16)old_ladegrad;

			}
			if(file->is_version_atleast(99, 18)) {
				file->rdwr_byte(entries[i].waiting_time_shift);

				if( file->get_extended_version() >= 9 && file->is_version_atleast(110, 6) ) {
					file->rdwr_short(entries[i].spacing_shift);
				}

				if(file->get_extended_version() >= 10)
				{
					file->rdwr_byte(entries[i].reverse);
					if(file->get_extended_revision() < 4 && file->get_extended_version() < 13 && entries[i].reverse)
					{
						// Older versions had true as a default: set to indeterminate.
						entries[i].reverse = -1;
					}
				}
				else
				{
					entries[i].reverse = -1;
				}
#ifdef SPECIAL_RESCUE_12 // For testers who want to load games saved with earlier unreleased versions.
				if(file->get_extended_version() >= 12 && file->is_saving())
#else
				if(file->get_extended_version() >= 12)
#endif
				{
					file->rdwr_bool(entries[i].wait_for_time);
				}
				else if(file->is_loading())
				{
					entries[i].wait_for_time = entries[i].minimum_loading > 100 && spacing;
				}
			}
			if(entries[i].wait_for_time)
			{
				// "minimum_loading" (wait for load) and wait_for_time are not compatible.
				// Resolve this in games saved before this fix was implemented
				entries[i].minimum_loading = 0;
			}
		}
	}
	if(file->is_loading()) {
		editing_finished = true;
	}
	if(current_stop>=entries.get_count()  ) {
		if (!entries.empty()) {
			dbg->error("schedule_t::rdwr()","current_stop %i >count %i => current_stop = 0", current_stop, entries.get_count() );
		}
		current_stop = 0;
	}

	if(file->get_extended_version() >= 9)
	{
		file->rdwr_short(spacing);
	}

	if( file->get_extended_version() >= 9 && file->is_version_atleast(110, 6) ) {
		file->rdwr_bool(same_spacing_shift);
	}


}


void schedule_t::rotate90( sint16 y_size )
{
	// now we have to rotate all entries ...
	for(schedule_entry_t & i : entries) {
		i.pos.rotate90(y_size);
	}
}


/*
 * compare this schedule (schedule) with another, passed in schedule
 */
bool schedule_t::matches(karte_t *welt, const schedule_t *schedule)
{
	if(  schedule == NULL  ) {
		return false;
	}
	// same pointer => equal!
	if(  this==schedule  ) {
		return true;
	}
	// different bidirectional or mirrored settings => not equal
	if ((this->bidirectional != schedule->bidirectional) || (this->mirrored != schedule->mirrored)) {
		return false;
	}
	// unequal count => not equal, but no match for empty schedules
	const uint8 min_count = min( schedule->entries.get_count(), entries.get_count() );
	if(  min_count==0  &&  schedule->entries.get_count()!=entries.get_count()  ) {
		return false;
	}
	if ( this->spacing != schedule->spacing ) {
		return false;
	}
	if (this->same_spacing_shift != schedule->same_spacing_shift) {
		return false;
	}
	// now we have to check all entries ...
	// we need to do this that complicated, because the last stop may make the difference
	uint16 f1=0, f2=0;
	while(  f1+f2<entries.get_count()+schedule->entries.get_count()  ) {

		if(		f1<entries.get_count()  &&  f2<schedule->entries.get_count()
			&& schedule->entries[(uint8)f2].pos == entries[(uint8)f1].pos
			&& schedule->entries[(uint16)f2].minimum_loading == entries[(uint16)f1].minimum_loading
			&& schedule->entries[(uint8)f2].waiting_time_shift == entries[(uint8)f1].waiting_time_shift
			&& schedule->entries[(uint8)f2].spacing_shift == entries[(uint8)f1].spacing_shift
			&& schedule->entries[(uint8)f2].wait_for_time == entries[(uint8)f1].wait_for_time
		  ) {
			// minimum_loading/waiting ignored: identical
			f1++;
			f2++;
		}
		else {
			bool ok = false;
			if(  f1<entries.get_count()  ) {
				grund_t *gr1 = welt->lookup(entries[(uint8)f1].pos);
				if(  gr1  &&  gr1->get_depot()  ) {
					// skip depot
					f1++;
					ok = true;
				}
			}
			if(  f2<schedule->entries.get_count()  ) {
				grund_t *gr2 = welt->lookup(schedule->entries[(uint8)f2].pos);
				if(  gr2  &&  gr2->get_depot()  ) {
					ok = true;
					f2++;
				}
			}
			// no depot but different => do not match!
			if(  !ok  ) {
				/* in principle we could also check for same halt; but this is dangerous,
				 * since a rebuilding of a single square might change that
				 */
				return false;
			}
		}
	}
	return f1==entries.get_count()  &&  f2==schedule->entries.get_count();
}


/*
 * Increment or decrement the given index according to the given direction.
 * Also switches the direction if necessary.
 * @author yobbobandana
 */
void schedule_t::increment_index(uint8 *index, bool *reversed) const {
	if( !get_count() )
	{
		return;
	}
	if( *reversed )
	{
		if( *index != 0 )
		{
			*index = *index - 1;
		}
		else if( mirrored )
		{
			*reversed = false;
			*index = get_count()>1 ? 1 : 0;
		}
		else
		{
			*index = get_count()-1;
		}
	}
	else
	{
		if( *index < get_count()-1 )
		{
			*index = *index + 1;
		}
		else if( mirrored && get_count()>1 )
		{
			*reversed = true;
			*index = get_count()-2;
		}
		else
		{
			*index = 0;
		}
	}
}

/**
 * Same as increment_index(), but skips waypoints.
 * @author suitougreentea
 */
void schedule_t::increment_index_until_next_halt(player_t *player, uint8 *index, bool *reversed) const {
	uint32 counter = get_count();
	while (counter --)
	{
		increment_index(index, reversed);
		const koord3d halt_position = entries[*index].pos;
		const halthandle_t halt = haltestelle_t::get_halt(halt_position, player);
		if (halt.is_bound()) return;
	}
}

/**
 * Ordering based on halt id
 */
class HaltIdOrdering
{
public:
	bool operator()(const halthandle_t& a, const halthandle_t& b) const { return a.get_id() < b.get_id(); }
};


/**
 * compare this schedule (schedule) with another, ignoring order and exact positions and waypoints
 */
bool schedule_t::similar( const schedule_t *schedule, const player_t *player )
{
	if(  schedule == NULL  ) {
		return false;
	}
	// same pointer => equal!
	if(  this == schedule  ) {
		return true;
	}
	// unequal count => not equal
	const uint8 min_count = min( schedule->entries.get_count(), entries.get_count() );
	if(  min_count == 0  ) {
		return false;
	}
	// now we have to check all entries: So we add all stops to a vector we will iterate over
	vector_tpl<halthandle_t> halts;
	for(  uint8 idx = 0;  idx < this->entries.get_count();  idx++  ) {
		koord3d p = this->entries[idx].pos;
		halthandle_t halt = haltestelle_t::get_halt( p, player );
		if(  halt.is_bound()  ) {
			halts.insert_unique_ordered( halt, HaltIdOrdering() );
		}
	}
	vector_tpl<halthandle_t> other_halts;
	for(  uint8 idx = 0;  idx < schedule->entries.get_count();  idx++  ) {
		koord3d p = schedule->entries[idx].pos;
		halthandle_t halt = haltestelle_t::get_halt( p, player );
		if(  halt.is_bound()  ) {
			other_halts.insert_unique_ordered( halt, HaltIdOrdering() );
		}
	}
	// now compare them
	if(  other_halts.get_count() != halts.get_count()  ) {
		return false;
	}
	// number of unique halt similar => compare them now
	for(  uint32 idx = 0;  idx < halts.get_count();  idx++  ) {
		if(  halts[idx] != other_halts[idx]  ) {
			return false;
		}
	}
	return true;
}


void schedule_t::sprintf_schedule( cbuffer_t &buf ) const
{
	buf.append( current_stop );
	buf.printf( ",%i,%i,%i,%i", bidirectional, mirrored, spacing, same_spacing_shift) ;
	buf.append( "|" );
	buf.append( (int)get_type() );
	buf.append( "|" );
	FOR(minivec_tpl<schedule_entry_t>, const& i, entries)
	{
		buf.printf( "%s,%i,%i,%i,%i,%i|", i.pos.get_str(), i.minimum_loading, (int)i.waiting_time_shift, (int)i.spacing_shift, (int)i.reverse, (int)i.wait_for_time );
	}
}


bool schedule_t::sscanf_schedule( const char *ptr )
{
	const char *p = ptr;
	// first: clear current schedule
	while (!entries.empty()) {
		remove();
	}
	if ( p == NULL  ||  *p == 0) {
		// empty string
		return false;
	}
	//  first get current_stop pointer
	current_stop = atoi( p );
	while ( *p && isdigit(*p) ) { p++; }
	if ( *p && *p == ',' ) { p++; }
	//bidirectional flag
	if( *p && (*p!=','  &&  *p!='|') ) { bidirectional = bool(atoi(p)); }
	while ( *p && isdigit(*p) ) { p++; }
	if ( *p && *p == ',' ) { p++; }
	// mirrored flag
	if( *p && (*p!=','  &&  *p!='|') ) { mirrored = bool(atoi(p)); }
	while ( *p && isdigit(*p) ) { p++; }
	if ( *p && *p == ',' ) { p++; }
	// spacing
	if( *p && (*p!=','  &&  *p!='|') ) { spacing = atoi(p); }
	while ( *p && isdigit(*p) ) { p++; }
	if ( *p && *p == ',' ) { p++; }
	// same_spacing_shift flag
	if( *p && (*p!=','  &&  *p!='|') ) { same_spacing_shift = bool(atoi(p)); }
	while ( *p && isdigit(*p) ) { p++; }
	if ( *p && *p == ',' ) { p++; }

	if(  *p!='|'  ) {
		dbg->error( "schedule_t::sscanf_schedule()","incomplete entry termination!" );
		return false;
	}
	p++;
	//  then schedule type
	int type = atoi( p );
	//  .. check for correct type
	if(  type != (int)get_type()) {
		dbg->error( "schedule_t::sscanf_schedule()","schedule has wrong type (%d)! should have been %d.", type, get_type() );
		return false;
	}
	while(  *p  &&  *p!='|'  ) {
		p++;
	}
	if(  *p!='|'  ) {
		dbg->error( "schedule_t::sscanf_schedule()","incomplete entry termination!" );
		return false;
	}
	p++;
	// now scan the entries
	while(  *p>0  ) {
		sint16 values[8];
		for(  sint8 i=0;  i<8;  i++  ) {
			values[i] = atoi( p );
			while(  *p  &&  (*p!=','  &&  *p!='|')  ) {
				p++;
			}
			if(  i<7  &&  *p!=','  ) {
				dbg->error( "schedule_t::sscanf_schedule()","incomplete string!" );
				return false;
			}
			if(  i==7  &&  *p!='|'  ) {
				dbg->error( "schedule_t::sscanf_schedule()","incomplete entry termination!" );
				return false;
			}
			p++;
		}
		// ok, now we have a complete entry
		entries.append(schedule_entry_t(koord3d(values[0], values[1], (sint8)values[2]), (uint16)values[3], (sint8)values[4], (sint16)values[5], (sint8)values[6], (bool)values[7]));
	}
	return true;
}


uint32 schedule_t::get_travel_distance() const
{
	uint32 total_distance = 0;
	for (int i = 0; i < entries.get_count()-1; i++) {
		total_distance += shortest_distance(entries[i].pos.get_2d(), entries[i + 1].pos.get_2d());
	}
	if (mirrored) {
		total_distance *= 2;
	}
	else if (entries.get_count() > 1) {
		total_distance += shortest_distance(entries[entries.get_count()-1].pos.get_2d(), entries[0].pos.get_2d());
	}
	return total_distance;
}


void schedule_t::gimme_stop_name(cbuffer_t & buf, karte_t* welt, const player_t *player_, const schedule_entry_t &entry, bool no_control_tower )
{
	halthandle_t halt = haltestelle_t::get_halt(entry.pos, player_);
	if(halt.is_bound())
	{
		char modified_name[320];
		if(no_control_tower)
		{
			sprintf(modified_name, "%s [%s]", halt->get_name(), translator::translate("NO CONTROL TOWER"));
		}
		else
		{
			sprintf(modified_name, "%s", halt->get_name());
		}

		if(entry.wait_for_time && !skinverwaltung_t::waiting_time)
		{
			buf.printf("[*] ");
		}
		else if (entry.minimum_loading != 0)
		{
			buf.printf("%d%% ", entry.minimum_loading);
		}
		buf.printf("%s", modified_name );
	}
	else {
		const grund_t* gr = welt->lookup(entry.pos);
		if(  gr==NULL  ) {
			buf.printf("%s", translator::translate("Invalid coordinate") );
		}
		else if(  gr->get_depot() != NULL  ) {
			buf.printf("%s", translator::translate("Depot") );
		}
		else if(  const char *label_text = gr->get_text()  ){
			buf.printf("%s", label_text );
		}
		else {
			buf.printf("%s", translator::translate("Wegpunkt") );
		}
	}
}

void schedule_t::gimme_short_stop_name(cbuffer_t& buf, karte_t* welt, player_t const* const player_, const schedule_t *schedule, int i, int max_chars)
{
	if (i < 0 || schedule == NULL || i >= schedule->get_count()) {
		dbg->warning("void schedule_gui_t::gimme_short_stop_name()", "tried to receive unused entry %i in schedule %p.", i, schedule);
		return;
	}
	const schedule_entry_t& entry = schedule->entries[i];
	const char* p;
	halthandle_t halt = haltestelle_t::get_halt(entry.pos, player_);
	if (halt.is_bound()) {
		p = halt->get_name();
	}
	else {
		const grund_t* gr = welt->lookup(entry.pos);
		if (gr == NULL) {
			p = translator::translate("Invalid coordinate");
		}
		else if (gr->get_depot() != NULL) {
			p = translator::translate("Depot");
		}
		else {
			p = translator::translate("Wegpunkt");
		}
	}

	// Finally start to append the entry. Start with the most complicated...
	if (entry.wait_for_time && entry.reverse == 1)
	{
		if (strlen(p) > (unsigned)max_chars - 8)
		{
			buf.printf("[*] %.*s... [<<]", max_chars - 12, p);
		}
		else
		{
			buf.append("[*] ");
			buf.append(p);
			buf.append(" [<<]");
		}
	}
	else if (entry.wait_for_time)
	{
		if (strlen(p) > (unsigned)max_chars - 4)
		{
			buf.printf("[*] %.*s...", max_chars - 8, p);
		}
		else
		{
			buf.append("[*] ");
			buf.append(p);
		}
	}
	else if (entry.reverse == 1)
	{
		if (strlen(p) > (unsigned)max_chars - 4)
		{
			buf.printf("%.*s... [<<]", max_chars - 8, p);
		}
		else
		{
			buf.append(p);
			buf.append(" [<<]");
		}
	}
	else if (strlen(p) > (unsigned)max_chars)
	{
		buf.printf("%.*s...", max_chars - 3, p);
	}
	else
	{
		buf.append(p);
	}
}


bool schedule_t::is_contained (koord3d pos)
{
	for(schedule_entry_t entry : entries) {
		if(pos == entry.pos)
		{
			return true;
		}
	}
	return false;
}


uint8 schedule_t::get_entry_index(halthandle_t halt, player_t *player, bool reverse) const
{
	if( !halt.is_bound() ) return -1;
	for (uint8 i = 0; i < entries.get_count(); i++) {
		// For schedules with station overlap we have to do a search from current convoy location
		uint8 check_index = current_stop;
		if (reverse) {
			check_index = (check_index+entries.get_count()-i)%entries.get_count();
		}
		else {
			check_index = (check_index+i)%entries.get_count();
		}

		halthandle_t entry_halt = haltestelle_t::get_halt(entries[check_index].pos, player);
		if( !entry_halt.is_bound() ) continue;

		if( halt==entry_halt ) {
			return check_index;
		}
	}
	return -1;
}


times_history_data_t::times_history_data_t() {
	for (uint16 i = 0; i < TIMES_HISTORY_SIZE; i++) {
		history[i] = 0;
	}
}

uint32 times_history_data_t::get_entry(uint16 index) const {
	if (index >= TIMES_HISTORY_SIZE) return 0;
	return history[index];
}

void times_history_data_t::put(uint32 time) {
	for (uint16 i = TIMES_HISTORY_SIZE - 1; i >= 1; i--) {
		history[i] = history[i - 1];
	}
	history[0] = time;
}

void times_history_data_t::set(uint16 index, uint32 time) {
	if (index >= TIMES_HISTORY_SIZE) return;
	history[index] = time;
}

uint32 times_history_data_t::get_average_seconds() const {
	uint64 total = 0;
	uint16 count = 0;
	for (uint16 i = 0; i < TIMES_HISTORY_SIZE; i++) {
		if (history[i] != 0) {
			total += history[i] * 6;
			count++;
		}
	}
	if (count == 0) return 0;
	return total / count;
}
