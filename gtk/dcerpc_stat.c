/* dcerpc_stat.c
 * dcerpc_stat   2002 Ronnie Sahlberg
 *
 * $Id: dcerpc_stat.c,v 1.20 2003/09/15 22:16:08 guy Exp $
 *
 * Ethereal - Network traffic analyzer
 * By Gerald Combs <gerald@ethereal.com>
 * Copyright 1998 Gerald Combs
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/* This module provides rpc call/reply SRT statistics to ethereal,
 * and displays them graphically.
 * It is only used by ethereal and not tethereal
 *
 * It serves as an example on how to use the tap api.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>
#include <gtk/gtk.h>
#include "menu.h"
#include "epan/packet_info.h"
#include "simple_dialog.h"
#include "dlg_utils.h"
#include "ui_util.h"
#include "tap.h"
#include "../register.h"
#include "packet-dcerpc.h"
#include "../globals.h"
#include "compat_macros.h"
#include "service_response_time_table.h"


extern GtkWidget   *main_display_filter_widget;

/* used to keep track of the statistics for an entire program interface */
typedef struct _rpcstat_t {
	GtkWidget *win;
	srt_stat_table srt_table;
	char *prog;
	e_uuid_t uuid;
	guint16 ver;
	int num_procedures;
} rpcstat_t;


static int
uuid_equal(e_uuid_t *uuid1, e_uuid_t *uuid2)
{
	if( (uuid1->Data1!=uuid2->Data1)
	  ||(uuid1->Data2!=uuid2->Data2)
	  ||(uuid1->Data3!=uuid2->Data3)
	  ||(uuid1->Data4[0]!=uuid2->Data4[0])
	  ||(uuid1->Data4[1]!=uuid2->Data4[1])
	  ||(uuid1->Data4[2]!=uuid2->Data4[2])
	  ||(uuid1->Data4[3]!=uuid2->Data4[3])
	  ||(uuid1->Data4[4]!=uuid2->Data4[4])
	  ||(uuid1->Data4[5]!=uuid2->Data4[5])
	  ||(uuid1->Data4[6]!=uuid2->Data4[6])
	  ||(uuid1->Data4[7]!=uuid2->Data4[7]) ){
		return 0;
	}
	return 1;
}
	

static void
dcerpcstat_reset(rpcstat_t *rs)
{
	char title_string[256];
	reset_srt_table_data(&rs->srt_table);

	snprintf(title_string, 255, "DCE-RPC Service Response Time statistics for %s version %d.%d : %s", rs->prog, rs->ver&0xff, rs->ver>>8, cf_get_display_name(&cfile));
	gtk_window_set_title(GTK_WINDOW(rs->win), title_string);
}


static int
dcerpcstat_packet(rpcstat_t *rs, packet_info *pinfo, epan_dissect_t *edt _U_, dcerpc_info *ri)
{
	if(!ri->call_data){
		return 0;
	}
	if(!ri->call_data->req_frame){
		/* we have not seen the request so we dont know the delta*/
		return 0;
	}
	if(ri->call_data->opnum>=rs->num_procedures){
		/* dont handle this since its outside of known table */
		return 0;
	}

	/* we are only interested in reply packets */
	if(ri->request){
		return 0;
	}

	/* we are only interested in certain program/versions */
	if( (!uuid_equal( (&ri->call_data->uuid), (&rs->uuid)))
	  ||(ri->call_data->ver!=rs->ver)){
		return 0;
	}


	add_srt_table_data(&rs->srt_table, ri->call_data->opnum, &ri->call_data->req_time, pinfo);


	return 1;
}

static void
dcerpcstat_draw(rpcstat_t *rs)
{
	draw_srt_table_data(&rs->srt_table);
}


/* since the gtk2 implementation of tap is multithreaded we must protect
 * remove_tap_listener() from modifying the list while draw_tap_listener()
 * is running.  the other protected block is in main.c
 *
 * there should not be any other critical regions in gtk2
 */
void protect_thread_critical_region(void);
void unprotect_thread_critical_region(void);
static void
win_destroy_cb(GtkWindow *win _U_, gpointer data)
{
	rpcstat_t *rs=(rpcstat_t *)data;

	protect_thread_critical_region();
	remove_tap_listener(rs);
	unprotect_thread_critical_region();

	free_srt_table_data(&rs->srt_table);
	g_free(rs);
}



/* When called, this function will create a new instance of gtk-dcerpcstat.
 */
static void
gtk_dcerpcstat_init(char *optarg)
{
	rpcstat_t *rs;
	guint32 i, max_procs;
	char title_string[256];
	char filter_string[256];
	GtkWidget *vbox;
	GtkWidget *stat_label;
	GtkWidget *filter_label;
	dcerpc_sub_dissector *procs;
	e_uuid_t uuid;
	int d1,d2,d3,d40,d41,d42,d43,d44,d45,d46,d47;
	int major, minor;
	int pos=0;
        char *filter=NULL;
        GString *error_string;

	if(sscanf(optarg,"dcerpc,srt,%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x,%d.%d,%n", &d1,&d2,&d3,&d40,&d41,&d42,&d43,&d44,&d45,&d46,&d47,&major,&minor,&pos)==13){
		uuid.Data1=d1;
		uuid.Data2=d2;
		uuid.Data3=d3;
		uuid.Data4[0]=d40;
		uuid.Data4[1]=d41;
		uuid.Data4[2]=d42;
		uuid.Data4[3]=d43;
		uuid.Data4[4]=d44;
		uuid.Data4[5]=d45;
		uuid.Data4[6]=d46;
		uuid.Data4[7]=d47;
		if(pos){
			filter=optarg+pos;
		} else {
			filter=NULL;
		}
	} else {
		fprintf(stderr, "ethereal: invalid \"-z dcerpc,srt,<uuid>,<major version>.<minor version>[,<filter>]\" argument\n");
		exit(1);
	}


	rs=g_malloc(sizeof(rpcstat_t));
	rs->prog=dcerpc_get_proto_name(&uuid, (minor<<8)|(major&0xff) );
	if(!rs->prog){
		g_free(rs);
		fprintf(stderr,"ethereal: dcerpcstat_init() Protocol with uuid:%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x v%d.%d not supported\n",uuid.Data1,uuid.Data2,uuid.Data3,uuid.Data4[0],uuid.Data4[1],uuid.Data4[2],uuid.Data4[3],uuid.Data4[4],uuid.Data4[5],uuid.Data4[6],uuid.Data4[7],major,minor);
		exit(1);
	}
	procs=dcerpc_get_proto_sub_dissector(&uuid, (minor<<8)|(major&0xff) );
	rs->uuid=uuid;
	rs->ver=(minor<<8)|(major&0xff);

	rs->win=gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_default_size(GTK_WINDOW(rs->win), 550, 400);
	snprintf(title_string, 255, "DCE-RPC Service Response Time statistics for %s version %d.%d : %s", rs->prog, rs->ver&0xff, rs->ver>>8, cf_get_display_name(&cfile));
	gtk_window_set_title(GTK_WINDOW(rs->win), title_string);
	SIGNAL_CONNECT(rs->win, "destroy", win_destroy_cb, rs);

	vbox=gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(rs->win), vbox);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 10);
	gtk_widget_show(vbox);

	stat_label=gtk_label_new(title_string);
	gtk_box_pack_start(GTK_BOX(vbox), stat_label, FALSE, FALSE, 0);
	gtk_widget_show(stat_label);

	snprintf(filter_string,255,"Filter:%s",filter?filter:"");
	filter_label=gtk_label_new(filter_string);
	gtk_box_pack_start(GTK_BOX(vbox), filter_label, FALSE, FALSE, 0);
	gtk_widget_show(filter_label);

	for(i=0,max_procs=0;procs[i].name;i++){
		if(procs[i].num>max_procs){
			max_procs=procs[i].num;
		}
	}
	rs->num_procedures=max_procs+1;

	/* We must display TOP LEVEL Widget before calling init_srt_table() */
	gtk_widget_show(rs->win);

	init_srt_table(&rs->srt_table, max_procs+1, vbox, NULL);

       	for(i=0;i<(max_procs+1);i++){
		int j;
		char *proc_name;

		proc_name="unknown";
		for(j=0;procs[j].name;j++){
			if(procs[j].num==i){
				proc_name=procs[j].name;
			}
		}

		init_srt_table_row(&rs->srt_table, i, proc_name);
	}


	error_string=register_tap_listener("dcerpc", rs, filter, (void*)dcerpcstat_reset, (void*)dcerpcstat_packet, (void*)dcerpcstat_draw);
	if(error_string){
		/* error, we failed to attach to the tap. clean up */
		simple_dialog(ESD_TYPE_WARN, NULL, error_string->str);
		g_string_free(error_string, TRUE);
		free_srt_table_data(&rs->srt_table);
		g_free(rs);
		return;
	}


	gtk_widget_show_all(rs->win);
	redissect_packets(&cfile);
}



static e_uuid_t *dcerpc_uuid_program=NULL;
static guint16 dcerpc_version;
static GtkWidget *dlg=NULL, *dlg_box;
static GtkWidget *prog_box;
static GtkWidget *prog_label, *prog_opt, *prog_menu;
static GtkWidget *vers_label, *vers_opt, *vers_menu;
static GtkWidget *filter_box;
static GtkWidget *filter_label, *filter_entry;
static GtkWidget *start_button;
static dcerpc_uuid_key *current_uuid_key=NULL;
static dcerpc_uuid_value *current_uuid_value=NULL;
static dcerpc_uuid_key *new_uuid_key=NULL;
static dcerpc_uuid_value *new_uuid_value=NULL;


static void
dcerpcstat_start_button_clicked(GtkWidget *item _U_, gpointer data _U_)
{
	char *filter;
	char str[256];

	filter=(char *)gtk_entry_get_text(GTK_ENTRY(filter_entry));
	if(filter[0]==0){
		sprintf(str, "dcerpc,srt,%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x,%d.%d",dcerpc_uuid_program->Data1,dcerpc_uuid_program->Data2,dcerpc_uuid_program->Data3,dcerpc_uuid_program->Data4[0],dcerpc_uuid_program->Data4[1],dcerpc_uuid_program->Data4[2],dcerpc_uuid_program->Data4[3],dcerpc_uuid_program->Data4[4],dcerpc_uuid_program->Data4[5],dcerpc_uuid_program->Data4[6],dcerpc_uuid_program->Data4[7],dcerpc_version&0xff,dcerpc_version>>8);

	} else {
		sprintf(str, "dcerpc,srt,%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x,%d.%d,%s",dcerpc_uuid_program->Data1,dcerpc_uuid_program->Data2,dcerpc_uuid_program->Data3,dcerpc_uuid_program->Data4[0],dcerpc_uuid_program->Data4[1],dcerpc_uuid_program->Data4[2],dcerpc_uuid_program->Data4[3],dcerpc_uuid_program->Data4[4],dcerpc_uuid_program->Data4[5],dcerpc_uuid_program->Data4[6],dcerpc_uuid_program->Data4[7],dcerpc_version&0xff,dcerpc_version>>8, filter);
	}

	gtk_dcerpcstat_init(str);
}


static void
dcerpcstat_version_select(GtkWidget *item _U_, gpointer key)
{
	int vers=(int)key;

	dcerpc_version=vers;
}




static void *
dcerpcstat_find_vers(gpointer *key, gpointer *value _U_, gpointer *user_data _U_)
{
	dcerpc_uuid_key *k=(dcerpc_uuid_key *)key;
	GtkWidget *menu_item;
	char vs[5];

	if(!uuid_equal((&k->uuid), dcerpc_uuid_program)){
		return NULL;
	}

	sprintf(vs,"%d.%d",k->ver&0xff,k->ver>>8);
	menu_item=gtk_menu_item_new_with_label(vs);
	SIGNAL_CONNECT(menu_item, "activate", dcerpcstat_version_select,
                       ((int)k->ver));
	gtk_widget_show(menu_item);
	gtk_menu_append(GTK_MENU(vers_menu), menu_item);

	if(dcerpc_version==0xffff){
		dcerpc_version=k->ver;
	}

	return NULL;
}


static void
dcerpcstat_program_select(GtkWidget *item _U_, gpointer key)
{
	dcerpc_uuid_key *k=(dcerpc_uuid_key *)key;

	dcerpc_uuid_program=&k->uuid;

	/* change version menu */
	dcerpc_version=0xffff;
	gtk_object_destroy(GTK_OBJECT(vers_menu));
	vers_menu=gtk_menu_new();
	g_hash_table_foreach(dcerpc_uuids, (GHFunc)dcerpcstat_find_vers, NULL);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(vers_opt), vers_menu);
}


static void 
dcerpcstat_add_program_to_menu(dcerpc_uuid_key *k, dcerpc_uuid_value *v)
{
	GtkWidget *program_menu_item;

	program_menu_item=gtk_menu_item_new_with_label(v->name);
	SIGNAL_CONNECT(program_menu_item, "activate", dcerpcstat_program_select, k);

	gtk_widget_show(program_menu_item);
	gtk_menu_append(GTK_MENU(prog_menu), program_menu_item);

	if(!dcerpc_uuid_program){
		dcerpc_uuid_program=&k->uuid;
	}

	return;
}

static void *
dcerpcstat_find_next_program(gpointer *key, gpointer *value, gpointer *user_data _U_)
{
	dcerpc_uuid_key *k=(dcerpc_uuid_key *)key;
	dcerpc_uuid_value *v=(dcerpc_uuid_value *)value;

	/* first time called, just set new_uuid to this one */
	if((current_uuid_key==NULL)&&(new_uuid_key==NULL)){
		new_uuid_key=k;
		new_uuid_value=v;
		return NULL;
	}

	/* if we havent got a current one yet, just check the new
	   and scan for the first one alphabetically  */
	if(current_uuid_key==NULL){
		if(strcmp(new_uuid_value->name, v->name)>0){
			new_uuid_key=k;
			new_uuid_value=v;
			return NULL;
		}
		return NULL;
	}

	/* searching for the next one we are only interested in those
	   that sorts alphabetically after the current one */
	if(strcmp(current_uuid_value->name, v->name)>=0){
		/* this one doesnt so just skip it */
		return NULL;
	}

	/* is it the first potential new entry? */
	if(new_uuid_key==NULL){
		new_uuid_key=k;
		new_uuid_value=v;
		return NULL;
	}

	/* does it sort before the current new one? */
	if(strcmp(new_uuid_value->name, v->name)>0){
		new_uuid_key=k;
		new_uuid_value=v;
		return NULL;
	}

	return NULL;
}


static void
dlg_destroy_cb(void)
{
	dlg=NULL;
}


static void
dlg_cancel_cb(GtkWidget *cancel_bt _U_, gpointer parent_w)
{
	gtk_widget_destroy(GTK_WIDGET(parent_w));
}


static void
gtk_dcerpcstat_cb(GtkWidget *w _U_, gpointer d _U_)
{
	GtkWidget *bbox, *cancel_button;
	char *filter;

	/* if the window is already open, bring it to front and
	   un-minimize it, as necessary */
	if(dlg){
		reactivate_window(dlg);
		return;
	}

	dlg=dlg_window_new("Ethereal: DCE-RPC SRT Statistics");
	SIGNAL_CONNECT(dlg, "destroy", dlg_destroy_cb, NULL);
	dlg_box=gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(dlg), dlg_box);
	gtk_widget_show(dlg_box);


	prog_box=gtk_hbox_new(FALSE, 10);
	/* Program label */
	gtk_container_set_border_width(GTK_CONTAINER(prog_box), 10);
	prog_label=gtk_label_new("Program:");
	gtk_box_pack_start(GTK_BOX(prog_box), prog_label, FALSE, FALSE, 0);
	gtk_widget_show(prog_label);

	/* Program menu */
	prog_opt=gtk_option_menu_new();
	prog_menu=gtk_menu_new();
	current_uuid_key=NULL;
	current_uuid_value=NULL;
	do {
		new_uuid_key=NULL;
		new_uuid_value=NULL;
		g_hash_table_foreach(dcerpc_uuids, (GHFunc)dcerpcstat_find_next_program, NULL);
		if(new_uuid_key){
			dcerpcstat_add_program_to_menu(new_uuid_key, new_uuid_value);
		}
		current_uuid_key=new_uuid_key;
		current_uuid_value=new_uuid_value;
	} while(new_uuid_key!=NULL);


	gtk_option_menu_set_menu(GTK_OPTION_MENU(prog_opt), prog_menu);
	gtk_box_pack_start(GTK_BOX(prog_box), prog_opt, TRUE, TRUE, 0);
	gtk_widget_show(prog_opt);

	/* Version label */
	gtk_container_set_border_width(GTK_CONTAINER(prog_box), 10);
	vers_label=gtk_label_new("Version:");
	gtk_box_pack_start(GTK_BOX(prog_box), vers_label, FALSE, FALSE, 0);
	gtk_widget_show(vers_label);

	/* Version menu */
	vers_opt=gtk_option_menu_new();
	vers_menu=gtk_menu_new();
	dcerpc_version=0xffff;
	g_hash_table_foreach(dcerpc_uuids, (GHFunc)dcerpcstat_find_vers, NULL);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(vers_opt), vers_menu);
	gtk_box_pack_start(GTK_BOX(prog_box), vers_opt, TRUE, TRUE, 0);
	gtk_widget_show(vers_opt);

	gtk_box_pack_start(GTK_BOX(dlg_box), prog_box, TRUE, TRUE, 0);
	gtk_widget_show(prog_box);


	/* filter box */
	filter_box=gtk_hbox_new(FALSE, 10);
	/* Filter label */
	gtk_container_set_border_width(GTK_CONTAINER(filter_box), 10);
	filter_label=gtk_label_new("Filter:");
	gtk_box_pack_start(GTK_BOX(filter_box), filter_label, FALSE, FALSE, 0);
	gtk_widget_show(filter_label);

	filter_entry=gtk_entry_new_with_max_length(250);
	gtk_box_pack_start(GTK_BOX(filter_box), filter_entry, FALSE, FALSE, 0);
	filter=gtk_entry_get_text(GTK_ENTRY(main_display_filter_widget));
	if(filter){
		gtk_entry_set_text(GTK_ENTRY(filter_entry), filter);
	}
	gtk_widget_show(filter_entry);
	
	gtk_box_pack_start(GTK_BOX(dlg_box), filter_box, TRUE, TRUE, 0);
	gtk_widget_show(filter_box);


	bbox=gtk_hbutton_box_new();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_END);
	gtk_button_box_set_spacing(GTK_BUTTON_BOX(bbox), 5);
	gtk_container_add(GTK_CONTAINER(dlg_box), bbox);
	gtk_widget_show(bbox);

	/* the start button */
	start_button=gtk_button_new_with_label("Create Stat");
	SIGNAL_CONNECT_OBJECT(start_button, "clicked", 
                              dcerpcstat_start_button_clicked, NULL);
	GTK_WIDGET_SET_FLAGS(start_button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), start_button, TRUE, TRUE, 0);
	gtk_widget_grab_default(start_button);
	gtk_widget_show(start_button);

#if GTK_MAJOR_VERSION < 2
	cancel_button=gtk_button_new_with_label("Cancel");
#else
	cancel_button=gtk_button_new_from_stock(GTK_STOCK_CANCEL);
#endif
	SIGNAL_CONNECT(cancel_button, "clicked", dlg_cancel_cb, dlg);
	GTK_WIDGET_SET_FLAGS(cancel_button, GTK_CAN_DEFAULT);
	gtk_box_pack_start(GTK_BOX(bbox), cancel_button, TRUE, TRUE, 0);
	gtk_widget_show(cancel_button);

	/* Catch the "activate" signal on the filter text entry, so that
	   if the user types Return there, we act as if the "Create Stat"
	   button had been selected, as happens if Return is typed if some
	   widget that *doesn't* handle the Return key has the input
	   focus. */
	dlg_set_activate(filter_entry, start_button);

	/* Catch the "key_press_event" signal in the window, so that we can
	   catch the ESC key being pressed and act as if the "Cancel" button
	   had been selected. */
	dlg_set_cancel(dlg, cancel_button);

	/* Give the initial focus to the "Filter" entry box. */
	gtk_widget_grab_focus(filter_entry);

	gtk_widget_show_all(dlg);
}

void
register_tap_listener_gtkdcerpcstat(void)
{
	register_ethereal_tap("dcerpc,srt,", gtk_dcerpcstat_init);
}

void
register_tap_menu_gtkdcerpcstat(void)
{
	register_tap_menu_item("Service Response Time/DCE-RPC", gtk_dcerpcstat_cb);
}
