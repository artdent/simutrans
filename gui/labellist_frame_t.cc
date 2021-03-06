/*
 * Copyright (c) 1997 - 2003 Hansj�rg Malthaner
 *
 * This file is part of the Simutrans project under the artistic licence.
 * (see licence.txt)
 */

#include "labellist_frame_t.h"
#include "labellist_stats_t.h"

#include "../dataobj/translator.h"
#include "../simcolor.h"


/**
 * This variable defines the sort order (ascending or descending)
 * Values: 1 = ascending, 2 = descending)
 * @author Markus Weber
 */
bool labellist_frame_t::sortreverse = false;
bool labellist_frame_t::filter_state = true;

/**
 * This variable defines by which column the table is sorted
 * Values: 0 = label name
 *         1 = label koord
 *         2 = label owner
 * @author Markus Weber
 */
labellist::sort_mode_t labellist_frame_t::sortby = labellist::by_name;

const char *labellist_frame_t::sort_text[labellist::SORT_MODES] = {
	"hl_btn_sort_name",
	"koord",
	"player"
};

labellist_frame_t::labellist_frame_t() :
	gui_frame_t( translator::translate("labellist_title") ),
	sort_label(translator::translate("hl_txt_sort")),
	stats(sortby,sortreverse,filter_state),
	scrolly(&stats)
{
	sort_label.set_pos(scr_coord(BUTTON1_X, 2));
	add_component(&sort_label);

	sortedby.init(button_t::roundbox, "", scr_coord(BUTTON1_X, 14), scr_size(D_BUTTON_WIDTH,D_BUTTON_HEIGHT));
	sortedby.add_listener(this);
	add_component(&sortedby);

	sorteddir.init(button_t::roundbox, "", scr_coord(BUTTON2_X, 14), scr_size(D_BUTTON_WIDTH,D_BUTTON_HEIGHT));
	sorteddir.add_listener(this);
	add_component(&sorteddir);

	filter.init( button_t::square_state, "Active player only", scr_coord(BUTTON3_X+10,14+1) );
	filter.pressed = filter_state;
	add_component(&filter);
	filter.add_listener( this );

	scrolly.set_pos(scr_coord(0,14+D_BUTTON_HEIGHT+2));
	scrolly.set_show_scroll_x(true);
	scrolly.set_scroll_amount_y(LINESPACE+1);
	add_component(&scrolly);

	display_list();

	set_windowsize(scr_size(D_DEFAULT_WIDTH, D_TITLEBAR_HEIGHT+18*(LINESPACE+1)+14+D_BUTTON_HEIGHT+2+1));
	set_min_windowsize(scr_size(D_DEFAULT_WIDTH, D_TITLEBAR_HEIGHT+4*(LINESPACE+1)+14+D_BUTTON_HEIGHT+2+1));

	set_resizemode(diagonal_resize);
	resize(scr_coord(0,0));
}



/**
 * This method is called if an action is triggered
 * @author Markus Weber/Volker Meyer
 */
bool labellist_frame_t::action_triggered( gui_action_creator_t *komp,value_t /* */)
{
	if(komp == &sortedby) {
		set_sortierung((labellist::sort_mode_t)((get_sortierung() + 1) % labellist::SORT_MODES));
		display_list();
	}
	else if(komp == &sorteddir) {
		set_reverse(!get_reverse());
		display_list();
	}
	else if (komp == &filter) {
		filter_state ^= 1;
		filter.pressed = filter_state;
		display_list();
	}
	return true;
}



/**
 * resize window in response to a resize event
 * @author Hj. Malthaner
 * @date   16-Oct-2003
 */
void labellist_frame_t::resize(const scr_coord delta)
{
	gui_frame_t::resize(delta);
	scr_size size = get_windowsize()-scr_size(0,D_TITLEBAR_HEIGHT+14+D_BUTTON_HEIGHT+2+1);
	scrolly.set_size(size);
}



/**
* This function refreshes the label list
* @author Markus Weber/Volker Meyer
*/
void labellist_frame_t::display_list()
{
	sortedby.set_text(sort_text[get_sortierung()]);
	sorteddir.set_text(get_reverse() ? "hl_btn_sort_desc" : "hl_btn_sort_asc");
	stats.get_unique_labels(sortby, sortreverse, filter_state);
	stats.recalc_size();
}
