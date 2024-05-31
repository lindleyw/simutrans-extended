/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_MONEY_FRAME_H
#define GUI_MONEY_FRAME_H


#include "gui_frame.h"
#include "depotlist_frame.h" // for stats
#include "components/action_listener.h"
#include "components/gui_button.h"
#include "components/gui_button_to_chart.h"
#include "components/gui_label.h"
#include "components/gui_tab_panel.h"
#include "components/gui_chart.h"
#include "components/gui_combobox.h"

#include "../player/finance.h"
#include "../player/simplay.h"
#include "../utils/cbuffer_t.h"

#define MAX_PLAYER_COST_BUTTON (19)

class money_frame_label_t;

/**
 * Finances dialog
 */
class money_frame_t : public gui_frame_t, private action_listener_t
{
	friend class money_frame_label_t;
private:
	cbuffer_t money_frame_title;

	gui_chart_t chart, mchart;

	gui_label_buf_t maintenance_money;
	gui_label_buf_t scenario_desc;
	gui_label_buf_t scenario_completion;
	gui_label_buf_t warn;

	gui_aligned_container_t container_year, container_month, cont_stats;
	gui_scrollpane_t scrolly_stats;

	uint16 transport_types[TT_MAX];
	uint16 transport_type_option;
	gui_combobox_t transport_type_c;

	player_t *player;

	gui_tab_panel_t year_month_tabs;

	button_t headquarter, bt_open_ranking;
	cbuffer_t headquarter_tooltip;

	/// Helper method to query data from players statistics
	sint64 get_statistics_value(int transport_type, uint8 type, int yearmonth, bool monthly);

	sint64 chart_table_month[MAX_PLAYER_HISTORY_MONTHS][MAX_PLAYER_COST_BUTTON];
	sint64 chart_table_year[ MAX_PLAYER_HISTORY_YEARS ][MAX_PLAYER_COST_BUTTON];

	gui_button_to_chart_array_t button_to_chart;

	vector_tpl<money_frame_label_t*> money_labels;

	void update_labels();
	void update_stats();
	void init_stats();

	void fill_chart_tables();

	bool is_chart_table_zero(int ttoption);

	// for stats
	uint16 tt_halt_counts[TT_MAX_VEH-1];
	uint32 tt_way_length[TT_MAX_VEH-1];
	sint64 tt_way_maint[TT_MAX_VEH-1];
	uint32 tt_electrified_len[TT_MAX_VEH-1];
	sint64 tt_electrification_maint[TT_MAX_VEH-1];
	uint32 tt_depot_counts[TT_MAX_VEH-1];
	sint64 tt_depot_maint[TT_MAX_VEH-1];
	uint16 tt_convoy_counts[TT_MAX_VEH-1];
	uint16 tt_inactive_convoy_counts[TT_MAX_VEH-1];
	uint32 tt_vehicle_counts[TT_MAX_VEH-1];
	uint32 tt_vehicle_maint[TT_MAX_VEH-1];
	uint16 tt_sign_counts[TT_MAX_VEH-1];
	uint16 tt_sign_maint[TT_MAX_VEH-1];
	gui_label_buf_t lb_station_counts[TT_MAX_VEH-1];
	gui_label_buf_t lb_way_distances[TT_MAX_VEH-1];
	gui_label_buf_t lb_way_maintenances[TT_MAX_VEH-1];
	gui_label_buf_t lb_electrified_distances[TT_MAX_VEH-1];
	gui_label_buf_t lb_electrification_maint[TT_MAX_VEH-1];
	gui_label_buf_t lb_depots[TT_MAX_VEH-1];
	gui_label_buf_t lb_depots_maint[TT_MAX_VEH-1];
	gui_label_buf_t lb_line_counts[TT_MAX_VEH-1];
	gui_label_buf_t lb_convoy_counts[TT_MAX_VEH-1];
	gui_label_buf_t lb_vehicle_counts[TT_MAX_VEH-1];
	gui_label_buf_t lb_vehicle_maint[TT_MAX_VEH-1];
	gui_label_buf_t lb_sign_counts[TT_MAX_VEH-1];
	gui_label_buf_t lb_sign_maint[TT_MAX_VEH-1];
	gui_label_buf_t lb_total_halts,                          lb_total_electrified_km,        lb_total_depots,      lb_total_active_lines, lb_own_convoy_count, lb_own_vehicle_count, lb_own_sign_count;
	gui_label_buf_t lb_total_halt_maint, lb_total_way_maint, lb_total_electrification_maint, lb_total_depot_maint,                                             lb_total_veh_maint,   lb_total_sign_maint;
	gui_label_buf_t lb_signalbox_count, lb_signalbox_maint;

	button_t bt_access_haltlist, bt_access_depotlist, bt_access_schedulelist, bt_access_convoylist, bt_access_signalboxlist;

public:
	/**
	 * Set the window associated helptext
	 * @return the filename for the helptext, or NULL
	 */
	const char * get_help_filename() const OVERRIDE {return "finances.txt";}

	/**
	 * Constructor. Adds all necessary Subcomponents.
	 */
	money_frame_t(player_t *player);

	~money_frame_t();

	/**
	 * Draw new component. The values to be passed refer to the window
	 * i.e. It's the screen coordinates of the window where the
	 * component is displayed.
	 */
	void draw(scr_coord pos, scr_size size) OVERRIDE;

	/**
	 * Set window size and adjust component sizes and/or positions accordingly
	 */
	void set_windowsize(scr_size size) OVERRIDE;

	bool action_triggered(gui_action_creator_t*, value_t) OVERRIDE;

	bool infowin_event(const event_t *ev) OVERRIDE;

	// saving/restore stuff
	uint32 get_rdwr_id() OVERRIDE;

	void rdwr( loadsave_t *file ) OVERRIDE;
};

#endif
