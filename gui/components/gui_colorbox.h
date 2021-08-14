/*
 * This file is part of the Simutrans-Extended project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_COMPONENTS_GUI_COLORBOX_H
#define GUI_COMPONENTS_GUI_COLORBOX_H


#include "gui_component.h"
#include "../../simcolor.h"

// for gui_vehicle_number_t
#include "../../descriptor/vehicle_desc.h"
#include "../../utils/cbuffer_t.h"

/**
 * Draws a simple colored box.
 */
class gui_colorbox_t : public gui_component_t
{
	PIXVAL color;

	scr_coord_val height = D_INDICATOR_HEIGHT;
	scr_coord_val width = D_INDICATOR_WIDTH;
	bool size_fixed = false;
	bool show_frame = true;

	const char * tooltip;

	scr_size max_size;
public:
	gui_colorbox_t(PIXVAL c = 0);

	void init(PIXVAL color_par, scr_size size, bool fixed=false, bool show_frame=true) {
		set_color(color_par);
		set_size(size);
		set_size_fixed(fixed);
		set_show_frame(show_frame);
	}

	void draw(scr_coord offset) OVERRIDE;

	void set_color(PIXVAL c)
	{
		color = c;
	}

	scr_size get_min_size() const OVERRIDE;

	scr_size get_max_size() const OVERRIDE;

	void set_size(scr_size size) OVERRIDE { width = size.w; height = size.h; max_size =size; };
	void set_size_fixed(bool yesno) { size_fixed = yesno; };
	void set_show_frame(bool yesno) { show_frame = yesno; };

	void set_tooltip(const char * t);

	void set_max_size(scr_size s)
	{
		max_size = s;
	}
};


/**
 * Draws a colored bar to represent the role and status of the vehicle
 */
class gui_vehicle_bar_t : public gui_component_t
{
protected:
	PIXVAL color;

	uint8 flags_left;
	uint8 flags_right;
	uint8 interactivity;

public:
	gui_vehicle_bar_t(PIXVAL = COL_DANGER, scr_size size=scr_size(VEHICLE_BAR_HEIGHT*4, VEHICLE_BAR_HEIGHT));

	void init(PIXVAL color_par, scr_size size=scr_size(VEHICLE_BAR_HEIGHT*4, VEHICLE_BAR_HEIGHT)) {
		set_color(color_par);
		set_size(size);
	}

	void set_flags(uint8 flags_left_, uint8 flags_right_, uint8 interactivity_);

	void draw(scr_coord offset) OVERRIDE;

	void set_color(PIXVAL c) { color = c; }

	scr_size get_min_size() const OVERRIDE { return size; };
	scr_size get_max_size() const OVERRIDE { return size; };
};


class gui_vehicle_number_t : public gui_vehicle_bar_t
{
	bool show_frame;
	cbuffer_t buf;

	void init();

public:
	gui_vehicle_number_t(const char* text_=NULL, PIXVAL bgcol = COL_SAFETY, PIXVAL textcol = color_idx_to_rgb(COL_WHITE), bool show_frame_ = true) :
		gui_vehicle_bar_t(bgcol) {
		show_frame = show_frame_;
		set_flags(vehicle_desc_t::can_be_head, vehicle_desc_t::can_be_head|vehicle_desc_t::can_be_tail, HAS_POWER | BIDIRECTIONAL);
		set_text(text_);
	}

	void set_text(const char* text_) {
		if (text_) {
			buf.clear();
			buf.append(text_);
		}
		init();
	}
	void draw(scr_coord offset) OVERRIDE;
};

#endif
