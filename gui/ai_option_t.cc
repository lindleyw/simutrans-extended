/*
 * Copyright (c) 2006 prissi
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

/*
 * AI behavior options from AI finance window
 * 2006 prissi
 */

#include <stdio.h>



#include "../player/ai.h"

#include "../besch/skin_besch.h"
#include "../dataobj/translator.h"
#include "../dataobj/environment.h"
#include "ai_option_t.h"

#define BUTTON_ROW (110+20)


ai_option_t::ai_option_t( player_t *player ) :
	gui_frame_t( translator::translate("Configure AI"), player ),
	label_cs( "construction speed" )
{
	this->ai = dynamic_cast<ai_t *>(player);

	scr_coord_val ypos = 4;

	label_cs.set_pos( scr_coord( 10, ypos ) );
	add_component( &label_cs );
	ypos += LINESPACE;

	construction_speed.init( ai->get_construction_speed(), 25, 1000000, gui_numberinput_t::POWER2, false );
	construction_speed.set_pos( scr_coord( 10, ypos ) );
	construction_speed.set_size( scr_size( 120, D_BUTTON_HEIGHT ) );
	construction_speed.add_listener( this );
	add_component( &construction_speed );

	ypos += D_BUTTON_HEIGHT+4;

	// find out if the mode is available and can be activated

	buttons[0].init( button_t::square_state, "road vehicle", scr_coord(10,ypos), scr_size( 120, D_BUTTON_HEIGHT ) );
	buttons[0].pressed = ai->has_road_transport();
	buttons[0].add_listener( this );
	ai->set_road_transport( !buttons[0].pressed );
	if(  buttons[0].pressed==ai->has_road_transport()  ) {
		if(  ai->has_road_transport()  ) {
			buttons[0].disable();
			add_component( buttons+0 );
			ypos += D_BUTTON_HEIGHT+2;
		}
	}
	else {
		ai->set_road_transport( buttons[0].pressed );
		add_component( buttons+0 );
		ypos += D_BUTTON_HEIGHT+2;
	}

	buttons[1].init( button_t::square_state, "rail car", scr_coord(10,ypos), scr_size( 120, D_BUTTON_HEIGHT ) );
	buttons[1].pressed = ai->has_rail_transport();
	buttons[1].add_listener( this );
	ai->set_rail_transport( !buttons[1].pressed );
	if(  buttons[1].pressed==ai->has_rail_transport()  ) {
		if(  ai->has_rail_transport()  ) {
			buttons[1].disable();
			add_component( buttons+1 );
			ypos += D_BUTTON_HEIGHT+2;
		}
	}
	else {
		ai->set_rail_transport( buttons[1].pressed );
		add_component( buttons+1 );
		ypos += D_BUTTON_HEIGHT+2;
	}

	buttons[2].init( button_t::square_state, "water vehicle", scr_coord(10,ypos), scr_size( 120, D_BUTTON_HEIGHT ) );
	buttons[2].pressed = ai->has_ship_transport();
	buttons[2].add_listener( this );
	ai->set_ship_transport( !buttons[2].pressed );
	if(  buttons[2].pressed==ai->has_ship_transport()  ) {
		if(  ai->has_ship_transport()  ) {
			buttons[2].disable();
			add_component( buttons+2 );
			ypos += D_BUTTON_HEIGHT+2;
		}
	}
	else {
		ai->set_ship_transport( buttons[2].pressed );
		add_component( buttons+2 );
		ypos += D_BUTTON_HEIGHT+2;
	}

	buttons[3].init( button_t::square_state, "airplane", scr_coord(10,ypos), scr_size( 120, D_BUTTON_HEIGHT ) );
	buttons[3].pressed = ai->has_air_transport();
	buttons[3].add_listener( this );
	ai->set_air_transport( !buttons[3].pressed );
	if(  buttons[3].pressed==ai->has_air_transport()  ) {
		if(  ai->has_air_transport()  ) {
			buttons[3].disable();
			add_component( buttons+3 );
			ypos += D_BUTTON_HEIGHT+2;
		}
	}
	else {
		ai->set_air_transport( buttons[3].pressed );
		add_component( buttons+3 );
		ypos += D_BUTTON_HEIGHT+2;
	}

	set_windowsize( scr_size(140, 18+ypos) );
}


bool ai_option_t::action_triggered( gui_action_creator_t *comp, value_t v )
{
	if(  comp==&construction_speed  ) {
		ai->set_construction_speed( v.i );
	}
	else if(  comp==buttons+0  ) {
		ai->set_road_transport( !buttons[0].pressed );
		buttons[0].pressed = ai->has_road_transport();
	}
	else if(  comp==buttons+1  ) {
		ai->set_rail_transport( !buttons[1].pressed );
		buttons[1].pressed = ai->has_rail_transport();
	}
	else if(  comp==buttons+2  ) {
		ai->set_ship_transport( !buttons[2].pressed );
		buttons[2].pressed = ai->has_ship_transport();
	}
	else if(  comp==buttons+3  ) {
		ai->set_air_transport( !buttons[3].pressed );
		buttons[3].pressed = ai->has_air_transport();
	}
	return true;
}
