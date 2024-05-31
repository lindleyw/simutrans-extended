/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "simtypes.h"
#include "simline.h"
#include "simhalt.h"
#include "simworld.h"

#include "utils/simstring.h"
#include "dataobj/schedule.h"
#include "dataobj/translator.h"
#include "dataobj/loadsave.h"
#include "gui/gui_theme.h"
#include "player/simplay.h"
#include "player/finance.h" // convert_money
#include "vehicle/vehicle.h"
#include "simconvoi.h"
#include "convoihandle_t.h"
#include "simlinemgmt.h"
#include "gui/simwin.h"
#include "gui/gui_frame.h"
#include "gui/schedule_list.h"


line_cost_t convoi_to_line_catgory_[convoi_t::MAX_CONVOI_COST] =
{
	LINE_CAPACITY,
	LINE_PAX_DISTANCE,
	LINE_AVERAGE_SPEED,
	LINE_COMFORT,
	LINE_REVENUE,
	LINE_OPERATIONS,
	LINE_PROFIT,
	LINE_DISTANCE,
	LINE_REFUNDS,
	LINE_WAYTOLL,
	LINE_MAIL_DISTANCE,
	LINE_PAYLOAD_DISTANCE
};

line_cost_t simline_t::convoi_to_line_catgory(convoi_t::convoi_cost_t cnv_cost)
{
	assert(cnv_cost < convoi_t::MAX_CONVOI_COST);
	return convoi_to_line_catgory_[cnv_cost];
}

const uint simline_t::linetype_to_stationtype[simline_t::MAX_LINE_TYPE] = {
	haltestelle_t::invalid,
	haltestelle_t::busstop,
	haltestelle_t::railstation,
	haltestelle_t::dock,
	haltestelle_t::airstop,
	haltestelle_t::monorailstop,
	haltestelle_t::tramstop,
	haltestelle_t::maglevstop,
	haltestelle_t::narrowgaugestop
};

karte_ptr_t simline_t::welt;


simline_t::simline_t(player_t* player, linetype type) :
	schedule(NULL),
	player(player),
	type(type),
	withdraw(false),
	self(linehandle_t(this)),
	state_color(SYSCOL_TEXT),
	start_reversed(false),
	livery_scheme_index(0),
	state(line_normal_state)
{
	char printname[128];
	sprintf(printname, "(%i) %s", self.get_id(), translator::translate("Line", welt->get_settings().get_name_language_id()));
	name = printname;

	for (uint32 j = 0; j< MAX_LINE_COST; ++j) {
		for (uint32 i = 0; i < MAX_MONTHS; ++i) {
			financial_history[i][j] = 0;
		}
	}

	for(uint8 i = 0; i < MAX_LINE_COST; i ++)
	{
		rolling_average[i] = 0;
		rolling_average_count[i] = 0;
	}

	create_schedule();
}


simline_t::simline_t(player_t* player, linetype type, loadsave_t *file) :
	schedule(NULL),
	player(player),
	type(type),
	withdraw(false),
	name(""),
	self(linehandle_t()), // id will be read and assigned during rdwr
	state_color(SYSCOL_TEXT),
	start_reversed(false),
	livery_scheme_index(0),
	state(line_normal_state)
{
	for (uint32 j = 0; j< MAX_LINE_COST; ++j) {
		for (uint32 i = 0; i < MAX_MONTHS; ++i) {
			financial_history[i][j] = 0;
		}
	}

	for(uint8 i = 0; i < MAX_LINE_COST; i ++)
	{
		rolling_average[i] = 0;
		rolling_average_count[i] = 0;
	}

	create_schedule();

	rdwr(file);

	// now self has the right id but the this-pointer is not assigned to the quickstone handle yet
	// do this explicitly
	// some savegames have line_id=0, resolve that in finish_rd
	if (self.get_id()!=0) {
		self = linehandle_t(this, self.get_id());
	}
}


simline_t::~simline_t()
{
	DBG_DEBUG("simline_t::~simline_t()", "deleting schedule=%p", schedule);

	if (!welt->is_destroying())
	{
		assert(count_convoys() == 0);
		unregister_stops();
	}


	delete schedule;
	self.detach();
	DBG_MESSAGE("simline_t::~simline_t()", "line %d (%p) destroyed", self.get_id(), this);
}

simline_t::linetype simline_t::waytype_to_linetype(const waytype_t wt)
{
	switch (wt) {
		case road_wt: return simline_t::truckline;
		case track_wt: return simline_t::trainline;
		case water_wt: return simline_t::shipline;
		case monorail_wt: return simline_t::monorailline;
		case maglev_wt: return simline_t::maglevline;
		case tram_wt: return simline_t::tramline;
		case narrowgauge_wt: return simline_t::narrowgaugeline;
		case air_wt: return simline_t::airline;
		default: return simline_t::MAX_LINE_TYPE;
	}
}


uint16 simline_t::get_traction_types() const
{
	uint16 traction_types = 0;
	for (auto line_managed_convoy : line_managed_convoys) {
		traction_types |= line_managed_convoy->get_traction_types();
	}
	return traction_types;
}


const char *simline_t::get_linetype_name(const simline_t::linetype lt)
{
	return translator::translate( schedule_type_text[lt] );
}


waytype_t simline_t::linetype_to_waytype(const linetype lt)
{
	static const waytype_t wt2lt[MAX_LINE_TYPE] = { invalid_wt, road_wt, track_wt, water_wt, air_wt, monorail_wt, tram_wt, maglev_wt, narrowgauge_wt };
	return wt2lt[lt];
}


void simline_t::set_schedule(schedule_t* schedule)
{
	if (this->schedule)
	{
		haltestelle_t::refresh_routing(schedule, goods_catg_index, NULL, NULL, player);
		unregister_stops();
		delete this->schedule;
	}
	this->schedule = schedule;
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
}


void simline_t::set_name(const char *new_name)
{
	name = new_name;

	/// Update window title if window is open
	gui_frame_t *line_info = win_get_magic((ptrdiff_t)self.get_rep());

	if (line_info) {
		line_info->set_name(name);
	}
}


void simline_t::create_schedule()
{
	recalc_status();
	switch(type) {
		case simline_t::truckline:       set_schedule(new truck_schedule_t()); break;
		case simline_t::trainline:       set_schedule(new train_schedule_t()); break;
		case simline_t::shipline:        set_schedule(new ship_schedule_t()); break;
		case simline_t::airline:         set_schedule(new airplane_schedule_()); break;
		case simline_t::monorailline:    set_schedule(new monorail_schedule_t()); break;
		case simline_t::tramline:        set_schedule(new tram_schedule_t()); break;
		case simline_t::maglevline:      set_schedule(new maglev_schedule_t()); break;
		case simline_t::narrowgaugeline: set_schedule(new narrowgauge_schedule_t()); break;
		default:
			dbg->fatal( "simline_t::create_schedule()", "Cannot create default schedule!" );
	}
}


void simline_t::add_convoy(convoihandle_t cnv, bool from_loading)
{
	if (line_managed_convoys.empty()  &&  self.is_bound()) {
		// first convoi -> ok, now we can announce this connection to the stations
		// unbound self can happen during loading if this line had line_id=0
		register_stops(schedule);
	}

	// first convoi may change line type
	if (type == trainline  &&  line_managed_convoys.empty() &&  cnv.is_bound()) {
		// check, if needed to convert to tram/monorail line
		if (vehicle_t const* const v = cnv->front()) {
			switch (v->get_desc()->get_waytype()) {
				case tram_wt:     type = simline_t::tramline;     break;
				// elevated monorail were saved with wrong coordinates for some versions.
				// We try to recover here
				case monorail_wt: type = simline_t::monorailline; break;
				default:          break;
			}
		}
	}
	// only add convoy if not already member of line
	line_managed_convoys.append_unique(cnv);

	// what goods can this line transport?
	bool update_schedules = false;
	if(  cnv->get_state()!=convoi_t::INITIAL  ) {
		/*
		// already on the road => need to add them
		for(  uint8 i=0;  i<cnv->get_vehicle_count();  i++  ) {
			// Only consider vehicles that really transport something
			// this helps against routing errors through passenger
			// trains pulling only freight wagons
			if(  cnv->get_vehicle(i)->get_cargo_max() == 0 && (cnv->get_vehicle(i)->get_cargo_type() != goods_manager_t::passengers || cnv->get_vehicle()->get_desc()->get_overcrowded_capacity() == 0))
			{
				continue;
			}
			const goods_desc_t *ware=cnv->get_vehicle(i)->get_cargo_type();
			if(  ware!=goods_manager_t::none  &&  !goods_catg_index.is_contained(ware->get_catg_index())  ) {
				goods_catg_index.append( ware->get_catg_index(), 1 );
				update_schedules = true;
			}
		}
		*/

		// Added by : Knightly
		const minivec_tpl<uint8> &categories = cnv->get_goods_catg_index();
		const uint8 catg_count = categories.get_count();
		for (uint8 i = 0; i < catg_count; i++)
		{
			if (!goods_catg_index.is_contained(categories[i]))
			{
				goods_catg_index.append(categories[i], 1);
				update_schedules = true;
			}
		}
	}

	calc_classes_carried();

	// will not hurt ...
	financial_history[0][LINE_CONVOIS] = count_convoys();
	recalc_status();

	// do we need to tell the world about our new schedule?
	if(  update_schedules  )
	{
		// Added by : Knightly
		haltestelle_t::refresh_routing(schedule, goods_catg_index, NULL, NULL, player);
	}

	// if the schedule is flagged as bidirectional, set the initial convoy direction
	if( schedule->is_bidirectional() && !from_loading )
	{
		start_reversed = !start_reversed;
		cnv->set_reverse_schedule(start_reversed);
	}
}


void simline_t::remove_convoy(convoihandle_t cnv)
{
	if(line_managed_convoys.is_contained(cnv) && !welt->is_destroying()) {
		line_managed_convoys.remove(cnv);
		recalc_catg_index();
		financial_history[0][LINE_CONVOIS] = count_convoys();
		recalc_status();
	}
	if(line_managed_convoys.empty()) {
		unregister_stops();
	}
	calc_classes_carried();
}


// invalid line id prior to 110.0
#define INVALID_LINE_ID_OLD ((uint16)(-1))
// invalid line id from 110.0 on
#define INVALID_LINE_ID ((uint16)(0))

void simline_t::rdwr_linehandle_t(loadsave_t *file, linehandle_t &line)
{
	uint16 id;
	if (file->is_saving()) {
		id = line.is_bound() ? line.get_id() :
			 (file->is_version_less(110, 0)  ? INVALID_LINE_ID_OLD : INVALID_LINE_ID);
	}
	else {
		// to avoid undefined errors during loading
		id = 0;
	}

	if(file->is_version_less(88, 3)) {
		sint32 dummy=id;
		file->rdwr_long(dummy);
		id = (uint16)dummy;
	}
	else {
		file->rdwr_short(id);
	}
	if (file->is_loading()) {
		// invalid line_id's: 0 and 65535
		if (id == INVALID_LINE_ID_OLD) {
			id = 0;
		}
		line.set_id(id);
	}
}


void simline_t::rdwr(loadsave_t *file)
{
	xml_tag_t s( file, "simline_t" );

	assert(schedule);

	file->rdwr_str(name);

	rdwr_linehandle_t(file, self);

	schedule->rdwr(file);

	//financial history
	if(  file->is_version_less(102, 3) || (file->is_version_less(103, 0) && file->get_extended_version() < 7) ) {
		for (int j = 0; j<LINE_DISTANCE; j++) {
			for (int k = MAX_MONTHS-1; k>=0; k--)
			{
				if(((j == LINE_AVERAGE_SPEED || j == LINE_COMFORT) && file->get_extended_version() <= 1) || (j == LINE_REFUNDS && file->get_extended_version() < 8))
				{
					// Versions of Extended saves with 1 and below
					// did not have settings for average speed or comfort.
					// Thus, this value must be skipped properly to
					// assign the values. Likewise, versions of Extended < 8
					// did not store refund information.
					if(file->is_loading())
					{
						financial_history[k][j] = 0;
					}
					continue;
				}
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
		for (int k = MAX_MONTHS-1; k>=0; k--)
		{
			financial_history[k][LINE_DISTANCE] = 0;
		}
	}
	else
	{
		for (int j = 0; j<MAX_LINE_COST; j++)
		{
			for (int k = MAX_MONTHS-1; k>=0; k--)
			{
#ifdef SPECIAL_RESCUE_12_2
				if(((j == LINE_AVERAGE_SPEED || j == LINE_COMFORT) && file->get_extended_version() <= 1) || (j == LINE_REFUNDS && file->get_extended_version() < 8) || ((j == LINE_DEPARTURES || j == LINE_DEPARTURES_SCHEDULED) && (file->get_extended_version() < 12 || file->is_loading())))
#else
				if(((j == LINE_AVERAGE_SPEED || j == LINE_COMFORT) && file->get_extended_version() <= 1)
					|| (j == LINE_REFUNDS && file->get_extended_version() < 8)
					|| ((j == LINE_DEPARTURES || j == LINE_DEPARTURES_SCHEDULED) && file->get_extended_version() < 12)
					|| ((j == LINE_MAIL_DISTANCE || j == LINE_PAYLOAD_DISTANCE) && file->is_version_ex_less(14, 48))
					)
#endif
				{
					// Versions of Extended saves with 1 and below
					// did not have settings for average speed or comfort.
					// Thus, this value must be skipped properly to
					// assign the values. Likewise, versions of Extended < 8
					// did not store refund information, and those of < 12 did
					// not store departure or scheduled departure information.
					if(file->is_loading())
					{
						financial_history[k][j] = 0;
					}
					continue;
				}
				else if(j == 7 && file->is_version_atleast(111,1) && file->get_extended_version() == 0)
				{
					// In Standard, this is LINE_MAXSPEED.
					sint64 dummy = 0;
					file->rdwr_longlong(dummy);
				}
				else if (j == LINE_WAYTOLL && (file->get_extended_version() < 14 || (file->get_extended_version() == 14 && file->get_extended_revision() < 25)))
				{
					if (file->is_loading())
					{
						financial_history[k][j] = 0;
						continue;
					}
				}
				file->rdwr_longlong(financial_history[k][j]);
			}
		}
	}

	if(  file->is_version_atleast(102, 2)  ) {
		file->rdwr_bool(withdraw);
	}

	if(file->get_extended_version() >= 9)
	{
		file->rdwr_bool(start_reversed);
	}

	// otherwise initialized to zero if loading ...
	financial_history[0][LINE_CONVOIS] = count_convoys();

	if(file->get_extended_version() >= 2)
	{
#ifdef SPECIAL_RESCUE_12_2
		const uint8 counter = file->is_version_less(103, 0) ? LINE_DISTANCE : file->get_extended_version() < 12 || file->is_loading() ? LINE_REFUNDS + 1 : LINE_WAYTOLL;
#else
		const uint8 counter = file->is_version_less(103, 0) ? LINE_DISTANCE : file->get_extended_version() < 12 ? LINE_REFUNDS + 1 : LINE_WAYTOLL;
#endif
		for(uint8 i = 0; i < counter; i ++)
		{
			file->rdwr_long(rolling_average[i]);
			file->rdwr_short(rolling_average_count[i]);
		}
		if (file->get_extended_version() > 14 || (file->get_extended_version() == 14 && file->get_extended_revision() >= 25))
		{
			file->rdwr_long(rolling_average[LINE_WAYTOLL]);
			file->rdwr_short(rolling_average_count[LINE_WAYTOLL]);
		}
	}

	if(  file->is_version_atleast(110, 6) && file->get_extended_version() >= 9  ) {
		file->rdwr_short(livery_scheme_index);
	}
	else
	{
		livery_scheme_index = 0;
	}

	if(file->get_extended_version() >= 10)
	{
		if(file->is_saving())
		{
			uint32 count = average_journey_times.get_count();
			file->rdwr_long(count);

			FOR(journey_times_map, const& iter, average_journey_times)
			{
				id_pair idp = iter.key;
				file->rdwr_short(idp.x);
				file->rdwr_short(idp.y);
				sint16 value = iter.value.count;
				file->rdwr_short(value);
				value = iter.value.total;
				file->rdwr_short(value);
			}
		}
		else
		{
			uint32 count = 0;
			file->rdwr_long(count);
			average_journey_times.clear();
			for(uint32 i = 0; i < count; i ++)
			{
				id_pair idp;
				file->rdwr_short(idp.x);
				file->rdwr_short(idp.y);

				uint16 count;
				uint16 total;
				file->rdwr_short(count);
				file->rdwr_short(total);

				average_tpl<uint32> average;
				average.count = count;
				average.total = total;

				average_journey_times.put(idp, average);
			}
		}
	}
	if ((file->get_extended_version() == 13 && file->get_extended_revision() >= 2) || file->get_extended_version() >= 14)
	{
		if(file->is_saving())
		{
			uint32 count = journey_times_history.get_count();
			file->rdwr_long(count);

			FOR(times_history_map, const& iter, journey_times_history)
			{
				departure_point_t idp = iter.key;
				file->rdwr_short(idp.x);
				file->rdwr_short(idp.y);
				times_history_data_t value = iter.value;
				for (int j = 0; j < TIMES_HISTORY_SIZE; j++)
				{
					uint32 time = value.get_entry(j);
					file->rdwr_long(time);
				}
			}
		}
		else
		{
			uint32 count = 0;
			file->rdwr_long(count);
			journey_times_history.clear();

			for (uint32 i = 0; i < count; i++)
			{
				departure_point_t idp;
				file->rdwr_short(idp.x);
				file->rdwr_short(idp.y);

				times_history_data_t data;

				for (int j = 0; j < TIMES_HISTORY_SIZE; j++) {
					uint32 time;
					file->rdwr_long(time);
					data.set(j, time);
				}

				journey_times_history.put(idp, data);
			}
		}
	}
	if(  file->is_version_atleast(111, 2) && file->get_extended_version() >= 10 && file->get_extended_version() < 12  )
	{
		bool dummy_is_alternating_circle_route = false; // Deprecated.
		file->rdwr_bool(dummy_is_alternating_circle_route);
		if(dummy_is_alternating_circle_route)
		{
			if(file->is_saving())
			{
				uint32 count = 0;
				file->rdwr_long(count);
				// There are no data to save.
			}
			else
			{
				uint32 count = 0;
				file->rdwr_long(count);
				for(uint32 i = 0; i < count; i ++)
				{
					id_pair dummy_idp;
					file->rdwr_short(dummy_idp.x);
					file->rdwr_short(dummy_idp.y);

					uint16 dummy;
					file->rdwr_short(dummy);
					file->rdwr_short(dummy);
				}
			}
		}
	}
	if (file->is_version_ex_atleast(14,52)) {
		file->rdwr_byte(line_color_index);
		file->rdwr_byte(line_lettercode_style);
		file->rdwr_str(linecode_l, lengthof(linecode_l));
		file->rdwr_str(linecode_r, lengthof(linecode_r));
	}

	// discard old incompatible datum
	if( file->is_loading() ) {
		if( file->is_version_ex_less(14,57) ) {
			for (int k = MAX_MONTHS - 1; k >= 0; k--) {
				financial_history[k][LINE_CAPACITY] = 0;
				if( file->is_version_ex_less(14,48) ) {
					financial_history[k][LINE_PAX_DISTANCE] = 0;
				}
			}
		}
		else if( file->is_version_ex_less(14,64) ) {
			// convert to new data (vacant_seats*km to seat-km)
			for (int k = MAX_MONTHS - 1; k >= 0; k--) {
				financial_history[k][LINE_CAPACITY] += financial_history[k][LINE_PAX_DISTANCE];
			}
		}
	}
}


void simline_t::finish_rd()
{
	if(  !self.is_bound()  ) {
		// get correct handle
		self = player->simlinemgmt.get_line_with_id_zero();
		assert( self.get_rep() == this );
		DBG_MESSAGE("simline_t::finish_rd", "assigned id=%d to line %s", self.get_id(), get_name());
	}
	if (!line_managed_convoys.empty()) {
		register_stops(schedule);
	}
	recalc_status();
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
}



void simline_t::register_stops(schedule_t * schedule)
{
	DBG_DEBUG("simline_t::register_stops()", "%d schedule entries in schedule %p", schedule->get_count(),schedule);
	for(schedule_entry_t const& i : schedule->entries) {
		halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
		if(halt.is_bound()) {
			//DBG_DEBUG("simline_t::register_stops()", "halt not null");
			halt->add_line(self);
		}
		else {
			DBG_DEBUG("simline_t::register_stops()", "halt null");
		}
	}
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
}

int simline_t::get_replacing_convoys_count() const {
	int count=0;
	for (uint32 i=0; i<line_managed_convoys.get_count(); ++i) {
		if (line_managed_convoys[i]->get_replace()) {
			count++;
		}
	}
	return count;
}

void simline_t::unregister_stops()
{
	unregister_stops(schedule);

	// It is necessary to clear all departure data,
	// which might be out of date on a change of schedule.
	for(convoihandle_t const &i : line_managed_convoys) {
		i->clear_departures();
	}
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
}


void simline_t::unregister_stops(schedule_t * schedule)
{
	for(schedule_entry_t const& i : schedule->entries) {
		halthandle_t const halt = haltestelle_t::get_halt(i.pos, player);
		if(halt.is_bound()) {
			halt->remove_line(self);
		}
	}
	journey_times_history.clear();
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
}


void simline_t::renew_stops()
{
	if (!line_managed_convoys.empty())
	{
		register_stops( schedule );

		// Added by Knightly
		haltestelle_t::refresh_routing(schedule, goods_catg_index, NULL, NULL, player);

		DBG_DEBUG("simline_t::renew_stops()", "Line id=%d, name='%s'", self.get_id(), name.c_str());
	}
	else
	{
		financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();
	}
}

void simline_t::set_linecode_l(const char *new_name)
{
	tstrncpy(linecode_l, new_name, lengthof(linecode_l));
}

void simline_t::set_linecode_r(const char *new_name)
{
	tstrncpy(linecode_r, new_name, lengthof(linecode_r));
}

void simline_t::set_line_color(uint8 color_idx, uint8 style)
{
	line_color_index = color_idx; line_lettercode_style = style;
}

void simline_t::check_freight()
{
	for(convoihandle_t const i : line_managed_convoys) {
		i->check_freight();
	}
}


void simline_t::new_month()
{
	recalc_status();
	for (int j = 0; j<MAX_LINE_COST; j++)
	{
		for (int k = MAX_MONTHS-1; k>0; k--)
		{
			financial_history[k][j] = financial_history[k-1][j];
		}
		financial_history[0][j] = 0;
	}
	financial_history[0][LINE_CONVOIS] = count_convoys();
	financial_history[0][LINE_DEPARTURES_SCHEDULED] = calc_departures_scheduled();

	if(financial_history[1][LINE_AVERAGE_SPEED] == 0)
	{
		// Last month's average speed is recorded as zero. This means that no
		// average speed data have been recorded in the last month, making
		// revenue calculations inaccurate. Use the second previous month's average speed
		// for the previous month's average speed.
		financial_history[1][LINE_AVERAGE_SPEED] = financial_history[2][LINE_AVERAGE_SPEED];
	}

	for(uint8 i = 0; i < MAX_LINE_COST; i ++)
	{
		rolling_average[i] = 0;
		rolling_average_count[i] = 0;
	}
}


void simline_t::init_financial_history()
{
	MEMZERO(financial_history);
}



/*
 * the current state saved as color
 * Meanings are BLACK (ok), WHITE (no convois), YELLOW (no vehicle moved), RED (last month income minus), DARK PURPLE (some vehicles overcrowded), BLUE (at least one convoi vehicle is obsolete)
 */
void simline_t::recalc_status()
{
	// normal state
	// Moved from an else statement at bottom
	// to ensure that this value is always initialised.
	state_color = SYSCOL_TEXT;
	state = line_normal_state;

	// Now can have multiple flags, so higher priority will be processed later for text color.
	// Note that if the pakset has no symbols, it depends on the text color.
	if (welt->use_timeline())
	{
		const uint16 month_now = welt->get_timeline_year_month();

		for(convoihandle_t const i : line_managed_convoys) {
			for (uint16 j = 0; j < i->get_vehicle_count(); j++)
			{
				vehicle_t *v = i->get_vehicle(j);
				if (v->get_desc()->get_upgrades_count() > 0)
				{
					for (int k = 0; k < v->get_desc()->get_upgrades_count(); k++)
					{
						if (v->get_desc()->get_upgrades(k) && !v->get_desc()->get_upgrades(k)->is_future(month_now))
						{
							state |= line_has_upgradeable_vehicles;
							if (!skinverwaltung_t::upgradable) state_color = SYSCOL_UPGRADEABLE;
						}
					}
				}
			}

			if (i->has_obsolete_vehicles())
			{
				state |= line_has_obsolete_vehicles;
				// obsolete has priority over upgradeable (only for color)
				state_color = SYSCOL_OBSOLETE;
			}

			if (i->get_state() == convoi_t::NO_ROUTE || i->get_state() == convoi_t::NO_ROUTE_TOO_COMPLEX || i->get_state() == convoi_t::OUT_OF_RANGE)
			{
				state |= line_has_stuck_convoy;
			}
		}
	}

	if (financial_history[1][LINE_DEPARTURES] < financial_history[1][LINE_DEPARTURES_SCHEDULED])
	{
		// Is missing scheduled slots.
		state_color = color_idx_to_rgb(COL_DARK_TURQUOISE);
		state |= line_missing_scheduled_slots;
	}
	if (has_overcrowded())
	{
		// Overcrowded
		state |= line_overcrowded;
		if (!skinverwaltung_t::upgradable) state_color = color_idx_to_rgb(COL_DARK_PURPLE);
	}
	if((financial_history[0][LINE_DISTANCE]|financial_history[1][LINE_DISTANCE]|financial_history[2][LINE_DISTANCE]) ==0)
	{
		// nothing moved
		state_color = SYSCOL_TEXT_UNUSED;
		state |= line_nothing_moved;
	}
	if(financial_history[0][LINE_CONVOIS]==0)
	{
		// no convoys assigned to this line
		state_color = SYSCOL_EMPTY;
		state |= line_no_convoys;
		withdraw = false;
	}
	if(financial_history[0][LINE_PROFIT]<0 && financial_history[1][LINE_PROFIT]<0)
	{
		// Loss-making
		state_color = MONEY_MINUS;
		state |= line_loss_making;
	}
}

bool simline_t::has_overcrowded() const
{
	for (auto line_managed_convoy : line_managed_convoys)
	{
		if (!line_managed_convoy->get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS)) continue;

		if(line_managed_convoy->get_overcrowded() > 0)
		{
			return true;
		}
	}
	return false;
}

bool simline_t::needs_electrification() const
{
	for (auto line_managed_convoy : line_managed_convoys)
	{
		if (line_managed_convoy->needs_electrification())
		{
			return true;
		}
	}
	return false;
}

uint16 simline_t::get_min_range() const
{
	uint16 min_range = UINT16_MAX;
	for (auto line_managed_convoy : line_managed_convoys)
	{
		if (line_managed_convoy->get_min_range())
		{
			min_range = min(line_managed_convoy->get_min_range(), min_range);
		}
	}
	return min_range == UINT16_MAX ? 0 : min_range;
}

uint16 simline_t::get_min_top_speed_kmh() const
{
	uint16 min_top_speed = 65535;
	for (auto line_managed_convoy : line_managed_convoys)
	{
		min_top_speed = min(speed_to_kmh(line_managed_convoy->get_min_top_speed()), min_top_speed);
	}
	return min_top_speed;
}

bool simline_t::has_reverse_scheduled_convoy() const
{
	if (schedule->is_mirrored()) { return true; }
	for (auto line_managed_convoy : line_managed_convoys)
	{
		if (line_managed_convoy->get_reverse_schedule()) {
			return true;
		}
	}

	return false;
}

void simline_t::calc_classes_carried()
{
	if (welt->is_destroying())
	{
		return;
	}

	passenger_classes_carried.clear();
	mail_classes_carried.clear();
	for(convoihandle_t const i : line_managed_convoys) {
		convoi_t const& cnv = *i;

		if (cnv.get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS))
		{
			FOR(const minivec_tpl<uint8>, const& g_class, *cnv.get_classes_carried(goods_manager_t::INDEX_PAS))
			{
				passenger_classes_carried.append_unique(g_class);
			}
		}

		if (cnv.get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL))
		{
			FOR(const minivec_tpl<uint8>, const& g_class, *cnv.get_classes_carried(goods_manager_t::INDEX_MAIL))
			{
				mail_classes_carried.append_unique(g_class);
			}
		}

		/*

		for (uint32 j = 0; j < cnv.get_classes_carried(goods_manager_t::INDEX_PAS)->get_count(); j++)
		{
			if (cnv.get_goods_catg_index().is_contained(goods_manager_t::INDEX_PAS))
			{
				if (cnv.carries_this_or_lower_class(goods_manager_t::INDEX_PAS, j))
				{
					passenger_classes_carried.append_unique(j);
				}
			}
		}

		for (uint32 j = 0; j < cnv.get_classes_carried(goods_manager_t::INDEX_MAIL)->get_count(); j++)
		{
			if (cnv.get_goods_catg_index().is_contained(goods_manager_t::INDEX_MAIL))
			{
				if (cnv.carries_this_or_lower_class(goods_manager_t::INDEX_MAIL, j))
				{
					mail_classes_carried.append_unique(j);
				}
			}
		}*/
	}
	// update dialog
	schedule_list_gui_t *sl = dynamic_cast<schedule_list_gui_t*>(win_get_magic((ptrdiff_t)(magic_line_management_t + get_owner()->get_player_nr())));
	if (sl) {
		sl->update_data(self);
	}
}


uint16 simline_t::get_unique_fare_capacity(uint8 catg, uint8 g_class) const
{
	if (catg == goods_manager_t::INDEX_PAS) {
		if (g_class >= goods_manager_t::passengers->get_number_of_classes()) { return 0; }
		if (!passenger_classes_carried.is_contained(g_class)) { return 0; };
	}
	else if (catg == goods_manager_t::INDEX_MAIL) {
		if (g_class >= goods_manager_t::mail->get_number_of_classes()) { return 0; }
		if (!mail_classes_carried.is_contained(g_class)) { return 0; };
	}
	else if (g_class > 0) {
		return 0; // freight does not have classes
	}

	uint16 capacity = 0;
	for (uint32 i = 0; i < line_managed_convoys.get_count(); i++) {
		convoihandle_t const convoy = line_managed_convoys[i];
		// we do not want to count the capacity of depot convois
		if (!convoy->in_depot()) {
			capacity += convoy->get_unique_fare_capacity(catg, g_class);
		}
	}
	return capacity;
}

// recalc what good this line is moving
void simline_t::recalc_catg_index()
{
	// first copy old
	minivec_tpl<uint8> old_goods_catg_index(goods_catg_index.get_count());
	FOR(minivec_tpl<uint8>, const i, goods_catg_index) {
		old_goods_catg_index.append(i);
	}
	goods_catg_index.clear();
	withdraw = !line_managed_convoys.empty();
	// then recreate current
	FOR(vector_tpl<convoihandle_t>, const i, line_managed_convoys) {
		// what goods can this line transport?
		convoi_t const& cnv = *i;
		withdraw &= cnv.get_withdraw();

		for(uint8 const catg_index : cnv.get_goods_catg_index()) {
			goods_catg_index.append_unique( catg_index );
		}
	}

	// Recalculate the classes: store the old classes to test for differences.
	minivec_tpl<uint8> old_passenger_classes_carried;
	minivec_tpl<uint8> old_mail_classes_carried;

	for (uint32 i = 0; i < passenger_classes_carried.get_count(); i++)
	{
		old_passenger_classes_carried.append(i);
	}

	for (uint32 i = 0; i < mail_classes_carried.get_count(); i++)
	{
		old_mail_classes_carried.append(i);
	}

	calc_classes_carried();

	// Modified by	: Knightly
	// Purpose		: Determine removed and added categories and refresh only those categories.
	//				  Avoids refreshing unchanged categories
	minivec_tpl<uint8> catg_differences(goods_catg_index.get_count() + old_goods_catg_index.get_count());
	minivec_tpl<uint8> passenger_class_differences;
	minivec_tpl<uint8> mail_class_differences;

	// removed categories : present in old category list but not in new category list
	for (uint8 i = 0; i < old_goods_catg_index.get_count(); i++)
	{
		if ( ! goods_catg_index.is_contained( old_goods_catg_index[i] ) )
		{
			catg_differences.append( old_goods_catg_index[i] );
		}
	}

	// added categories : present in new category list but not in old category list
	for(uint8 const i : goods_catg_index) {
		if (!old_goods_catg_index.is_contained(i))
		{
			catg_differences.append(i);
		}
	}

	if (!catg_differences.is_contained(goods_manager_t::INDEX_PAS))
	{
		// Passengers were carried before and are carried now - check for class differences.

		for (uint8 i = 0; i < old_passenger_classes_carried.get_count(); i++)
		{
			// Classes present now not present previously
			if (!passenger_classes_carried.is_contained(old_passenger_classes_carried.get_element(i)))
			{
				passenger_class_differences.append(old_passenger_classes_carried.get_element(i));
			}
		}

		for (uint8 i = 0; i < passenger_classes_carried.get_count(); i++)
		{
			// Classes present previously not present now
			if (old_passenger_classes_carried.get_count() >= i)
			{
				break;
			}
			if (!old_passenger_classes_carried.is_contained(old_passenger_classes_carried.get_element(i)))
			{
				passenger_class_differences.append(i);
			}
		}
	}

	if (!catg_differences.is_contained(goods_manager_t::INDEX_MAIL))
	{
		// Mail was carried before and are carried now - check for class differences.

		for (uint8 i = 0; i < old_mail_classes_carried.get_count(); i++)
		{
			// Classes present now not present previously
			if (!mail_classes_carried.is_contained(old_mail_classes_carried.get_element(i)))
			{
				mail_class_differences.append(old_mail_classes_carried.get_element(i));
			}
		}

		for (uint8 i = 0; i < mail_classes_carried.get_count(); i++)
		{
			// Classes present previously not present now
			if (old_mail_classes_carried.get_count() >= i)
			{
				break;
			}
			if (!old_mail_classes_carried.is_contained(old_mail_classes_carried.get_element(i)))
			{
				mail_class_differences.append(i);
			}
		}
	}

	// refresh only those categories which are either removed or added to the category list
	haltestelle_t::refresh_routing(schedule, catg_differences, &passenger_class_differences, &mail_class_differences, player);
}

bool simline_t::carries_this_or_lower_class(uint8 catg, uint8 g_class)
{
	if (catg != goods_manager_t::INDEX_PAS && catg != goods_manager_t::INDEX_MAIL)
	{
		return true;
	}

	const bool carries_this_class = catg == goods_manager_t::INDEX_PAS ? passenger_classes_carried.is_contained(g_class) : mail_classes_carried.is_contained(g_class);
	if (carries_this_class)
	{
		return true;
	}

	// Check whether a lower class is carried, as passengers may board vehicles of a lower, but not a higher, class
	if (catg == goods_manager_t::INDEX_PAS)
	{
		FOR(vector_tpl<uint8>, i, passenger_classes_carried)
		{
			if (i < g_class)
			{
				return true;
			}
		}
	}
	else
	{
		FOR(vector_tpl<uint8>, i, mail_classes_carried)
		{
			if (i < g_class)
			{
				return true;
			}
		}
	}

	return false;
}


void simline_t::set_withdraw( bool yes_no )
{
	withdraw = yes_no && !line_managed_convoys.empty();
	// convois in depots will be immediately destroyed, thus we go backwards
	for (size_t i = line_managed_convoys.get_count(); i-- != 0;) {
		line_managed_convoys[i]->set_no_load(yes_no); // must be first, since set withdraw might destroy convoi if in depot!
		line_managed_convoys[i]->set_withdraw(yes_no);
	}
}

void simline_t::propogate_livery_scheme()
{
	ITERATE(line_managed_convoys, i)
	{
		line_managed_convoys[i]->set_livery_scheme_index(livery_scheme_index);
		line_managed_convoys[i]->apply_livery_scheme();
	}
}

PIXVAL simline_t::get_line_color() const
{
	return line_color_index==254 ? color_idx_to_rgb(player->get_player_color1()+4) : line_color_idx_to_rgb(line_color_index);
}

sint64 simline_t::calc_departures_scheduled()
{
	if(schedule->get_spacing() == 0)
	{
		return 0;
	}

	sint64 timed_departure_points_count = 0ll;
	for(int i = 0; i < schedule->get_count(); i++)
	{
		if(schedule->entries[i].wait_for_time || schedule->entries[i].minimum_loading > 0)
		{
			timed_departure_points_count ++;
		}
	}

	return timed_departure_points_count * (sint64) schedule->get_spacing();
}

sint64 simline_t::get_stat_converted(int month, int cost_type) const
{
	sint64 value = financial_history[month][cost_type];
	switch(cost_type) {
		case LINE_REVENUE:
		case LINE_OPERATIONS:
		case LINE_PROFIT:
		case LINE_WAYTOLL:
			value = convert_money(value);
			break;
		default: ;
	}
	return value;
}

uint32 simline_t::get_load_factor_pax_year() const
{
	if (goods_catg_index.is_contained(!goods_manager_t::INDEX_PAS)) return 0;

	sint64 total_seat_km = 0;
	sint64 total_pax_km = 0;
	for (uint8 m = 0; m < MAX_MONTHS; m++) {
		total_seat_km += financial_history[m][LINE_CAPACITY];
		total_pax_km += financial_history[m][LINE_PAX_DISTANCE];
	}
	return total_seat_km ? (uint32)(1000 * total_pax_km / total_seat_km) : 0;
}

uint32 simline_t::get_load_factor_pax_last_month() const
{
	if (goods_catg_index.is_contained(!goods_manager_t::INDEX_PAS)) return 0;

	return financial_history[1][LINE_CAPACITY] ? (uint32)(1000 * financial_history[1][LINE_PAX_DISTANCE] / financial_history[1][LINE_CAPACITY]) : 0;
}

sint64 simline_t::get_service_frequency()
{
	sint64 total_trip_times = 0;
	sint64 convoys_with_trip_data = 0;
	for (uint32 i = 0; i < line_managed_convoys.get_count(); i++) {
		convoihandle_t const cnv = line_managed_convoys[i];
		if (!cnv->in_depot()) {
			total_trip_times += cnv->get_average_round_trip_time();
			if (cnv->get_average_round_trip_time()) {
				convoys_with_trip_data++;
			}
		}
	}

	sint64 service_frequency = convoys_with_trip_data ? total_trip_times / convoys_with_trip_data : 0; // In ticks.
	if (line_managed_convoys.get_count()) {
		service_frequency /= line_managed_convoys.get_count();
	}
	const int spacing = schedule->get_spacing();
	if (line_managed_convoys.get_count() && spacing > 0)
	{
		// Check whether the spacing setting affects things.
		sint64 spacing_ticks = welt->ticks_per_world_month / (sint64)spacing;
		const uint32 spacing_time = welt->ticks_to_tenths_of_minutes(spacing_ticks);
		service_frequency = max(spacing_time, service_frequency);
	}
	return service_frequency;
}
