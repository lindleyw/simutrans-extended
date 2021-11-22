/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "../simdebug.h"
#include "../simcity.h"
#include "../simmenu.h"
#include "../simworld.h"
#include "../simcolor.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../obj/leitung2.h"
#include "../utils/cbuffer_t.h"
#include "../utils/simstring.h"
#include "../tpl/array2d_tpl.h"

#include "city_info.h"
#include "minimap.h"
#include "components/gui_button_to_chart.h"
#include "components/gui_image.h"
#include "components/gui_colorbox.h"
#include "components/gui_divider.h"

#include "../display/simgraph.h"

#define BUTTONS_PER_ROW 4


const char *hist_type[MAX_CITY_HISTORY] =
{
	"citicens",
	"Jobs",
	"Visitor demand",
	"Growth",
	"Buildings",
	"Verkehrsteilnehmer",
	"Public transport users",
	"Passagiere",
	"Walked",
	"sended",
	"Post",
	"Goods supplied",
	"Goods needed",
	"Power supply",
	"Power demand",
	"Congestion"
};

const uint8 hist_type_color[MAX_CITY_HISTORY] =
{
	COL_DARK_GREEN+1,
	COL_COMMUTER,
	COL_LIGHT_PURPLE,
	COL_DARK_GREEN,
	COL_GREY3,
	COL_TRAFFIC,
	COL_DODGER_BLUE,
	COL_PASSENGERS,
	COL_WALKED,
	COL_LIGHT_YELLOW,
	COL_YELLOW,
	COL_LIGHT_BROWN,
	COL_BROWN,
	COL_ELECTRICITY+2,
	COL_ELECTRICITY-1,
	COL_OVERCROWD
};


const char *pax_dest_type[PAX_DEST_COLOR_LEGENDS] =
{
	"Happy",
	"too_slow_and_gave_up",
	"too_slow_and_use_privatecar",
	"Verkehrsteilnehmer",
	"Walked",
	"Turned away",
	"No route",
	"destination_unavailable"
};

const uint8 pax_dest_color[PAX_DEST_COLOR_LEGENDS] =
{
	MAP_COL_HAPPY,
	MAP_COL_TOO_SLOW,
	MAP_COL_TOO_SLOW_USE_PRIVATECAR,
	MAP_COL_PRIVATECAR,
	MAP_COL_WALKED,
	MAP_COL_OVERCROWDED,
	MAP_COL_NOROUTE,
	MAP_COL_UNAVAILABLE
};



/**
 * Component to show both passenger destination minimaps
 */
class gui_city_minimap_t : public gui_world_component_t
{
	stadt_t* city;
	scr_size minimaps_size;        ///< size of minimaps
	scr_coord minimap2_offset;     ///< position offset of second minimap
	array2d_tpl<PIXVAL> pax_dest_old, pax_dest_new;
	uint32 pax_destinations_last_change;

	void init_pax_dest( array2d_tpl<PIXVAL> &pax_dest );
	void add_pax_dest( array2d_tpl<PIXVAL> &pax_dest, const sparse_tpl<PIXVAL>* city_pax_dest );

public:
	gui_city_minimap_t(stadt_t* city) : city(city),
		pax_dest_old(0,0),
		pax_dest_new(0,0)
	{
		minimaps_size = scr_size(welt->get_size().x, welt->get_size().y); // default minimaps size
		minimap2_offset = scr_coord(minimaps_size.w + D_H_SPACE, 0);
	}
	scr_size get_min_size() const OVERRIDE { return scr_size(PAX_DEST_MIN_SIZE*2 + D_H_SPACE, PAX_DEST_MIN_SIZE); }

	// set size of minimap, decide for horizontal or vertical arrangement
	void set_size(scr_size size) OVERRIDE
	{
		// calculate new minimaps size : expand horizontally or vertically ?
		const float world_aspect = (float)welt->get_size().x / (float)welt->get_size().y;
		const scr_coord space(size.w, size.h);
		const float space_aspect = (float)space.x / (float)space.y;

		if(  world_aspect/space_aspect > PAX_DEST_VERTICAL  ) { // world wider than space, use vertical minimap layout
			minimaps_size.h = (space.y - D_V_SPACE) / 2;
			if(  minimaps_size.h * world_aspect <= space.x) {
				// minimap fits
				minimaps_size.w = (sint16) (minimaps_size.h * world_aspect);
			}
			else {
				// too large, truncate
				minimaps_size.w = space.x;
				minimaps_size.h = max((int)(minimaps_size.w / world_aspect), PAX_DEST_MIN_SIZE);
			}
			minimap2_offset = scr_coord( 0, minimaps_size.h + D_V_SPACE );
		}
		else { // horizontal minimap layout
			minimaps_size.w = (space.x - D_H_SPACE) / 2;
			if (minimaps_size.w / world_aspect <= space.y) {
				// minimap fits
				minimaps_size.h = (sint16) (minimaps_size.w / world_aspect);
			}
			else {
				// too large, truncate
				minimaps_size.h = space.y;
				minimaps_size.w = max((int)(minimaps_size.h * world_aspect), PAX_DEST_MIN_SIZE);
			}
			minimap2_offset = scr_coord( minimaps_size.w + D_H_SPACE, 0 );
		}

		// resize minimaps
		pax_dest_old.resize( minimaps_size.w, minimaps_size.h );
		pax_dest_new.resize( minimaps_size.w, minimaps_size.h );

		// reinit minimaps data
		init_pax_dest( pax_dest_old );
		pax_dest_new = pax_dest_old;
		add_pax_dest( pax_dest_old, city->get_pax_destinations_old() );
		add_pax_dest( pax_dest_new, city->get_pax_destinations_new() );
		pax_destinations_last_change = city->get_pax_destinations_new_change();

		gui_world_component_t::set_size(scr_size(min(size.w, minimap2_offset.x + minimaps_size.w),min(size.h, minimap2_offset.y + minimaps_size.h)));
	}
	// handle clicks into minimaps
	bool infowin_event(const event_t *ev) OVERRIDE
	{
		int my = ev->my;
		if(  my > minimaps_size.h  &&  minimap2_offset.y > 0  ) {
			// Little trick to handle both maps with the same code: Just remap the y-values of the bottom map.
			my -= minimaps_size.h + D_V_SPACE;
		}

		if(  ev->ev_class!=EVENT_KEYBOARD  &&  ev->ev_code==MOUSE_LEFTBUTTON  &&  0<=my  &&  my<minimaps_size.h  ) {
			int mx = ev->mx;
			if(  mx > minimaps_size.w  &&  minimap2_offset.x > 0  ) {
				// Little trick to handle both maps with the same code: Just remap the x-values of the right map.
				mx -= minimaps_size.w + D_H_SPACE;
			}

			if(  0 <= mx && mx < minimaps_size.w  ) {
				// Clicked in a minimap.
				const koord p = koord(
					(mx * welt->get_size().x) / (minimaps_size.w),
									  (my * welt->get_size().y) / (minimaps_size.h));
				welt->get_viewport()->change_world_position( p );
				return true;
			}
		}
		return false;
	}
	// draw both minimaps
	void draw(scr_coord offset) OVERRIDE
	{
		const uint32 current_pax_destinations = city->get_pax_destinations_new_change();
		if(  pax_destinations_last_change > current_pax_destinations  ) {
			// new month started
			pax_dest_old = pax_dest_new;
			init_pax_dest( pax_dest_new );
			add_pax_dest( pax_dest_new, city->get_pax_destinations_new());
		}
		else if(  pax_destinations_last_change != current_pax_destinations  ) {
			// Since there are only new colors, this is enough:
			add_pax_dest( pax_dest_new, city->get_pax_destinations_new() );
		}
		pax_destinations_last_change = current_pax_destinations;

		display_array_wh(pos.x + offset.x, pos.y + offset.y, minimaps_size.w, minimaps_size.h, pax_dest_old.to_array() );
		display_array_wh(pos.x + offset.x + minimap2_offset.x, pos.y + offset.y + minimap2_offset.y, minimaps_size.w, minimaps_size.h, pax_dest_new.to_array() );
	}
};



city_info_t::city_info_t(stadt_t* city) :
	gui_frame_t( name, NULL ),
	city(city),
	scrolly_stats(&cont_city_stats, true),
	pax_map(city)
{
	if (city) {
		init();
	}
}

void city_info_t::init()
{
	reset_city_name();

	set_table_layout(1,0);

	// add city name input field
	name_input.add_listener( this );
	add_component(&name_input);

	lb_size.set_align(gui_label_t::centered);
	lb_buildings.set_align(gui_label_t::centered);

	add_table(1,0)->set_alignment(ALIGN_TOP);
	{
		add_table(1,0)->set_spacing(scr_size(D_H_SPACE, 0));
		add_component(&lb_border);

		new_component<gui_margin_t>(LINESPACE/3);

		add_component(&lb_powerdemand);

		new_component<gui_margin_t>(LINESPACE/3);

		// add "allow city growth" button below city info
		allow_growth.init( button_t::square_state, "Allow city growth");
		allow_growth.pressed = city->get_citygrowth();
		allow_growth.add_listener( this );
		add_component(&allow_growth);
		end_table();
	}
	end_table();

	// tab (month/year)
	container_chart.set_table_layout(1, 0);
	year_month_tabs.add_tab(&container_year, translator::translate("Years"));
	year_month_tabs.add_tab(&container_month, translator::translate("Months"));
	container_chart.add_component(&year_month_tabs);

	tabs.add_tab(&scrolly_stats, translator::translate("Statistics"));
	tabs.add_tab(&container_chart, translator::translate("Chart"));
	tabs.add_tab(&cont_destination_map, translator::translate("pax_destination_mapping"));
	add_component(&tabs);
	// .. put the same buttons in both containers
	button_t* buttons[MAX_CITY_HISTORY];
	// add city charts
	// year chart
	container_year.set_table_layout(1,0);
	container_year.add_component(&chart);
	chart.set_min_size(scr_size(0 ,8*LINESPACE));
	chart.set_dimension(MAX_CITY_HISTORY_YEARS, 10000);
	chart.set_seed(welt->get_last_year());
	chart.set_background(SYSCOL_CHART_BACKGROUND);
	chart.set_ltr(env_t::left_to_right_graphs);

	container_year.add_table(BUTTONS_PER_ROW,int((MAX_CITY_HISTORY+(BUTTONS_PER_ROW-1))/BUTTONS_PER_ROW))->set_force_equal_columns(true);
	//   skip electricity
	for(  uint32 i = 0;  i<MAX_CITY_HISTORY;  i++  ) {
		sint16 curve = chart.add_curve( color_idx_to_rgb(hist_type_color[i]), city->get_city_history_year(),
			MAX_CITY_HISTORY, i, 12, STANDARD, (city->stadtinfo_options & (1<<i))!=0, true, 0 );
		// add button
		buttons[i] = container_year.new_component<button_t>();
		buttons[i]->init(button_t::box_state_automatic | button_t::flexible, hist_type[i]);
		buttons[i]->background_color = color_idx_to_rgb(hist_type_color[i]);
		buttons[i]->pressed = (city->stadtinfo_options & (1<<i))!=0;

		button_to_chart.append(buttons[i], &chart, curve);
	}
	container_year.end_table();

	// month chart
	container_month.set_table_layout(1,0);
	container_month.add_component(&mchart);
	mchart.set_pos(scr_coord(D_MARGIN_LEFT,1));
	mchart.set_min_size(scr_size(0 ,8*LINESPACE));
	mchart.set_dimension(MAX_CITY_HISTORY_MONTHS, 10000);
	mchart.set_seed(0);
	mchart.set_background(SYSCOL_CHART_BACKGROUND);
	mchart.set_ltr(env_t::left_to_right_graphs);

	container_month.add_table(BUTTONS_PER_ROW,int((MAX_CITY_HISTORY+(BUTTONS_PER_ROW-1))/BUTTONS_PER_ROW))->set_force_equal_columns(true);
	for(  uint32 i = 0;  i<MAX_CITY_HISTORY;  i++  ) {
		sint16 curve = mchart.add_curve( color_idx_to_rgb(hist_type_color[i]), city->get_city_history_month(),
			MAX_CITY_HISTORY, i, 12, STANDARD, (city->stadtinfo_options & (1<<i))!=0, true, 0 );

		// add button
		container_month.add_component(buttons[i]);
		button_to_chart.append(buttons[i], &mchart, curve);
	}
	container_month.end_table();

	// City statistics
	cont_city_stats.set_table_layout(1, 0);
	transportation_this_year.set_size(scr_size(100, (D_INDICATOR_HEIGHT+LINESPACE)/2));
	transportation_last_year.set_size(scr_size(100, (D_INDICATOR_HEIGHT+LINESPACE)/2));
	transportation_this_year.add_color_value(&transportation_pas[0], color_idx_to_rgb(hist_type_color[HIST_PAS_TRANSPORTED]));
	transportation_this_year.add_color_value(&transportation_pas[1], color_idx_to_rgb(hist_type_color[HIST_CITYCARS]));
	transportation_this_year.add_color_value(&transportation_pas[2], color_idx_to_rgb(hist_type_color[HIST_PAS_WALKED]));
	transportation_last_year.add_color_value(&transportation_pas[3], color_idx_to_rgb(hist_type_color[HIST_PAS_TRANSPORTED]));
	transportation_last_year.add_color_value(&transportation_pas[4], color_idx_to_rgb(hist_type_color[HIST_CITYCARS]));
	transportation_last_year.add_color_value(&transportation_pas[5], color_idx_to_rgb(hist_type_color[HIST_PAS_WALKED]));

	// passenger destination mapping
	cont_destination_map.set_table_layout(1,0);
	cont_destination_map.add_component(&pax_map);

	bt_show_contour.init(button_t::square_state, "Show contour");
	bt_show_contour.set_tooltip("Color-coded terrain according to altitude.");
	bt_show_contour.add_listener(this);
	bt_show_contour.pressed=true;
	cont_destination_map.add_component(&bt_show_contour);

	cont_destination_map.add_table(3,1)->set_alignment(ALIGN_TOP);
	{
		bt_show_hide_legend.init(button_t::roundbox_state, "+");
		bt_show_hide_legend.set_width(display_get_char_width('+') + gui_theme_t::gui_button_text_offset.w + gui_theme_t::gui_button_text_offset_right.x);
		bt_show_hide_legend.add_listener(this);
		cont_destination_map.add_component(&bt_show_hide_legend);

		lb_collapsed.set_text("Open the color legend");
		cont_destination_map.add_component(&lb_collapsed);

		cont_minimap_legend.set_table_layout(2,0);
		for (uint8 i = 0; i < PAX_DEST_COLOR_LEGENDS; i++) {
			cont_minimap_legend.new_component<gui_colorbox_t>()->init(color_idx_to_rgb(pax_dest_color[i]), scr_size(D_INDICATOR_BOX_WIDTH, D_INDICATOR_BOX_HEIGHT), true, false);
			cont_minimap_legend.new_component<gui_label_t>(pax_dest_type[i]);
		}
		cont_destination_map.add_component(&cont_minimap_legend);
	}
	cont_destination_map.end_table();
	cont_minimap_legend.set_visible(false);

	update_stats();

	update_labels();
	set_resizemode(diagonal_resize);
	reset_min_windowsize();
}


city_info_t::~city_info_t()
{
	// send rename command if necessary
	rename_city();
	// save button state
	uint32 flags = 0;
	FOR(const vector_tpl<gui_button_to_chart_t*>, b2c, button_to_chart.list()) {
		if (b2c->get_button()->pressed) {
			flags |= 1 << b2c->get_curve();
		}
	}
	city->stadtinfo_options = flags;
}


// returns position of the city center on the map
koord3d city_info_t::get_weltpos(bool)
{
	const grund_t* gr = welt->lookup_kartenboden(city->get_center());
	const koord3d k = gr ? gr->get_pos() : koord3d::invalid;
	return k;
}


bool city_info_t::is_weltpos()
{
	return (welt->get_viewport()->is_on_center( get_weltpos(false)));
}


/**
 * send rename command if necessary
 */
void city_info_t::rename_city()
{
	if(  welt->get_cities().is_contained(city)  ) {
		const char *t = name_input.get_text();
		// only change if old name and current name are the same
		// otherwise some unintended undo if renaming would occur
		if(  t  &&  t[0]  &&  strcmp(t, city->get_name())!=0  &&  strcmp(old_name, city->get_name())==0  ) {
			// text changed => call tool
			cbuffer_t buf;
			buf.printf( "t%u,%s", welt->get_cities().index_of(city), name );
			tool_t *tmp_tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
			tmp_tool->set_default_param( buf );
			welt->set_tool( tmp_tool, welt->get_public_player());
			// since init always returns false, it is safe to delete immediately
			delete tmp_tool;
			// do not trigger this command again
			tstrncpy(old_name, t, sizeof(old_name));
		}
	}
}


void city_info_t::reset_city_name()
{
	// change text input
	if(  welt->get_cities().is_contained(city)  ) {
		tstrncpy(old_name, city->get_name(), sizeof(old_name));
		tstrncpy(name, city->get_name(), sizeof(name));
		name_input.set_text(name, sizeof(name));
	}
}


void gui_city_minimap_t::init_pax_dest( array2d_tpl<PIXVAL> &pax_dest )
{
	const int size_x = welt->get_size().x;
	const int size_y = welt->get_size().y;
	for(  sint16 y = 0;  y < minimaps_size.h;  y++  ) {
		for(  sint16 x = 0;  x < minimaps_size.w;  x++  ) {
			const grund_t *gr = welt->lookup_kartenboden( koord( (x * size_x) / minimaps_size.w, (y * size_y) / minimaps_size.h ) );
			pax_dest.at(x,y) = minimap_t::calc_ground_color(gr, show_contour);
		}
	}
}


void gui_city_minimap_t::add_pax_dest( array2d_tpl<PIXVAL> &pax_dest, const sparse_tpl<PIXVAL>* city_pax_dest )
{
	PIXVAL color;
	koord pos;
	// how large the box in the world?
	const sint16 dd_x = 1+(minimaps_size.w-1)/world()->get_size().x;
	const sint16 dd_y = 1+(minimaps_size.h-1)/world()->get_size().y;

	for(  uint16 i = 0;  i < city_pax_dest->get_data_count();  i++  ) {
		city_pax_dest->get_nonzero(i, pos, color);

		// calculate display position according to minimap size
		const sint16 x0 = (pos.x*minimaps_size.w)/world()->get_size().x;
		const sint16 y0 = (pos.y*minimaps_size.h)/world()->get_size().y;

		for(  sint32 y=0;  y<dd_y  &&  y+y0<minimaps_size.h;  y++  ) {
			for(  sint32 x=0;  x<dd_x  &&  x+x0<minimaps_size.w;  x++  ) {
				pax_dest.at( x+x0, y+y0 ) = color;
			}
		}
	}
}


void city_info_t::update_labels()
{
	stadt_t* const c = city;

	// display city stats
	lb_size.buf().printf("%.2f %s", c->get_land_area(), translator::translate("sq. km.")); lb_size.update();
	lb_buildings.buf().printf("%d/%s", c->get_population_density(), translator::translate("sq. km.")); lb_buildings.update();

	const koord ul = c->get_linksoben();
	const koord lr = c->get_rechtsunten();
	lb_border.buf().printf( "%d,%d - %d,%d", ul.x, ul.y, lr.x , lr.y); lb_border.update();

	lb_powerdemand.buf().printf("%s: ", translator::translate("Power demand"));
	const uint32 power_demand = (c->get_power_demand())>>POWER_TO_MW;
	if(power_demand == 0)
	{
		lb_powerdemand.buf().append((c->get_power_demand() * 1000)>>POWER_TO_MW);
		lb_powerdemand.buf().append(" KW");
	}
	else if(power_demand < 1000)
	{
		lb_powerdemand.buf().append(power_demand);
		lb_powerdemand.buf().append(" MW");
	}
	else
	{
		lb_powerdemand.buf().append(power_demand / 1000);
		lb_powerdemand.buf().append(" GW");
	}
	lb_powerdemand.update();
}


void city_info_t::update_stats()
{
	scr_coord_val value_cell_width = max(proportional_string_width(translator::translate("This Year")), proportional_string_width(translator::translate("Last Year")));
	cont_city_stats.remove_all();

	cont_city_stats.new_component<gui_heading_t>("Statistics", SYSCOL_TEXT, env_t::default_window_title_color, 1)->set_width(D_DEFAULT_WIDTH - D_MARGINS_X - D_H_SPACE);
	cont_city_stats.add_table(6,0)->set_spacing(scr_size(D_H_SPACE*2,1));
	{
		// header
		cont_city_stats.new_component<gui_empty_t>();
		cont_city_stats.new_component<gui_empty_t>();
		cont_city_stats.new_component<gui_label_t>("citicens", SYSCOL_TEXT, gui_label_t::centered);
		cont_city_stats.new_component<gui_label_t>("Jobs", SYSCOL_TEXT, gui_label_t::centered);
		cont_city_stats.new_component<gui_label_t>("Passagierrate", SYSCOL_TEXT, gui_label_t::centered);
		cont_city_stats.new_component<gui_fill_t>();

		// data
		cont_city_stats.new_component_span<gui_label_t>("Total", 2);
		cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered)->buf().append(city->get_city_population());
		cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered)->buf().append(city->get_city_jobs());
		cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::centered)->buf().append(city->get_city_visitor_demand());
		cont_city_stats.new_component<gui_empty_t>();

		// demand
		cont_city_stats.new_component_span<gui_label_t>("(ci_demands)", 2);
		cont_city_stats.new_component<gui_label_buf_t>(city->get_homeless() < 0 ? SYSCOL_DOWN_TRIANGLE : SYSCOL_UP_TRIANGLE, gui_label_t::centered)->buf().append(city->get_homeless());
		cont_city_stats.new_component<gui_label_buf_t>(city->get_unemployed()<0 ? SYSCOL_DOWN_TRIANGLE : SYSCOL_UP_TRIANGLE, gui_label_t::centered)->buf().append(city->get_unemployed());
		cont_city_stats.new_component<gui_label_t>("-", SYSCOL_TEXT_WEAK, gui_label_t::centered);
		cont_city_stats.new_component<gui_empty_t>();

		// classes
		for (uint8 c = 0; c < goods_manager_t::passengers->get_number_of_classes(); c++) {
			cont_city_stats.new_component<gui_margin_t>(D_MARGIN_LEFT);
			cont_city_stats.new_component<gui_label_t>(goods_manager_t::get_translated_wealth_name(goods_manager_t::INDEX_PAS, c));
			cont_city_stats.new_component<gui_data_bar_t>()->init((sint64)city->get_population_by_class(c),     city->get_city_population(),     value_cell_width*2, color_idx_to_rgb(COL_DARK_GREEN+1),       false, true);
			cont_city_stats.new_component<gui_data_bar_t>()->init((sint64)city->get_jobs_by_class(c),           city->get_city_jobs(),           value_cell_width*2, color_idx_to_rgb(COL_COMMUTER),           false, true);
			cont_city_stats.new_component<gui_data_bar_t>()->init((sint64)city->get_visitor_demand_by_class(c), city->get_city_visitor_demand(), value_cell_width*2, goods_manager_t::passengers->get_color(), false, true);
			cont_city_stats.new_component<gui_fill_t>();
		}
		cont_city_stats.new_component_span<gui_border_t>(5);
		cont_city_stats.new_component<gui_fill_t>();

		// area
		cont_city_stats.new_component_span<gui_label_t>("City size",2);
		cont_city_stats.add_component(&lb_size);
		cont_city_stats.new_component_span<gui_empty_t>(2);
		cont_city_stats.new_component<gui_fill_t>();

		cont_city_stats.new_component_span<gui_label_t>("Population density",2);
		cont_city_stats.add_component(&lb_buildings);
		cont_city_stats.new_component_span<gui_empty_t>(2);
		cont_city_stats.new_component<gui_fill_t>();

	}
	cont_city_stats.end_table();
	cont_city_stats.new_component<gui_margin_t>(0, D_V_SPACE);


	// city transportation network quality
	cont_city_stats.new_component<gui_heading_t>("Success rate", SYSCOL_TEXT, env_t::default_window_title_color, 1)->set_width(D_DEFAULT_WIDTH - D_MARGINS_X - D_H_SPACE);
	cont_city_stats.add_table(5, 0)->set_spacing(scr_size(D_H_SPACE, D_V_SPACE/2));
	{
		// header
		cont_city_stats.new_component<gui_margin_t>(8);
		cont_city_stats.new_component<gui_margin_t>(D_BUTTON_WIDTH);
		cont_city_stats.new_component<gui_label_t>("This Year");
		cont_city_stats.new_component<gui_label_t>("Last Year");
		cont_city_stats.new_component<gui_fill_t>();

		cont_city_stats.new_component<gui_image_t>(skinverwaltung_t::passengers->get_image_id(0), 0,0, true);
		cont_city_stats.new_component<gui_label_t>("ratio_pax");

		gui_label_buf_t *lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		sint64 sum = city->get_finance_history_year(0, HIST_PAS_TRANSPORTED)+city->get_finance_history_year(0, HIST_CITYCARS)+city->get_finance_history_year(0, HIST_PAS_WALKED);
		lb->set_fixed_width(value_cell_width);
		lb->buf().printf("%.1f%%",
			city->get_finance_history_year(0, HIST_PAS_GENERATED) > 0 ?
			(float)(100.0*sum / city->get_finance_history_year(0, HIST_PAS_GENERATED)) : 0.0);
		lb->update();

		sum = city->get_finance_history_year(1, HIST_PAS_TRANSPORTED) + city->get_finance_history_year(1, HIST_CITYCARS) + city->get_finance_history_year(1, HIST_PAS_WALKED);
		lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->set_fixed_width(value_cell_width);
		lb->buf().printf("%.1f%%",
			city->get_finance_history_year(1, HIST_PAS_GENERATED) > 0 ?
			(float)(100.0*sum / city->get_finance_history_year(1, HIST_PAS_GENERATED)) : 0.0);
		lb->update();
		cont_city_stats.new_component<gui_fill_t>();

		cont_city_stats.new_component<gui_empty_t>();
		cont_city_stats.new_component<gui_label_t>("pas_transportation_ratio")->set_tooltip("helptxt_pas_transportation_ratio");
		cont_city_stats.add_component(&transportation_this_year);
		cont_city_stats.add_component(&transportation_last_year);
		cont_city_stats.new_component<gui_fill_t>();

		// mail
		cont_city_stats.new_component<gui_image_t>(skinverwaltung_t::mail->get_image_id(0), 0, 0, true);
		cont_city_stats.new_component<gui_label_t>("ratio_mail");
		lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->set_fixed_width(value_cell_width);
		lb->buf().printf("%.1f%%",
			city->get_finance_history_year(0, HIST_MAIL_GENERATED) > 0 ?
			(float)(100.0*city->get_finance_history_year(0, HIST_MAIL_TRANSPORTED) / city->get_finance_history_year(0, HIST_MAIL_GENERATED)) : 0.0);
		lb->update();
		lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->set_fixed_width(value_cell_width);
		lb->buf().printf("%.1f%%",
			city->get_finance_history_year(1, HIST_MAIL_GENERATED) > 0 ?
			(float)(100.0*city->get_finance_history_year(1, HIST_MAIL_TRANSPORTED)/city->get_finance_history_year(1, HIST_MAIL_GENERATED)) : 0.0);
		lb->update();
		cont_city_stats.new_component<gui_fill_t>();

		// goods
		cont_city_stats.new_component<gui_image_t>(skinverwaltung_t::goods->get_image_id(0), 0, 0, true);
		cont_city_stats.new_component<gui_label_t>("ratio_goods");
		lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->set_fixed_width(value_cell_width);
		sint64 goods_needed = city->get_finance_history_year(0, HIST_GOODS_NEEDED);
		if (goods_needed>0) {
			lb->buf().printf("%.1f%%",
				(float)(100.0*city->get_finance_history_year(0, HIST_GOODS_RECEIVED) / goods_needed));
		}
		else {
			lb->buf().append("-");
		}
		lb->update();

		lb = cont_city_stats.new_component<gui_label_buf_t>(SYSCOL_TEXT, gui_label_t::right);
		lb->set_fixed_width(value_cell_width);
		goods_needed = city->get_finance_history_year(1, HIST_GOODS_NEEDED);
		if (goods_needed > 0) {
			lb->buf().printf("%.1f%%",
				(float)(100.0*city->get_finance_history_year(1, HIST_GOODS_RECEIVED) / goods_needed));
		}
		else {
			lb->buf().append("-");
		}
		lb->update();
		cont_city_stats.new_component<gui_fill_t>();

	}
	cont_city_stats.end_table();

	// legend of passenger transportaion
	cont_city_stats.add_table(9,1)->set_margin(scr_size(D_MARGIN_LEFT,D_V_SPACE), scr_size(0,D_V_SPACE));
	{
		cont_city_stats.new_component<gui_colorbox_t>(color_idx_to_rgb(hist_type_color[HIST_PAS_TRANSPORTED]))->set_size(scr_size((D_INDICATOR_HEIGHT+LINESPACE)/2+2, (D_INDICATOR_HEIGHT+LINESPACE)/2+2));
		cont_city_stats.new_component<gui_label_t>(hist_type[HIST_PAS_TRANSPORTED]);
		cont_city_stats.new_component<gui_margin_t>(D_H_SPACE*2);
		cont_city_stats.new_component<gui_colorbox_t>(color_idx_to_rgb(hist_type_color[HIST_CITYCARS]))->set_size(scr_size((D_INDICATOR_HEIGHT+LINESPACE)/2+2, (D_INDICATOR_HEIGHT+LINESPACE)/2+2));
		cont_city_stats.new_component<gui_label_t>(hist_type[HIST_CITYCARS]);
		cont_city_stats.new_component<gui_margin_t>(D_H_SPACE*2);
		cont_city_stats.new_component<gui_colorbox_t>(color_idx_to_rgb(hist_type_color[HIST_PAS_WALKED]))->set_size(scr_size((D_INDICATOR_HEIGHT+LINESPACE)/2+2, (D_INDICATOR_HEIGHT+LINESPACE)/2+2));
		cont_city_stats.new_component<gui_label_t>(hist_type[HIST_PAS_WALKED]);
		cont_city_stats.new_component<gui_fill_t>();
	}
	cont_city_stats.end_table();

	reset_min_windowsize();
}


void city_info_t::draw(scr_coord pos, scr_size size)
{
	// update chart seed
	chart.set_seed(welt->get_last_year());
	update_labels();
	if (tabs.get_aktives_tab() == &scrolly_stats) {
		transportation_pas[0] = city->get_finance_history_year(0, HIST_PAS_TRANSPORTED);
		transportation_pas[1] = city->get_finance_history_year(0, HIST_CITYCARS);
		transportation_pas[2] = city->get_finance_history_year(0, HIST_PAS_WALKED);
		transportation_pas[3] = city->get_finance_history_year(1, HIST_PAS_TRANSPORTED);
		transportation_pas[4] = city->get_finance_history_year(1, HIST_CITYCARS);
		transportation_pas[5] = city->get_finance_history_year(1, HIST_PAS_WALKED);

		if (update_seed != city->get_city_population()- city->get_city_jobs() || welt->get_current_month() != old_month) {
			update_seed = city->get_city_population() - city->get_city_jobs();
			old_month = welt->get_current_month();
			update_stats();
		}
	}
	const scr_coord_val margin_above_tab = name_input.get_pos().y + name_input.get_size().h + D_V_SPACE * 2 + allow_growth.get_pos().y + allow_growth.get_size().h;
	tabs.set_pos(scr_coord(0, margin_above_tab));
	tabs.set_size(scr_size(tabs.get_size().w, get_client_windowsize().h-margin_above_tab));
	gui_frame_t::draw(pos, size);
}


bool city_info_t::action_triggered( gui_action_creator_t *comp,value_t /* */)
{
	static char param[16];
	if(  comp==&allow_growth  ) {
		sprintf(param,"g%hi,%hi,%hi", city->get_pos().x, city->get_pos().y, (short)(!city->get_citygrowth()) );
		tool_t::simple_tool[TOOL_CHANGE_CITY]->set_default_param( param );
		welt->set_tool( tool_t::simple_tool[TOOL_CHANGE_CITY], welt->get_active_player());
		return true;
	}
	if(  comp==&name_input  ) {
		// send rename command if necessary
		rename_city();
	}
	if(  comp==&bt_show_contour  ) {
		// terrain heights color scale
		bt_show_contour.pressed = !bt_show_contour.pressed;
		pax_map.set_show_contour(bt_show_contour.pressed);
		return true;
	}
	if(  comp==&bt_show_hide_legend  ) {
		bt_show_hide_legend.pressed = !bt_show_hide_legend.pressed;
		bt_show_hide_legend.set_text(bt_show_hide_legend.pressed ? "-" : "+");
		lb_collapsed.set_visible(!bt_show_hide_legend.pressed);
		cont_minimap_legend.set_visible(bt_show_hide_legend.pressed);
		return true;
	}
	return false;
}


void city_info_t::map_rotate90( sint16 )
{
	pax_map.set_size( pax_map.get_size() );
}


// current task: just update the city pointer ...
bool city_info_t::infowin_event(const event_t *ev)
{
	if(  IS_WINDOW_TOP(ev)  ) {
		minimap_t::get_instance()->set_selected_city( city );
	}

	return gui_frame_t::infowin_event(ev);
}


void city_info_t::update_data()
{
	allow_growth.pressed = city->get_citygrowth();
	if(  strcmp(old_name, city->get_name())!=0  ) {
		reset_city_name();
	}
	set_dirty();
}

/********** dialog restoring after saving stuff **********/
void city_info_t::rdwr(loadsave_t *file)
{
	// window size
	scr_size size = get_windowsize();
	size.rdwr( file );

	uint32 townindex;
	if (file->is_saving()) {
		townindex = welt->get_cities().index_of(city);
	}
	file->rdwr_long( townindex );

	if(  file->is_loading()  ) {
		city = welt->get_cities()[townindex];
		pax_map.set_city(city);

		init();
		win_set_magic( this, (ptrdiff_t)city );
		reset_min_windowsize();
		set_windowsize(size);
	}

	// button-to-chart array
	button_to_chart.rdwr(file);

	year_month_tabs.rdwr(file);

	if (city == NULL) {
		destroy_win(this);
	}
}
