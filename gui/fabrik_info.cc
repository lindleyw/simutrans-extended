/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "fabrik_info.h"

#include "components/gui_button.h"

#include "help_frame.h"
#include "factory_chart.h"

#include "../simfab.h"
#include "../simcolor.h"
#include "../display/simgraph.h"
#include "../display/viewport.h"
#include "../simcity.h"
#include "../gui/simwin.h"
#include "../simmenu.h"
#include "../player/simplay.h"
#include "../simworld.h"
#include "../simskin.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "../utils/simstring.h"
#include "../bauer/goods_manager.h"


static const char factory_status_type[fabrik_t::MAX_FAB_STATUS][64] =
{
	"", "", "", "", "", // status not problematic
	"Storage full",
	"Inactive", "Shipment stuck",
	"Material shortage", "No material",
	"stop_some_goods_arrival", "Fully stocked",
	"fab_stuck",
	"staff_shortage"
};

static const int fab_alert_level[fabrik_t::MAX_FAB_STATUS] =
{
	0, 0, 0, 0, 0, // status not problematic
	1,
	2, 2,
	2, 2,
	3, 3,
	4,
	0
};

sint16 fabrik_info_t::tabstate = 0;

fabrik_info_t::fabrik_info_t(fabrik_t* fab_, const gebaeude_t* gb) :
	gui_frame_t("", fab_->get_owner()),
	storage(fab_),
	fab(fab_),
	goods_chart(fab_),
	chart(fab_),
	lbl_factory_status(factory_status),
	view(gb, scr_size( max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	scrolly_info(&container_info),
	scrolly_details(&container_details),
	all_suppliers(fab_, true),
	all_consumers(fab_, false),
	nearby_halts(fab_),
	prod(&prod_buf),
	txt(&info_buf)
{
	staffing_level = staffing_level2 = staff_shortage_factor = 0;

	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name( fabname );

	input.set_pos(scr_coord(D_MARGIN_LEFT,D_MARGIN_TOP));
	input.set_text( fabname, lengthof(fabname) );
	input.add_listener(this);
	add_component(&input);

	add_component(&view);

	prod.set_pos( scr_coord( D_MARGIN_LEFT, D_MARGIN_TOP+D_BUTTON_HEIGHT+D_V_SPACE+LINESPACE ) );
	fab->info_prod( prod_buf );
	prod.recalc_size();
	add_component( &prod );

	const sint16 offset_below_viewport = D_MARGIN_TOP+D_BUTTON_HEIGHT+D_V_SPACE+ max( prod.get_size().h + storage.get_size().h, view.get_size().h + 8 ) + LINESPACE;

	storage.set_pos(scr_coord(0, offset_below_viewport));
	storage.recalc_size();
	add_component(&storage);

	tabs.set_pos(scr_coord(0, offset_below_viewport + storage.get_size().h + LINESPACE));
	tabs.set_size(scr_size(D_DEFAULT_WIDTH-2, D_TAB_HEADER_HEIGHT + 70 + LINESPACE*2));

	set_min_windowsize(scr_size(max(D_DEFAULT_WIDTH, 250+view.get_size().w), D_TAB_HEADER_HEIGHT + tabs.get_pos().y + D_TAB_HEADER_HEIGHT));

	// calculate height
	fab->info_prod(prod_buf);

	// fill position buttons etc
	fab->info_conn(info_buf);
	txt.recalc_size();
	update_info();

	// tab1 - connections
	container_info.set_pos(scr_coord(0, D_MARGIN_TOP));
	scr_coord_val y = 0; // calc for layout
	lb_suppliers.set_visible(false);
	lb_suppliers.init(translator::translate("Suppliers"), scr_coord(D_H_SPACE, y));
	if (all_suppliers.get_size().h) {
		lb_suppliers.set_visible(true);
		y += LINESPACE;
	}
	all_suppliers.set_pos(scr_coord(0, y));
	y += all_suppliers.get_size().h;
	if (all_suppliers.get_size().h) {
		y += LINESPACE;
	}

	lb_consumers.set_visible(false);
	lb_consumers.init(translator::translate("Abnehmer"), scr_coord(D_H_SPACE, y));
	if (all_consumers.get_size().h) {
		lb_consumers.set_visible(true);
		y += LINESPACE;
	}
	all_consumers.set_pos(scr_coord(0, y));
	y += all_consumers.get_size().h;
	y += LINESPACE;

	lb_nearby_halts.init(translator::translate("Connected stops (freight)"), scr_coord(D_H_SPACE, y));
	y += LINESPACE;
	nearby_halts.set_pos(scr_coord(0, y));
	y += nearby_halts.get_size().h;
	y += D_MARGIN_BOTTOM;

	container_info.add_component(&lb_suppliers);
	container_info.add_component(&all_suppliers);
	container_info.add_component(&lb_consumers);
	container_info.add_component(&all_consumers);
	container_info.add_component(&lb_nearby_halts);
	container_info.add_component(&nearby_halts);
	container_info.set_size(scr_size(400, y));

	// tab4 - building info
	container_details.init(scr_coord(0, D_MARGIN_TOP), scr_size(D_BUTTON_WIDTH*3, txt.get_size().h));

	// Hajo: "About" button only if translation is available
	char key[256];
	sprintf(key, "factory_%s_details", fab->get_desc()->get_name());
	const char * value = translator::translate(key);
	if(value && *value != 'f') {
		details_button.init( button_t::roundbox, "Details", scr_coord(BUTTON4_X,offset_below_viewport), scr_size(D_BUTTON_WIDTH, D_BUTTON_HEIGHT));
//		details_button.set_tooltip("Factory details");
		details_button.add_listener(this);
		add_component(&details_button);
	}


	tabs.add_tab(&scrolly_info, translator::translate("Connections"));
	tabs.add_tab(&goods_chart, translator::translate("Goods chart"));
	tabs.add_tab(&chart, translator::translate("Production chart"));
	tabs.add_tab(&scrolly_details, translator::translate("Building info."));

	tabs.add_listener(this);
	add_component(&tabs);

	// staffing bar
	add_component(&staffing_bar);

	// The status label
	lbl_factory_status.set_tooltip(translator::translate("staffing_bar_tooltip_help"));
	add_component(&lbl_factory_status);

	set_windowsize(scr_size(D_DEFAULT_WIDTH, D_TAB_HEADER_HEIGHT + tabs.get_pos().y + D_TAB_HEADER_HEIGHT + tabs.get_size().h));

	tabs.set_size(get_client_windowsize() - tabs.get_pos() - scr_size(0, 1));

	set_resizemode(diagonal_resize);
	resize(scr_coord(0,0));
}


fabrik_info_t::~fabrik_info_t()
{
	rename_factory();
	fabname[0] = 0;

	//delete [] stadtbuttons;
}


void fabrik_info_t::rename_factory()
{
	if(  fabname[0]  &&  welt->get_fab_list().is_contained(fab)  &&  strcmp(fabname, fab->get_name())  ) {
		// text changed and factory still exists => call tool
		cbuffer_t buf;
		buf.printf( "f%s,%s", fab->get_pos().get_str(), fabname );
		tool_t *tool = create_tool( TOOL_RENAME | SIMPLE_TOOL );
		tool->set_default_param( buf );
		welt->set_tool( tool, welt->get_public_player());
		// since init always returns false, it is safe to delete immediately
		delete tool;
	}
}


/**
 * Set window size and adjust component sizes and/or positions accordingly
 * @author Markus Weber
 */
void fabrik_info_t::set_windowsize(scr_size size)
{
	gui_frame_t::set_windowsize(size);

	const uint8 alert_icon_with = skinverwaltung_t::alerts ? skinverwaltung_t::alerts->get_image(0)->get_pic()->w + 4 : 0;

	// would be only needed in case of enabling horizontal resizes
	input.set_size(scr_size(get_windowsize().w-D_MARGIN_LEFT-D_MARGIN_RIGHT, D_BUTTON_HEIGHT));
	view.set_pos(scr_coord(get_windowsize().w - view.get_size().w - D_MARGIN_RIGHT , D_MARGIN_TOP+D_BUTTON_HEIGHT+D_V_SPACE ));
	lbl_factory_status.set_pos(scr_coord(get_windowsize().w - view.get_size().w - D_MARGIN_RIGHT, D_MARGIN_TOP + D_BUTTON_HEIGHT + D_V_SPACE*2 + view.get_size().h + D_INDICATOR_HEIGHT+2));
	lbl_factory_status.set_size(scr_size(view.get_size().w - alert_icon_with, LINESPACE));
	staffing_bar.set_pos(scr_coord(view.get_pos().x + 1, view.get_pos().y + view.get_size().h));
	staffing_bar.set_size(scr_size(view.get_size().w-2, D_INDICATOR_HEIGHT));

	tabs.set_size(get_client_windowsize() - tabs.get_pos() - scr_size(0, 1));
}


/**
 * Draw new component. The values to be passed refer to the window
 * i.e. It's the screen coordinates of the window where the
 * component is displayed.
 * @author Hj. Malthaner
 */
void fabrik_info_t::draw(scr_coord pos, scr_size size)
{
	if (world()->closed_factories_this_month.is_contained(fab))
	{
		return;
	}
	const scr_size old_size = txt.get_size();

	fab->info_prod(prod_buf);
	fab->info_conn(info_buf);

	gui_frame_t::draw(pos, size);
	set_dirty();

	if (old_size != txt.get_size()) {
		update_info();
	}

	// staffing bar
	if (fab->get_sector() == fabrik_t::end_consumer) {
		staff_shortage_factor = (sint32)welt->get_settings().get_minimum_staffing_percentage_consumer_industry();
	}
	else if(!(welt->get_settings().get_rural_industries_no_staff_shortage() && fab->get_sector() == fabrik_t::resource)){
		staff_shortage_factor = (sint32)welt->get_settings().get_minimum_staffing_percentage_full_production_producer_industry();
	}
	else {
		//staff_shortage_factor = 0;
	}
	staffing_bar.add_color_value(&staff_shortage_factor, COL_YELLOW);
	staffing_level = fab->get_staffing_level_percentage();
	const goods_desc_t *wtyp = goods_manager_t::get_info((uint16)0);
	staffing_bar.add_color_value(&staffing_level, wtyp->get_color());
	staffing_level2 = staff_shortage_factor > staffing_level ? staffing_level : 0;
	staffing_bar.add_color_value(&staffing_level2, COL_STAFF_SHORTAGE);

	int left = D_MARGIN_LEFT + D_INDICATOR_WIDTH + D_H_SPACE;
	int top = pos.y + view.get_pos().y + D_TITLEBAR_HEIGHT;

	// status color bar
	if (fab->get_status() >= fabrik_t::staff_shortage) {
		display_ddd_box_clip(pos.x + D_MARGIN_LEFT - 1, top + 1, D_INDICATOR_WIDTH + 2, D_INDICATOR_HEIGHT + 2, COL_STAFF_SHORTAGE, COL_STAFF_SHORTAGE);
	}
	unsigned indikatorfarbe = fabrik_t::status_to_color[fab->get_status() % fabrik_t::staff_shortage];
	display_fillbox_wh_clip(pos.x + D_MARGIN_LEFT, top + 2, D_INDICATOR_WIDTH, D_INDICATOR_HEIGHT, indikatorfarbe, true);
	// Status line written text
	if(skinverwaltung_t::alerts && fab_alert_level[fab->get_status() % fabrik_t::staff_shortage]){
		left += D_H_SPACE * 2;
		display_color_img(skinverwaltung_t::alerts->get_image_id(fab_alert_level[fab->get_status() % fabrik_t::staff_shortage]), pos.x + left, top, 0, false, false);
		left += 13;
	}
	prod_buf.clear();
	if (factory_status_type[fab->get_status() % fabrik_t::staff_shortage]) {
		prod_buf.append(translator::translate(factory_status_type[fab->get_status()%fabrik_t::staff_shortage]));
	}
	display_proportional(pos.x + left, top, prod_buf, ALIGN_LEFT, indikatorfarbe, true);

	prod_buf.clear();

	display_ddd_box_clip(pos.x + view.get_pos().x, top + view.get_size().h, view.get_size().w, D_INDICATOR_HEIGHT + 2, MN_GREY0, MN_GREY4);
	// tooltip for staffing_bar
	if (abs((int)(pos.x + view.get_pos().x + view.get_size().h/2 - get_mouse_x())) < view.get_size().h/2 && abs((int)(top + view.get_size().h + (D_INDICATOR_HEIGHT+2)/2 - get_mouse_y())) < (D_INDICATOR_HEIGHT+2)/2) {
		prod_buf.append(translator::translate("staffing_bar_tooltip_help"));
		win_set_tooltip(pos.x + view.get_pos().x, top + view.get_size().h + D_INDICATOR_HEIGHT + 2, prod_buf);
		prod_buf.clear();
	}

	scr_coord_val x_boost_symbol_pos = proportional_string_width(translator::translate("Productivity")) + proportional_string_width(" : 000% ") + proportional_string_width(translator::translate("(Max. %u%%)")) + D_MARGIN_LEFT + D_H_SPACE;
	if (skinverwaltung_t::electricity->get_image_id(0) != IMG_EMPTY) {
		// indicator for receiving
		if (fab->get_desc()->get_electric_boost()) {
			if (fab->get_prodfactor_electric() > 0) {
				display_color_img(skinverwaltung_t::electricity->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, 0, false, false);
			}
			else {
				display_img_blend(skinverwaltung_t::electricity->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, TRANSPARENT50_FLAG | OUTLINE_FLAG | COL_GREY2, false, false);
			}
			x_boost_symbol_pos += skinverwaltung_t::electricity->get_image(0)->get_pic()->w + 4;
		}
	}

	if (skinverwaltung_t::passengers->get_image_id(0) != IMG_EMPTY) {
		if (fab->get_desc()->get_pax_boost()) {
			if (fab->get_prodfactor_pax() > 0) {
				display_color_img(skinverwaltung_t::passengers->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, 0, false, false);
			}
			else {
				display_img_blend(skinverwaltung_t::passengers->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, TRANSPARENT50_FLAG | OUTLINE_FLAG | COL_GREY2, false, false);
			}
			x_boost_symbol_pos += skinverwaltung_t::passengers->get_image(0)->get_pic()->w + 4;
		}
	}
	if (skinverwaltung_t::mail->get_image_id(0) != IMG_EMPTY) {
		if (fab->get_desc()->get_mail_boost()) {
			if (fab->get_prodfactor_mail() > 0) {
				display_color_img(skinverwaltung_t::mail->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, 0, false, false);
			}
			else {
				display_img_blend(skinverwaltung_t::mail->get_image_id(0), pos.x + x_boost_symbol_pos, top + LINESPACE, TRANSPARENT50_FLAG | OUTLINE_FLAG | COL_GREY2, false, false);
			}
		}
	}

	factory_status.clear();
	factory_status.append("");
	if (fab->get_status() >= fabrik_t::staff_shortage) {
		factory_status.append(translator::translate("staff_shortage"));
	}
	lbl_factory_status.set_color(COL_STAFF_SHORTAGE);
}


bool fabrik_info_t::is_weltpos()
{
	return ( welt->get_viewport()->is_on_center( get_weltpos(false) ) );
}


/**
 * This method is called if an action is triggered
 * @author Hj. Malthaner
 *
 * Returns true, if action is done and no more
 * components should be triggered.
 * V.Meyer
 */
bool fabrik_info_t::action_triggered( gui_action_creator_t *comp, value_t v)
{
	if(comp == &input) {
		rename_factory();
	}
	else if(comp == &details_button) {
		help_frame_t * frame = new help_frame_t();
		char key[256];
		sprintf(key, "factory_%s_details", fab->get_desc()->get_name());
		frame->set_text(translator::translate(key));
		create_win(frame, w_info, (ptrdiff_t)this);
	}
	else if (tabstate != tabs.get_active_tab_index() || get_windowsize().h == get_min_windowsize().h) {
		set_tab_opened();
	}

	return true;
}


bool fabrik_info_t::infowin_event(const event_t *ev)
{
	if (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_DOWN) {
		set_tab_opened();
	}
	if (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_UP) {
		set_windowsize(get_min_windowsize());
	}
	if (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_LEFT) {
		tabstate = (tabstate + tabs.get_count() - 1)% tabs.get_count();
		tabs.set_active_tab_index(tabstate);
		set_tab_opened();
	}
	if (ev->ev_class == EVENT_KEYBOARD && ev->ev_code == SIM_KEY_RIGHT) {
		tabstate = (tabstate + 1) % tabs.get_count();
		tabs.set_active_tab_index(tabstate);
		set_tab_opened();
	}

	return gui_frame_t::infowin_event(ev);
}

void fabrik_info_t::set_tab_opened()
{
	tabstate = tabs.get_active_tab_index();
	switch (tabstate)
	{
	case 0: // info
	default:
		tabs.set_size(scrolly_info.get_size());
		set_windowsize(scr_size(get_windowsize().w, tabs.get_pos().y + D_TAB_HEADER_HEIGHT + D_MARGINS_Y + D_V_SPACE * 2 + min(22 * LINESPACE, container_info.get_size().h)));
		break;
	case 1: // goods chart 
		goods_chart.recalc_size();
		set_windowsize(scr_size(get_windowsize().w, tabs.get_pos().y + D_TAB_HEADER_HEIGHT + D_MARGINS_Y + goods_chart.get_size().h));
		break;
	case 2: // prod chart
		chart.recalc_size();
		set_windowsize(scr_size(get_windowsize().w, tabs.get_pos().y + D_TAB_HEADER_HEIGHT + D_MARGINS_Y + chart.get_size().h));
		break;
	case 3: // details 
		set_windowsize(scr_size(get_windowsize().w, tabs.get_pos().y + D_TAB_HEADER_HEIGHT + D_MARGINS_Y + D_V_SPACE * 2 + container_details.get_size().h));
		break;
	}
}


void fabrik_info_t::update_info()
{
	tstrncpy( fabname, fab->get_name(), lengthof(fabname) );
	gui_frame_t::set_name( fabname );
	input.set_text( fabname, lengthof(fabname) );

	// needs to update all text
	txt.set_pos( scr_coord(D_MARGIN_LEFT,0) );
	container_details.add_component(&txt);
	all_suppliers.recalc_size();
	all_consumers.recalc_size();
	nearby_halts.update(fab);
	nearby_halts.recalc_size();




	const sint16 offset_below_viewport = D_MARGIN_TOP + D_BUTTON_HEIGHT + D_V_SPACE + max(prod.get_size().h + storage.get_size().h, view.get_size().h + 8) + LINESPACE*3;
}


// component for city demand icons display
void gui_fabrik_info_t::draw(scr_coord offset)
{
	int xoff = pos.x+offset.x+D_MARGIN_LEFT+16;
	int yoff = pos.y+offset.y;

	gui_container_t::draw( offset );
}


/***************** Saveload stuff from here *****************/


fabrik_info_t::fabrik_info_t() :
	gui_frame_t("", welt->get_public_player()),
	storage(NULL),
	fab(NULL),
	goods_chart(NULL),
	chart(NULL),
	all_suppliers(NULL, true),
	all_consumers(NULL, false),
	nearby_halts(NULL),
	view(scr_size( max(64, get_base_tile_raster_width()), max(56, (get_base_tile_raster_width() * 7) / 8))),
	scrolly_info(&container_info),
	scrolly_details(&container_details),
	prod(&prod_buf),
	txt(&info_buf)
{

	input.set_pos(scr_coord(D_MARGIN_LEFT,D_MARGIN_TOP));
	input.set_text( fabname, lengthof(fabname) );
	input.add_listener(this);
	add_component(&input);

	add_component(&view);

	prod.set_pos( scr_coord( D_MARGIN_LEFT, D_MARGIN_TOP+D_BUTTON_HEIGHT+D_V_SPACE+LINESPACE ) );
	add_component( &prod );

	const sint16 total_width = D_MARGIN_LEFT + 3*(D_BUTTON_WIDTH + D_H_SPACE) + max( D_BUTTON_WIDTH, view.get_size().w ) + D_MARGIN_RIGHT;
	set_min_windowsize(scr_size(total_width, D_TITLEBAR_HEIGHT+LINESPACE*5+D_MARGIN_BOTTOM));

	set_resizemode(diagonal_resize);
}


void fabrik_info_t::rdwr( loadsave_t *file )
{
	scr_size size = get_windowsize();
	//sint32 scroll_y = scrolly.get_scroll_y();
	koord fabpos;
	scr_coord viewpos = view.get_pos();
	scr_size viewsize = view.get_size();
	//scr_coord scrollypos = scrolly.get_pos();

	if(  file->is_saving()  ) {
		fabpos = fab->get_pos().get_2d();
	}

	size.rdwr( file );
	fabpos.rdwr( file );
	viewpos.rdwr( file );
	viewsize.rdwr( file );
	//scrollypos.rdwr( file );
	file->rdwr_str( fabname, lengthof(fabname) );
	//file->rdwr_long( scroll_x );
	//file->rdwr_long( scroll_y );

	if(  file->is_loading()  ) {
		fab = fabrik_t::get_fab(fabpos );

		// will fail on factories with no ground or no building at (0,0)
		view.set_obj( welt->lookup_kartenboden( fabpos )->find<gebaeude_t>() );
		view.set_size( viewsize );
		view.set_pos( viewpos );
		//chart.set_factory( fab );
		//chart.rdwr( file );

		// now put stuff at old positions
		fab->info_prod( prod_buf );
		prod.recalc_size();

		const sint16 offset_below_viewport = D_MARGIN_TOP+D_BUTTON_HEIGHT+D_V_SPACE*2+ max( prod.get_size().h + storage.get_size().h, view.get_size().h + 8 + D_V_SPACE )+ LINESPACE;

		// calculate height
		fab->info_prod(prod_buf);

		// fill position buttons etc
		fab->info_conn(info_buf);
		txt.recalc_size();
		update_info();

		set_min_windowsize(scr_size(get_min_windowsize().w, D_TITLEBAR_HEIGHT+LINESPACE*5+D_MARGIN_BOTTOM));

		// Hajo: "About" button only if translation is available
		char key[256];
		sprintf(key, "factory_%s_details", fab->get_desc()->get_name());
		const char * value = translator::translate(key);
		if(value && *value != 'f') {
			details_button.init( button_t::roundbox, "Details", scr_coord(BUTTON4_X,offset_below_viewport), scr_size(D_BUTTON_WIDTH, D_BUTTON_HEIGHT));
//			details_button.set_tooltip("Factory details");
			details_button.add_listener(this);
			add_component(&details_button);
		}

		/*if(  chart_button.pressed  ) {
			add_component( &chart );
			const scr_coord offset = scr_coord(0, chart.get_size().h - 6);
			details_button.set_pos( details_button.get_pos() + offset );
		}*/
		set_windowsize( size );
		resize( scr_coord(0,0) );
	}
	else {
		//chart.rdwr( file );
	}
}
