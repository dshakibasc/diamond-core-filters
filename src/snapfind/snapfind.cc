#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <math.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libgen.h>		/* dirname */
#include <assert.h>
#include <stdint.h>
#include <signal.h>

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#ifdef linux
#include <getopt.h>
#else
#ifndef HAVE_DECL_GETOPT
#define HAVE_DECL_GETOPT 1
#endif
#include <gnugetopt/getopt.h>
#endif

#include "filter_api.h"
#include "searchlet_api.h"
#include "gui_thread.h"

#include "queue.h"
#include "ring.h"
#include "rtimer.h"
#include "sf_consts.h"

#include "face_search.h"
#include "face_tools.h"
#include "face_image.h"
#include "rgb.h"
#include "face.h"
#include "fil_tools.h"
#include "image_tools.h"
#include "face_widgets.h"
#include "texture_tools.h"
#include "snap_search.h"
#include "sfind_search.h"
#include "snap_popup.h"
#include "snapfind.h"

/* number of thumbnails to show */
static const int TABLE_COLS = 3;
static const int TABLE_ROWS = 2;

static int default_min_faces = 0;
static int default_face_levels = 37;


thumblist_t thumbnails = TAILQ_HEAD_INITIALIZER(thumbnails);
thumbnail_t *cur_thumbnail = NULL;

/* XXX move these later to common header */
#define	TO_SEARCH_RING_SIZE		512
#define	FROM_SEARCH_RING_SIZE		512


int expert_mode = 0;		/* global (also used in face_widgets.c) */
int dump_attributes = 0;
char *dump_spec_file = NULL;		/* dump spec file and exit */
int dump_objects = 0;		/* just dump all the objects and exit (no gui) */
GtkTooltips *tooltips = NULL;
char *read_spec_filename = NULL;



/* XXXX fix this */ 
GtkWidget *config_table;

typedef struct export_threshold_t {
  char *name;
  double distance;
  int index;			/* index into scapes[] */
  TAILQ_ENTRY(export_threshold_t) link;
} export_threshold_t;


TAILQ_HEAD(export_list_t, export_threshold_t) export_list = TAILQ_HEAD_INITIALIZER(export_list);

static struct {
  GtkWidget *main_window;
  
  GtkWidget *min_faces;
  GtkWidget *face_levels;

  GtkWidget *start_button;
  GtkWidget *stop_button;
  GtkWidget *search_box;
  GtkWidget *search_widget;
  GtkWidget *attribute_cb, *attribute_entry;
/* GtkWidget *andorbuttons[2]; */
  
  GtkWidget *scapes_tables[2];

} gui;



/* 
 * scapes entries. sorta contant
 */
/* XXXX fix this */
#define	MAX_SEARCHES	64
snap_search * snap_searches[MAX_SEARCHES];
int num_searches = 0;


static lf_fhandle_t fhandle = 0;	/* XXX */


/**********************************************************************/

/*
 * state required to support popup window to show fullsize img
 */

pop_win_t	 popup_window = {NULL, NULL, NULL};



/* ********************************************************************** */

/* some stats for user study */

struct {
  int total_seen, total_marked;
} user_measurement = { 0, 0 };


typedef enum {
	CNTRL_ELEV,
	//CNTRL_SLOPE,
	CNTRL_WAIT,
	CNTRL_NEXT,
} control_ops_t;

typedef	 struct {
	GtkWidget *	parent_box;
	GtkWidget *	control_box;
	GtkWidget *	next_button;
	GtkWidget *	zbutton;
	control_ops_t 	cur_op;
	int	 	zlevel;
} image_control_t;

typedef struct {
	GtkWidget *	parent_box;
	GtkWidget *	info_box1;
	GtkWidget *	info_box2;
	GtkWidget *	name_tag;
	GtkWidget *	name_label;
	GtkWidget *	count_tag;
	GtkWidget *	count_label;

	GtkWidget *     qsize_label; /* no real need to save this */
	GtkWidget *     tobjs_label; /* Total objs being searche */ 
	GtkWidget *     sobjs_label; /* Total objects searched */
	GtkWidget *     dobjs_label; /* Total objects dropped */
} image_info_t;


/* XXX */
static image_control_t		image_controls;
static image_info_t		image_information;


/*
 * some globals that we need to find a place for
 */
ring_data_t *	to_search_thread;
ring_data_t *	from_search_thread;
int		pend_obj_cnt = 0;
int		tobj_cnt = 0;
int		sobj_cnt = 0;
int		dobj_cnt = 0;

static pthread_t	display_thread_info;
static int		display_thread_running = 0;

/*
 * Display the cond variables.
 */
static pthread_cond_t	display_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t	display_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t	ring_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t	thumb_mutex = PTHREAD_MUTEX_INITIALIZER;



/* 
 * some prototypes 
 */

extern region_t draw_bounding_box(RGBImage *img, int scale, 
				  lf_fhandle_t fhandle, ls_obj_handle_t ohandle,
				  RGBPixel color, RGBPixel mask, char *fmt, int i);
static GtkWidget *make_gimage(RGBImage *img, int w, int h);

/* from read_config.l */
extern int read_search_config(char *fname, snap_search **list, int *num);

/* from face_search.c */
extern void drain_ring(ring_data_t *ring);

static void highlight_progress_f(void *widget, int val, int total);


/* ********************************************************************** */

struct collection_t {
  char *name;
  //int id;
  int active;
};

struct collection_t collections[MAX_ALBUMS+1] = {
/*   {"Local",   1, 0}, */
/*   {"All remote",   2, 1}, */
/*   {"diamond01 only", 3, 0}, */
/*   {"diamond02 only",  4, 0}, */
/*   {"diamond03 only",  5, 0}, */
/*   {"diamond04 only",  6, 0}, */
  {NULL}
};




/* ********************************************************************** */
/* utility functions */
/* ********************************************************************** */


/*
 * make pixbuf from img
 */
static GdkPixbuf*
pb_from_img(RGBImage *img) {
	GdkPixbuf *pbuf;

	/* NB pixbuf refers to data */
	pbuf = gdk_pixbuf_new_from_data((const guchar *)&img->data[0], 
					GDK_COLORSPACE_RGB, 1, 8, 
					img->columns, img->rows, 
					(img->columns*sizeof(RGBPixel)),
					NULL,
					NULL);
	if (pbuf == NULL) {
		printf("failed to allocate pbuf\n");
		exit(1);
	}
	return pbuf;
}


/* 
 * make a gtk image from an img
 */
static GtkWidget *
make_gimage(RGBImage *img, int dest_width, int dest_height) {
  GdkPixbuf *pbuf; // *scaled_pbuf;

	//fprintf(stderr, "gimage called\n");

	GUI_THREAD_CHECK(); 

	pbuf = gdk_pixbuf_new_from_data((const guchar *)&img->data[0], 
					GDK_COLORSPACE_RGB, 1, 8, 
					img->columns, img->rows, 
					(img->columns*sizeof(RGBPixel)),
					NULL,
					NULL);
	if (pbuf == NULL) {
		printf("failed to allocate pbuf\n");
		exit(1);
	}

//	scaled_pbuf = gdk_pixbuf_scale_simple(pbuf, dest_width, dest_height, GDK_INTERP_BILINEAR);

	GtkWidget *image;
	//image = gtk_image_new_from_pixbuf(scaled_pbuf);
	image = gtk_image_new_from_pixbuf(pbuf);
	assert(image);

	return image;
}




static void
clear_image_info(image_info_t *img_info)
{
	char	data[BUFSIZ];

	GUI_THREAD_CHECK(); 

	sprintf(data, "%-60s", " ");
	gtk_label_set_text(GTK_LABEL(img_info->name_label), data);

	sprintf(data, "%-3s", " ");
	gtk_label_set_text(GTK_LABEL(img_info->count_label), data);

	//gtk_label_set_text(GTK_LABEL(img_info->qsize_label), data);

}


static void
write_image_info(image_info_t *img_info, char *name, int count)
{
	char	txt[BUFSIZ];

	GUI_THREAD_CHECK(); 

	sprintf(txt, "%-60s", name);
	gtk_label_set_text(GTK_LABEL(img_info->name_label), txt);

	sprintf(txt, "%-3d", count);
	gtk_label_set_text(GTK_LABEL(img_info->count_label), txt);
}


/*
 * This disables all the buttons in the image control section
 * of the display.  This will be called when there is no active image
 * to manipulate.
 */
void
disable_image_control(image_control_t *img_cntrl)
{

	GUI_THREAD_CHECK(); 
	gtk_widget_set_sensitive(img_cntrl->next_button, FALSE);
}

	
/*
 * This enables all the buttons in the image control section
 * of the display.  This will be called when there is a new image to 
 * manipulated.
 */
static void
enable_image_control(image_control_t *img_cntrl)
{

	GUI_THREAD_CHECK(); 
	gtk_widget_set_sensitive(img_cntrl->next_button, TRUE);
	gtk_widget_grab_default(img_cntrl->next_button);

}



static void
build_search_from_gui(topo_region_t *main_region) 
{

	int i;
	/* 
	 * figure out the args and build message
	 */
	for(i=0; i<MAX_ALBUMS && collections[i].name; i++) {
	  /* if collection active, figure out the gids and add to out list
	   * allows duplicates XXX */
	  if(collections[i].active) {
	    int err;
	    int num_gids = MAX_ALBUMS;
	    groupid_t gids[MAX_ALBUMS], *gptr;
	    err = nlkup_lookup_collection(collections[i].name, &num_gids, gids);
	    assert(!err);
	    gptr = gids;
	    while(num_gids) {
	      main_region->gids[main_region->ngids++] = *gptr++;
	      num_gids--;
	    }
	    //printf("gid %d active\n",  collections[i].id);
	  }
	}
}

/*
 * Build the filters specification into the temp file name
 * "tmp_file".  We walk through all the activated regions and
 * all the them to write out the file.
 */

char *
build_filter_spec(char *tmp_file, topo_region_t *main_region)
{ 
	char * 		tmp_storage = NULL;
	FILE *		fspec;	
	int		err;
	int             fd;
	snap_search *		snapobj;
	int					i;

	tmp_storage = (char *)malloc(L_tmpnam);	/* where is the free for this? XXX */
	if (tmp_storage == NULL) {
		printf("XXX failed to alloc memory !!! \n");
		return(NULL);
	}

	if(!tmp_file) {
		tmp_file = tmp_storage;
		sprintf(tmp_storage, "%sXXXXXX", "/tmp/filspec");
		fd = mkstemp(tmp_storage);
	} else {
		fd = open(tmp_file, O_RDWR|O_CREAT|O_TRUNC, 0666);
	}
		
	if(fd < 0) { 
		perror(tmp_file);
		free(tmp_storage);
		return NULL;
	}
	fspec = fdopen(fd, "w+");
	if (fspec == NULL) {
		perror(tmp_file);
		free(tmp_storage);
		return(NULL);
	}

	/* XXX empty dependency list */

	for (i = 0; i < num_searches ; i++) {
		snapobj = snap_searches[i];
		if (snapobj->is_selected()) {
			snapobj->save_edits();
			snapobj->write_fspec(fspec);
		}
	}

	/* XXX write the dependy list */

	err = fclose(fspec);	/* closes fd as well */
	if (err != 0) {
		printf("XXX failed to close file \n");
		free(tmp_storage);
		return(NULL);
	}
	return(tmp_file);
}

static void
do_img_mark(GtkWidget *widget) {
  thumbnail_t *thumb;

  thumb= (thumbnail_t *)gtk_object_get_user_data(GTK_OBJECT(widget));

  thumb->marked ^= 1;

  /* adjust count */
  user_measurement.total_marked += (thumb->marked ? 1 : -1);

  printf("marked count = %d/%d\n", user_measurement.total_marked, 
	 user_measurement.total_seen);
  
  gtk_frame_set_label(GTK_FRAME(thumb->frame),
		      (thumb->marked) ? "marked" : "");
}

static void
cb_img_popup(GtkWidget *widget, GdkEventButton *event, gpointer data) {

  GUI_CALLBACK_ENTER();

  /* dispatch based on the button pressed */
  switch(event->button) {
  case 1:
    do_img_popup(widget);
    break;
  case 3:
    do_img_mark(widget);
  default:
    break;
  }
  
  GUI_CALLBACK_LEAVE();
}



static int
timeout_write_qsize(gpointer label)
{
	char	txt[BUFSIZ];
	int     qsize;

	qsize = pend_obj_cnt;
	sprintf(txt, "Pending images = %-6d", qsize);

	GUI_THREAD_ENTER(); 
	gtk_label_set_text(GTK_LABEL(label), txt);
	GUI_THREAD_LEAVE();

	return TRUE;
}

static int
timeout_write_tobjs(gpointer label)
{
	char	txt[BUFSIZ];
	int     tobjs;

	tobjs = tobj_cnt;
	sprintf(txt, "Total Objs = %-6d ", tobjs);

	GUI_THREAD_ENTER(); 
	gtk_label_set_text(GTK_LABEL(label), txt);
	GUI_THREAD_LEAVE();
	return TRUE;
}

static int
timeout_write_sobjs(gpointer label)
{
	char	txt[BUFSIZ];
	int     sobjs;

	sobjs = sobj_cnt;
	sprintf(txt, "Searched Objs = %-6d ", sobjs);

	GUI_THREAD_ENTER(); 
	gtk_label_set_text(GTK_LABEL(label), txt);
	GUI_THREAD_LEAVE();
	return TRUE;
}

static int
timeout_write_dobjs(gpointer label)
{
	char	txt[BUFSIZ];
	int     dobjs;

	dobjs = dobj_cnt;
	sprintf(txt, "Dropped Objs = %-6d ", dobjs);

	GUI_THREAD_ENTER(); 
	gtk_label_set_text(GTK_LABEL(label), txt);
	GUI_THREAD_LEAVE();
	return TRUE;
}




/* ********************************************************************** */
/* ********************************************************************** */

static void
highlight_box_f(void *cont, search_param_t *param) {
	RGBImage *img = (RGBImage *)cont;
	bbox_t bbox;

	//fprintf(stderr, "found bbox\n");

	
	bbox.min_x = param->bbox.xmin;
	bbox.min_y = param->bbox.ymin;
	bbox.max_x = param->bbox.xmin + param->bbox.xsiz - 1;
	bbox.max_y = param->bbox.ymin + param->bbox.ysiz - 1;

	image_fill_bbox_scale(img, &bbox, 1, hilitMask, hilit);

	GUI_THREAD_ENTER();
	gtk_widget_queue_draw_area(popup_window.drawing_area,
				   param->bbox.xmin, param->bbox.ymin,
				   param->bbox.xsiz, param->bbox.ysiz);
	GUI_THREAD_LEAVE();
}

static void
highlight_box_f(RGBImage *img, bbox_t bbox) {
	image_fill_bbox_scale(img, &bbox, 1, hilitMask, hilit);

	GUI_THREAD_ENTER();
	gtk_widget_queue_draw_area(popup_window.drawing_area,
				   bbox.min_x, bbox.min_y,
				   bbox.max_x, bbox.max_y);
	GUI_THREAD_LEAVE();
}

/* horribly expensive, but... */
static void
highlight_progress_f(void *widget, int val, int total) 
{
	double fraction;

	fraction = (double)val / total;

	GUI_THREAD_ENTER(); 
	gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(widget), fraction);
	GUI_THREAD_LEAVE();
}



static void
display_thumbnail(ls_obj_handle_t ohandle)
{
	RGBImage        *rgbimg, *scaledimg;
	char            name[MAX_NAME];
	off_t		bsize;
	lf_fhandle_t	fhandle = 0; /* XXX */
	int		err;
	int		num_face, num_histo;

	while(image_controls.cur_op == CNTRL_WAIT) {
		fprintf(stderr, "GOT WAIT. waiting...\n");
		pthread_mutex_lock(&display_mutex);
		if(image_controls.cur_op == CNTRL_WAIT) {
			pthread_cond_wait(&display_cond, &display_mutex);
		}
		pthread_mutex_unlock(&display_mutex);
	}

	assert(image_controls.cur_op == CNTRL_NEXT ||
	       image_controls.cur_op == CNTRL_ELEV);

	/* get path XXX */
	bsize = MAX_NAME;
	err = lf_read_attr(fhandle, ohandle, DISPLAY_NAME, &bsize, name);
	if(err) {
	  err = lf_read_attr(fhandle, ohandle, OBJ_PATH, &bsize, name);
	}
	if (err) {
		sprintf(name, "%s", "uknown");
		bsize = strlen(name);
	}
	name[bsize] = '\0';	/* terminate string */


	/* get the img */
	rgbimg = (RGBImage*)ft_read_alloc_attr(fhandle, ohandle, RGB_IMAGE);
	assert(rgbimg);
	assert(rgbimg->width);

	/* figure out bboxes to highlight */
	bsize = sizeof(num_histo);
	err = lf_read_attr(fhandle, ohandle, NUM_HISTO, &bsize, (char *)&num_histo);
	if (err) { num_histo = 0; }
	bsize = sizeof(num_face);
	err = lf_read_attr(fhandle, ohandle, NUM_FACE, &bsize, (char *)&num_face);
	if (err) { num_face = 0; }


	int scale = (int)ceil(compute_scale(rgbimg, THUMBSIZE_X, THUMBSIZE_Y));
	scaledimg = image_gen_image_scale(rgbimg, scale);
	assert(scaledimg);


	for(int i=0; i<num_histo; i++) {
		draw_bounding_box(scaledimg, scale, fhandle, ohandle, 
				  green, colorMask, HISTO_BBOX_FMT, i);
	}
	for(int i=0; i<num_face; i++) {
		draw_bounding_box(scaledimg, scale, fhandle, ohandle, 
				  red, colorMask, FACE_BBOX_FMT, i);
	}

	user_measurement.total_seen++;

	GUI_THREAD_ENTER();

	/*
	 * Build image the new data
	 */
	GtkWidget *image = make_gimage(scaledimg, THUMBSIZE_X, THUMBSIZE_Y);
	assert(image);

	/* 
	 * update the display
	 */

	pthread_mutex_lock(&thumb_mutex);

	if(!cur_thumbnail) {
		cur_thumbnail = TAILQ_FIRST(&thumbnails);
	}
	if(cur_thumbnail->img) { /* cleanup */
		gtk_container_remove(GTK_CONTAINER(cur_thumbnail->viewport), 
				     cur_thumbnail->gimage);
		lf_free_buffer(fhandle, (char *)cur_thumbnail->img); /* XXX */
		//lf_free_buffer(fhandle, (char *)cur_thumbnail->fullimage); /* XXX */
		ih_drop_ref(cur_thumbnail->hooks, fhandle);
		//ls_release_object(fhandle, cur_thumbnail->ohandle);		
	}
	gtk_frame_set_label(GTK_FRAME(cur_thumbnail->frame), "");
	cur_thumbnail->marked = 0;
	cur_thumbnail->img = scaledimg;
	cur_thumbnail->gimage = image;
	strcpy(cur_thumbnail->name, name);
	cur_thumbnail->nboxes = num_histo;
	cur_thumbnail->nfaces = num_face;
	cur_thumbnail->hooks = ih_new_ref(rgbimg, (HistoII*)NULL, ohandle);

	gtk_container_add(GTK_CONTAINER(cur_thumbnail->viewport), image);
	gtk_widget_show_now(image);


	cur_thumbnail = TAILQ_NEXT(cur_thumbnail, link);

	/* check if panel is full */
	if(cur_thumbnail == NULL) {
		pthread_mutex_unlock(&thumb_mutex);

		enable_image_control(&image_controls);
		//fprintf(stderr, "WINDOW FULL. waiting...\n");
		GUI_THREAD_LEAVE();

		/* block until user hits next */
		pthread_mutex_lock(&display_mutex);
		image_controls.cur_op = CNTRL_WAIT;
		pthread_cond_wait(&display_cond, &display_mutex);
		pthread_mutex_unlock(&display_mutex);

		GUI_THREAD_ENTER();
		disable_image_control(&image_controls);
		//fprintf(stderr, "WAIT COMPLETE...\n");
		GUI_THREAD_LEAVE();
	} else {
		pthread_mutex_unlock(&thumb_mutex);
		GUI_THREAD_LEAVE();
	}


}	


static void
clear_thumbnails() 
{
	//thumbnail_t *cur_thumbnail;

	clear_image_info(&image_information);

	pthread_mutex_lock(&thumb_mutex);
	TAILQ_FOREACH(cur_thumbnail, &thumbnails, link) {
		if(cur_thumbnail->img) { /* cleanup */
			gtk_container_remove(GTK_CONTAINER(cur_thumbnail->viewport), 
					     cur_thumbnail->gimage);
			free(cur_thumbnail->img); /* XXX */
			ih_drop_ref(cur_thumbnail->hooks, fhandle);
			cur_thumbnail->img = NULL;
	}
		
	}
	cur_thumbnail = NULL;
	pthread_mutex_unlock(&thumb_mutex);
}


static void *
display_thread(void *data)
{
	message_t *		message;
	struct timespec timeout;


	//fprintf(stderr, "DISPLAY THREAD STARTING\n");

	while (1) {

	        pthread_mutex_lock(&ring_mutex);
		message = (message_t *)ring_deq(from_search_thread);
		pthread_mutex_unlock(&ring_mutex);

		if (message != NULL) {
			switch (message->type) {
			case NEXT_OBJECT:

				//fprintf(stderr, "display obj...\n"); /* XXX */
				display_thumbnail((ls_obj_handle_t)message->data);
				//fprintf(stderr, "display obj done.\n");   /* XXX */
				break;

			case DONE_OBJECTS:
				/*
				 * We are done recieving objects.
				 * We need to disable the thread
				 * image controls and enable start
				 * search button.
				 */

				free(message);
				gtk_widget_set_sensitive(gui.start_button, TRUE);
				gtk_widget_set_sensitive(gui.stop_button, FALSE);
				display_thread_running = 0;
				pthread_exit(0);
				break;
				
			default:
				break;

			}
			free(message);
		}

		timeout.tv_sec = 0;
		timeout.tv_nsec = 10000000; /* XXX 10 ms?? */
		nanosleep(&timeout, NULL);

	}
	return 0;
}




static void 
cb_stop_search(GtkButton* item, gpointer data)
{
	message_t *		message;
	int			err;

        GUI_CALLBACK_ENTER();
	//printf("stop search !! \n");

	/*
	 * Toggle the start and stop buttons.
	 */
    	gtk_widget_set_sensitive(gui.start_button, TRUE);
	/* we should not be messing with the default. this is here so
	 * that we can trigger a search from the text entry without a
	 * return-pressed handler.  XXX */
	gtk_widget_grab_default (gui.start_button);
    	gtk_widget_set_sensitive(gui.stop_button, FALSE);

	message = (message_t *)malloc(sizeof(*message));
	if (message == NULL) {
		printf("failed to allocate message \n");
		exit(1);
	}

	message->type = TERM_SEARCH;
	message->data = NULL;

	err = ring_enq(to_search_thread, message);
	if (err) {
		printf("XXX failed to enq message \n");
		exit(1);
	}
	//fprintf(stderr, "facemain: enq TERM_SEARCH\n");

        GUI_CALLBACK_LEAVE();
}
static void 
cb_start_search(GtkButton* item, gpointer data)
{
	message_t *		message;
	int			err;

        GUI_CALLBACK_ENTER();
	printf("starting search !! \n");

	/*
	 * Disable the start search button and enable the stop search
	 * button.
	 */
    	gtk_widget_set_sensitive(gui.start_button, FALSE);
    	gtk_widget_set_sensitive(gui.stop_button, TRUE);
	clear_thumbnails();

        /* another global, ack!! this should be on the heap XXX */
        static topo_region_t main_region;
        build_search_from_gui(&main_region);

	pthread_mutex_lock(&display_mutex);
	image_controls.cur_op = CNTRL_NEXT;
	pthread_mutex_unlock(&display_mutex);


	/* problem: the signal (below) gets handled before the search
	   thread has a chance to drain the ring. so do it here. */
	pthread_mutex_lock(&ring_mutex);
	drain_ring(from_search_thread);
	pthread_mutex_unlock(&ring_mutex);


	/* 
	 * send the message
	 */
	message = (message_t *)malloc(sizeof(*message));
	if (message == NULL) {
		printf("failed to allocate message \n");
		exit(1);
	}
	message->type = START_SEARCH;
	message->data = (void *)&main_region;
	err = ring_enq(to_search_thread, message);
	if (err) {
		printf("XXX failed to enq message \n");
		exit(1);
	}

	//fprintf(stderr, "facemain: enq START_SEARCH\n");

        GUI_CALLBACK_LEAVE();	/* need to do this before signal (below) */

	if(!display_thread_running) {
		display_thread_running = 1;
		err = pthread_create(&display_thread_info, PATTR_DEFAULT, display_thread, NULL);
		if (err) {
			printf("failed to create  display thread \n");
			exit(1);
		}
	} else {
		pthread_mutex_lock(&display_mutex);
		pthread_cond_signal(&display_cond);
		pthread_mutex_unlock(&display_mutex);
	}

}


/* The file selection widget and the string to store the chosen filename */

static void
cb_save_spec_to_filename(GtkWidget *widget, gpointer user_data) 
{
  GtkWidget *file_selector = (GtkWidget *)user_data;
  topo_region_t 	main_region;
  const gchar *selected_filename;
  char buf[BUFSIZ];

  GUI_CALLBACK_ENTER();

  selected_filename = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));
 
  build_search_from_gui(&main_region);
  printf("saving spec to: %s\n", selected_filename);
  sprintf(buf, "%s", selected_filename);
  build_filter_spec(buf, &main_region);

  GUI_CALLBACK_LEAVE();
}


static void
write_search_config(const char *dirname, snap_search **searches, int nsearches)
{
	struct stat	buf;
	int			err;
	int			i;
	FILE *		conf_file;
	char		buffer[256];	/* XXX check */


	/* Do some test on the dir */
	/* XXX popup errors */
	err = stat(dirname, &buf);
	if (err != 0) {
		if (errno == ENOENT) {
			err = mkdir(dirname, 0777);
			if (err != 0) {
				perror("Failed to make save directory ");
				assert(0);
			}
		} else {
			perror("open data file: ");
			assert(0);
		}
	} else {
		/* make sure it is a dir */
		if (!S_ISDIR(buf.st_mode)) {
			fprintf(stderr, "%s is not a directory \n", dirname);	
			assert(0);
		}
	}

	sprintf(buffer, "%s/%s", dirname, SEARCH_CONFIG_FILE);
	conf_file = fopen(buffer, "w");

	for (i=0; i < nsearches; i++) {
		snap_searches[i]->write_config(conf_file, dirname);
	}
	fclose(conf_file);
}

void
update_search_entry(snap_search *cur_search, int row)
{
	GtkWidget *widget;
	widget = cur_search->get_search_widget();
	gtk_table_attach_defaults(GTK_TABLE(config_table), widget, 0, 1, 
		row+1, row+2);
	widget = cur_search->get_config_widget();
	gtk_table_attach_defaults(GTK_TABLE(config_table), widget, 1, 2, 
		row+1, row+2);
	widget = cur_search->get_edit_widget();
	gtk_table_attach_defaults(GTK_TABLE(config_table), widget, 2, 3, 
		row+1, row+2);
}

static GtkWidget *
create_search_window()
{
    GtkWidget *box2, *box1;
    GtkWidget *separator;
    GtkWidget *table;
    GtkWidget *frame, *widget;
    int row = 0;        /* current table row */
	int			i;

    GUI_THREAD_CHECK(); 

    box1 = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (box1);

    frame = gtk_frame_new("Searches");
    table = gtk_table_new(MAX_SEARCHES+1, 3, FALSE);
	config_table = table;	/* XXX */
    gtk_table_set_row_spacings(GTK_TABLE(table), 2);
    gtk_table_set_col_spacings(GTK_TABLE(table), 4);
    gtk_container_set_border_width(GTK_CONTAINER(table), 10);

    widget = gtk_label_new("Predicate");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_widget_show(widget);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, row, row+1);

    widget = gtk_label_new("Description");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_widget_show(widget);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 1, 2, row, row+1);

    widget = gtk_label_new("Edit");
    gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
	gtk_widget_show(widget);
	gtk_table_attach_defaults(GTK_TABLE(table), widget, 2, 3, row, row+1);

	for (i=0; i < num_searches; i++) {
		row = i + 1;
		widget = snap_searches[i]->get_search_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 0, 1, row, row+1);
		widget = snap_searches[i]->get_config_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 1, 2, row, row+1);
		widget = snap_searches[i]->get_edit_widget();
		gtk_table_attach_defaults(GTK_TABLE(table), widget, 2, 3, row, row+1);
	}
	gtk_container_add(GTK_CONTAINER(frame), table);
    gtk_box_pack_start(GTK_BOX(box1), frame, FALSE, FALSE, 10);
    gtk_widget_show(frame);
    gtk_widget_show(table);

    /* Add the start and stop buttons */

    box2 = gtk_hbox_new (FALSE, 10);
    gtk_container_set_border_width (GTK_CONTAINER (box2), 10);
    gtk_box_pack_end (GTK_BOX (box1), box2, FALSE, FALSE, 0);
    gtk_widget_show (box2);

    gui.start_button = gtk_button_new_with_label ("Start");
    g_signal_connect_swapped (G_OBJECT (gui.start_button), "clicked",
		    	     G_CALLBACK(cb_start_search), NULL);
    gtk_box_pack_start (GTK_BOX (box2), gui.start_button, TRUE, TRUE, 0);
    GTK_WIDGET_SET_FLAGS (gui.start_button, GTK_CAN_DEFAULT);
    gtk_widget_show (gui.start_button);

    gui.stop_button = gtk_button_new_with_label ("Stop");
    g_signal_connect_swapped (G_OBJECT (gui.stop_button), "clicked",
		    	     G_CALLBACK(cb_stop_search), NULL);
    gtk_box_pack_start (GTK_BOX (box2), gui.stop_button, TRUE, TRUE, 0);
    GTK_WIDGET_SET_FLAGS (gui.stop_button, GTK_CAN_DEFAULT);
    gtk_widget_set_sensitive(gui.stop_button, FALSE);
    gtk_widget_show (gui.stop_button);

    separator = gtk_hseparator_new ();
    gtk_box_pack_end (GTK_BOX (box1), separator, FALSE, FALSE, 0);
    gtk_widget_show (separator);

	return(box1);
}


static void
cb_load_search_from_dir(GtkWidget *widget, gpointer user_data) 
{
	GtkWidget *file_selector = (GtkWidget *)user_data;
	const gchar *dirname;
	char *	olddir;
	int			err;
	char buf[BUFSIZ];

	GUI_CALLBACK_ENTER();

	dirname = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));
	sprintf(buf, "%s/%s", dirname, SEARCH_CONFIG_FILE);

	olddir = getcwd(NULL, 0);

	err = chdir(dirname);
	assert(err == 0);

	/* XXXX cleanup all the old searches first */

	printf("Reading scapes: %s ...\n", buf);
	read_search_config(buf, snap_searches, &num_searches);
	printf("Done reading scapes...\n");
	
	err = chdir(olddir);
	assert(err == 0);
	free(olddir);

	gtk_widget_destroy(gui.search_widget);
    gui.search_widget = create_search_window();
    gtk_box_pack_start (GTK_BOX(gui.search_box), gui.search_widget, 
		FALSE, FALSE, 10);

	GUI_CALLBACK_LEAVE();
}

static void
cb_save_search_dir(GtkWidget *widget, gpointer user_data) 
{
	GtkWidget *file_selector = (GtkWidget *)user_data;
	const gchar *dirname;
	char buf[BUFSIZ];

	GUI_CALLBACK_ENTER();

	dirname = gtk_file_selection_get_filename(GTK_FILE_SELECTION(file_selector));

	sprintf(buf, "%s/%s", dirname, "search_config");

	/* XXXX cleanup all the old searches first */

	printf("Reading scapes: %s ...\n", buf);
	write_search_config(dirname, snap_searches, num_searches);
	printf("Done reading scapes...\n");

	gtk_widget_destroy(gui.search_widget);
    gui.search_widget = create_search_window();
    gtk_box_pack_start (GTK_BOX(gui.search_box), gui.search_widget, 
		FALSE, FALSE, 10);

	GUI_CALLBACK_LEAVE();
}


static void
cb_load_search() 
{
	GtkWidget *file_selector;

  	GUI_CALLBACK_ENTER();
	printf("load search \n");

	/* Create the selector */
  	file_selector = gtk_file_selection_new("Dir name for search");
  	gtk_file_selection_show_fileop_buttons(GTK_FILE_SELECTION(file_selector));

  	g_signal_connect(GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
		    "clicked", G_CALLBACK(cb_load_search_from_dir),
		    (gpointer) file_selector);
   			   
  	/* 
     * Ensure that the dialog box is destroyed when the user clicks a button. 
	 * Use swapper here to get the right argument to destroy (YUCK).
     */
  	g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
			    "clicked",
			    G_CALLBACK(gtk_widget_destroy), 
			    (gpointer) file_selector); 

	g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->cancel_button),
			    "clicked",
			    G_CALLBACK (gtk_widget_destroy),
			    (gpointer) file_selector); 

	/* Display that dialog */
	gtk_widget_show (file_selector);
	GUI_CALLBACK_LEAVE();
}


static void
cb_save_search() 
{
  GtkWidget *file_selector;

  GUI_CALLBACK_ENTER();
  printf("save search \n");
  GUI_CALLBACK_LEAVE();
  return;

  /* Create the selector */
  file_selector = gtk_file_selection_new("Filename for filter spec.");
  //gtk_file_selection_set_filename(GTK_FILE_SELECTION(file_selector),
				  //"sample.spec");
  gtk_file_selection_show_fileop_buttons(GTK_FILE_SELECTION(file_selector));

  g_signal_connect(GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
		    "clicked",
		    G_CALLBACK(cb_save_spec_to_filename),
		    (gpointer) file_selector);
   			   
  /* Ensure that the dialog box is destroyed when the user clicks a button. */
  g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
			    "clicked",
			    G_CALLBACK (gtk_widget_destroy), 
			    (gpointer) file_selector); 

  g_signal_connect_swapped (GTK_OBJECT (GTK_FILE_SELECTION (file_selector)->cancel_button),
			    "clicked",
			    G_CALLBACK (gtk_widget_destroy),
			    (gpointer) file_selector); 
   
   /* Display that dialog */
   gtk_widget_show (file_selector);

  GUI_CALLBACK_LEAVE();
}

static void
cb_save_search_as() 
{
	GtkWidget *file_selector;

  	GUI_CALLBACK_ENTER();
	printf("Save search \n");

	/* Create the selector */
  	file_selector = gtk_file_selection_new("Save Directory:");
  	gtk_file_selection_show_fileop_buttons(GTK_FILE_SELECTION(file_selector));

  	g_signal_connect(GTK_OBJECT (GTK_FILE_SELECTION(file_selector)->ok_button),
		    "clicked", G_CALLBACK(cb_save_search_dir),
		    (gpointer) file_selector);
   			   
  	/* 
     * Ensure that the dialog box is destroyed when the user clicks a button. 
	 * Use swapper here to get the right argument to destroy (YUCK).
     */
  	g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->ok_button),
			    "clicked",
			    G_CALLBACK(gtk_widget_destroy), 
			    (gpointer) file_selector); 

	g_signal_connect_swapped(GTK_OBJECT(GTK_FILE_SELECTION(file_selector)->cancel_button),
			    "clicked",
			    G_CALLBACK (gtk_widget_destroy),
			    (gpointer) file_selector); 

	/* Display that dialog */
	gtk_widget_show (file_selector);
	GUI_CALLBACK_LEAVE();
}



static void 
cb_show_stats(GtkButton* item, gpointer data)
{
    GUI_CALLBACK_ENTER();
    create_stats_win(shandle, expert_mode);
    GUI_CALLBACK_LEAVE();
}

/* For the check button */
static void
cb_toggle_stats( gpointer   callback_data,
		 guint      callback_action,
		 GtkWidget *menu_item )
{
    GUI_CALLBACK_ENTER();
/*     if(GTK_CHECK_MENU_ITEM (menu_item)->active) { */
/*       create_stats_win(shandle, expert_mode); */
/*     } else { */
/*       close_stats_win(); */
/*     } */
    toggle_stats_win(shandle, expert_mode);
    GUI_CALLBACK_LEAVE();
}


/* For the check button */
static void
cb_toggle_dump_attributes( gpointer   callback_data,
			   guint      callback_action,
			   GtkWidget *menu_item )
{
    GUI_CALLBACK_ENTER();
    dump_attributes = GTK_CHECK_MENU_ITEM (menu_item)->active;
    GUI_CALLBACK_LEAVE();
}


/* For the check button */
static void
cb_toggle_expert_mode( gpointer   callback_data,
		       guint      callback_action,
		       GtkWidget *menu_item )
{
    GUI_CALLBACK_ENTER();
    if( GTK_CHECK_MENU_ITEM (menu_item)->active ) {
      expert_mode = 1;
      show_expert_widgets();
    } else {
      expert_mode = 0;
      hide_expert_widgets();
    }
    GUI_CALLBACK_LEAVE();
}


/* ********************************************************************** */
/* widget setup functions */
/* ********************************************************************** */

static void
create_image_info(GtkWidget *container_box, image_info_t *img_info)
{

	char		data[BUFSIZ];

	GUI_THREAD_CHECK(); 

	/*
	 * Now create another region that has the controls
	 * that manipulate the current image being displayed.
	 */

	img_info->parent_box = container_box;
	img_info->info_box1 = gtk_hbox_new (FALSE, 0);
    	gtk_container_set_border_width(GTK_CONTAINER(img_info->info_box1), 10);

	GtkWidget *frame;
	frame = gtk_frame_new("Image Info");
	gtk_container_add(GTK_CONTAINER(frame), img_info->info_box1);
    	gtk_widget_show(frame);

    	gtk_box_pack_start(GTK_BOX(img_info->parent_box), 
			frame, TRUE, TRUE, 0);
    	gtk_widget_show(img_info->info_box1);

/* 	img_info->info_box2 = gtk_hbox_new (FALSE, 10); */
/*     	gtk_container_set_border_width(GTK_CONTAINER(img_info->info_box2), 10); */
/*     	gtk_box_pack_start(GTK_BOX(img_info->parent_box),  */
/* 			img_info->info_box2, FALSE, FALSE, 0); */
/*     	gtk_widget_show(img_info->info_box2); */


	/* 
	 * image name
	 */
	//sprintf(data, "%-10s:\n", "Name:");
	img_info->name_tag = gtk_label_new("Name:");
    	gtk_box_pack_start (GTK_BOX(img_info->info_box1), 
			img_info->name_tag, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->name_tag);


	sprintf(data, "%-60s:", " ");
	img_info->name_label = gtk_label_new(data);
    	gtk_box_pack_start (GTK_BOX(img_info->info_box1), 
			img_info->name_label, TRUE, TRUE, 0);
    	gtk_widget_show(img_info->name_label);

	/*
	 * Place holder and blank spot for the number of bounding
	 * boxes found.
	 */
	sprintf(data, "%-3s", " ");
	img_info->count_label = gtk_label_new(data);
    	gtk_box_pack_end (GTK_BOX(img_info->info_box1), 
			img_info->count_label, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->count_label);

	img_info->count_tag = gtk_label_new("Num Scenes:");
    	gtk_box_pack_end(GTK_BOX(img_info->info_box1), 
			img_info->count_tag, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->count_tag);
}


static void 
cb_next_image(GtkButton* item, gpointer data)
{
	GUI_CALLBACK_ENTER();
	image_controls.zlevel = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(image_controls.zbutton));
	clear_thumbnails();
	GUI_CALLBACK_LEAVE(); /* need to put this here instead of at
				 end because signal wakes up another
				 thread immediately... */

	pthread_mutex_lock(&display_mutex);
	image_controls.cur_op = CNTRL_NEXT;
	pthread_cond_signal(&display_cond);
	pthread_mutex_unlock(&display_mutex);

}


static void
cb_img_info(GtkWidget *widget, gpointer data) 
{
	thumbnail_t *thumb;

	GUI_CALLBACK_ENTER();
	thumb = (thumbnail_t *)gtk_object_get_user_data(GTK_OBJECT(widget));

	/* the data gpointer passed in seems to not be the data
	 * pointer we gave gtk. instead, we save a pointer in the
	 * widget. -RW */

	//fprintf(stderr, "thumb=%p\n", thumb);

	if(thumb->img) {
		write_image_info(&image_information, thumb->name, thumb->nboxes);
	}
	GUI_CALLBACK_LEAVE();
}


static void
create_image_control(GtkWidget *container_box,
		     image_info_t *img_info,
		     image_control_t *img_cntrl)
{
	//GtkWidget *label;
    	GtkObject *adj;

	GUI_THREAD_CHECK(); 

	/*
	 * Now create another region that has the controls
	 * that manipulate the current image being displayed.
	 */

	img_cntrl->parent_box = container_box;
	img_cntrl->control_box = gtk_hbox_new (FALSE, 10);
    	gtk_container_set_border_width(GTK_CONTAINER(img_cntrl->control_box), 0);
    	gtk_box_pack_start(GTK_BOX(img_cntrl->parent_box), 
			img_cntrl->control_box, FALSE, FALSE, 0);
    	gtk_widget_show(img_cntrl->control_box);

	img_cntrl->next_button = gtk_button_new_with_label ("Next");
    	g_signal_connect_swapped(G_OBJECT(img_cntrl->next_button), 
			"clicked", G_CALLBACK(cb_next_image), NULL);
    	gtk_box_pack_end(GTK_BOX(img_cntrl->control_box), 
			img_cntrl->next_button, FALSE, FALSE, 0);
    	GTK_WIDGET_SET_FLAGS (img_cntrl->next_button, GTK_CAN_DEFAULT);
    	//gtk_widget_grab_default(img_cntrl->next_button);
    	gtk_widget_show(img_cntrl->next_button);

/* 	label = gtk_label_new ("more images"); */
/*     	gtk_box_pack_end(GTK_BOX(img_cntrl->control_box), label, FALSE, FALSE, 0); */
/*     	gtk_widget_show(label); */


	img_info->qsize_label = gtk_label_new ("");
    	gtk_box_pack_end(GTK_BOX(img_cntrl->control_box), 
			img_info->qsize_label, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->qsize_label);
	gtk_timeout_add(500 /* ms */, timeout_write_qsize, img_info->qsize_label);

	img_info->tobjs_label = gtk_label_new ("");
    	gtk_box_pack_start(GTK_BOX(img_cntrl->control_box), 
			img_info->tobjs_label, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->tobjs_label);
	gtk_timeout_add(500 /* ms */, timeout_write_tobjs, img_info->tobjs_label);

	img_info->sobjs_label = gtk_label_new ("");
    	gtk_box_pack_start(GTK_BOX(img_cntrl->control_box), 
			img_info->sobjs_label, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->sobjs_label);
	gtk_timeout_add(500 /* ms */, timeout_write_sobjs, img_info->sobjs_label);

	img_info->dobjs_label = gtk_label_new ("");
    	gtk_box_pack_start(GTK_BOX(img_cntrl->control_box), 
			img_info->dobjs_label, FALSE, FALSE, 0);
    	gtk_widget_show(img_info->dobjs_label);
	gtk_timeout_add(500 /* ms */, timeout_write_dobjs, img_info->dobjs_label);

	
	
/* 	label = gtk_label_new ("Zoom"); */
/*     	gtk_box_pack_start (GTK_BOX(img_cntrl->control_box),  */
/* 			label, FALSE, FALSE, 0); */
    	//gtk_widget_show(label);


    	adj = gtk_adjustment_new(2.0, 1.0, 10.0, 1.0, 1.0, 1.0);
	img_cntrl->zlevel = 2;
    	img_cntrl->zbutton = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1.0, 0);
    	gtk_box_pack_start (GTK_BOX(img_cntrl->control_box),img_cntrl->zbutton, 
			FALSE, FALSE, 0);
    	//gtk_widget_show(img_cntrl->zbutton);


	img_cntrl->cur_op = CNTRL_ELEV;

}

/*
 * Create the region that will display the results of the 
 * search.
 */
static void
create_display_region(GtkWidget *main_box)
{

	GtkWidget *	box2;
 	GtkWidget *	separator;
	GtkWidget *	x;

	GUI_THREAD_CHECK(); 

	/*
	 * Create a box that holds the following sub-regions.
	 */
    	x = gtk_hbox_new(FALSE, 0);
    	gtk_container_set_border_width(GTK_CONTAINER(x), 10);
    	gtk_box_pack_start(GTK_BOX(main_box), x, TRUE, TRUE, 0);
    	gtk_widget_show(x);

/*     	separator = gtk_vseparator_new(); */
/*     	gtk_box_pack_start(GTK_BOX(x), separator, FALSE, FALSE, 0); */
/*     	gtk_widget_show (separator); */

/*     	separator = gtk_vseparator_new(); */
/*     	gtk_box_pack_end(GTK_BOX(x), separator, FALSE, FALSE, 0); */
/*     	gtk_widget_show (separator); */

	GtkWidget *result_box;
    	result_box = gtk_vbox_new(FALSE, 10);
    	gtk_container_set_border_width(GTK_CONTAINER(result_box), 0);
    	gtk_box_pack_start(GTK_BOX(x), result_box, TRUE, TRUE, 0);
    	gtk_widget_show(result_box);

	/* create the image information area */
	create_image_info(result_box, &image_information);

    	separator = gtk_hseparator_new ();
    	gtk_box_pack_start (GTK_BOX(result_box), separator, FALSE, FALSE, 0);
    	gtk_widget_show (separator);

	/*
	 * Create the region that will hold the current results
	 * being displayed.
	 */
    	box2 = gtk_vbox_new(FALSE, 10);
    	gtk_container_set_border_width (GTK_CONTAINER (box2), 10);
    	gtk_box_pack_start (GTK_BOX (result_box), box2, TRUE, TRUE, 0);
    	gtk_widget_show(box2);


/* 	image_window = gtk_scrolled_window_new(NULL, NULL); */
/*     	gtk_box_pack_start(GTK_BOX (box2), image_window, TRUE, TRUE, 0); */
/*     	gtk_widget_show(image_window); */

/* 	image_view = gtk_viewport_new(NULL, NULL); */
/* 	gtk_container_add(GTK_CONTAINER(image_window), image_view); */
/*     	gtk_widget_show(image_view); */

	GtkWidget *thumbnail_view;
	thumbnail_view = gtk_table_new(TABLE_ROWS, TABLE_COLS, TRUE);
	gtk_box_pack_start(GTK_BOX (box2), thumbnail_view, TRUE, TRUE, 0);
	for(int i=0; i< TABLE_ROWS; i++) {
		for(int j=0; j< TABLE_COLS; j++) {
			GtkWidget *widget, *eb;
			GtkWidget *frame;

			eb = gtk_event_box_new();
			
			frame = gtk_frame_new(NULL);
			gtk_frame_set_label(GTK_FRAME(frame), "");
			gtk_container_add(GTK_CONTAINER(eb), frame);
			gtk_widget_show(frame);

			widget = gtk_viewport_new(NULL, NULL);
			gtk_widget_set_size_request(widget, THUMBSIZE_X, THUMBSIZE_Y);
			gtk_container_add(GTK_CONTAINER(frame), widget);
			gtk_table_attach_defaults(GTK_TABLE(thumbnail_view), eb,
						  j, j+1, i, i+1);
			gtk_widget_show(widget);
			gtk_widget_show(eb);

			thumbnail_t *thumb = (thumbnail_t *)malloc(sizeof(thumbnail_t));
			thumb->marked = 0;
			thumb->img = NULL;
			thumb->viewport = widget;
			thumb->frame = frame;
			thumb->name[0] = '\0';
			thumb->nboxes = 0;
			thumb->nfaces = 0;
			thumb->hooks = NULL;
			TAILQ_INSERT_TAIL(&thumbnails, thumb, link);

			//fprintf(stderr, "thumb=%p\n", thumb);

			/* the data pointer mechanism seems to be
			 * broken, so use the object user pointer
			 * instead */
			gtk_object_set_user_data(GTK_OBJECT(eb), thumb);
			gtk_signal_connect(GTK_OBJECT(eb),
					   "enter-notify-event",
					   GTK_SIGNAL_FUNC(cb_img_info),
					   (gpointer)thumb);
			gtk_signal_connect(GTK_OBJECT(eb),
					   "button-press-event",
					   GTK_SIGNAL_FUNC(cb_img_popup),
					   (gpointer)thumb);
		}
	}
    gtk_widget_show(thumbnail_view);


	/* create the image control area */
	create_image_control(result_box, &image_information, &image_controls);


	clear_image_info(&image_information);
	disable_image_control(&image_controls);
}

/* set check button when activated */
/* static void */
/* cb_enable_check_button(GtkWidget *widget) { */
/*   GUI_CALLBACK_ENTER(); */

/*   GtkWidget *button  */
/*     = (GtkWidget *)gtk_object_get_user_data(GTK_OBJECT(widget)); */
/*   gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), TRUE); */

/*   GUI_CALLBACK_LEAVE(); */
/* } */




static void
cb_quit() {
  printf("MARKED: %d of %d seen\n", user_measurement.total_marked, 
	 user_measurement.total_seen);
  gtk_main_quit();
}




/* Our menu, an array of GtkItemFactoryEntry structures that defines each menu item */
static GtkItemFactoryEntry menu_items[] = { /* XXX */

  { "/_File",         NULL,         NULL,           0, "<Branch>" },
  //  { "/File/_New",     "<control>N", (GtkItemFactoryCallback)print_hello,    0, "<StockItem>", GTK_STOCK_NEW },
  //  { "/File/_Open",    "<control>O", (GtkItemFactoryCallback)print_hello,    0, "<StockItem>", GTK_STOCK_OPEN },
  //  { "/File/_Save",    "<control>S", (GtkItemFactoryCallback)print_hello,    0, "<StockItem>", GTK_STOCK_SAVE },
  //  { "/File/Save _As", NULL,         NULL,           0, "<Item>" },
  { "/File/Load Search",   NULL,  G_CALLBACK(cb_load_search),   0, "<Item>" },
  { "/File/Save Search",   NULL,  G_CALLBACK(cb_save_search),   0, "<Item>" },
  { "/File/Save Search As.",   NULL,  G_CALLBACK(cb_save_search_as),  0, "<Item>" },
  { "/File/sep1",     NULL,         NULL,           0, "<Separator>" },
  { "/File/_Quit",    "<CTRL>Q", (GtkItemFactoryCallback)cb_quit, 0, "<StockItem>", GTK_STOCK_QUIT },
  { "/_View",                NULL,  NULL,                                  0, "<Branch>" },
  { "/_View/Stats Window",   "<CTRL>I",  G_CALLBACK(cb_toggle_stats),   0, "<Item>" },

  { "/Options",                 NULL, NULL, 0, "<Branch>" },
  { "/Options/sep1",            NULL, NULL, 0, "<Separator>" },
  //{ "/Options/Expert Mode",     NULL, G_CALLBACK(cb_toggle_expert_mode), 0, "<CheckItem>" },
  { "/Options/Expert Mode",     "<CTRL>E", G_CALLBACK(cb_toggle_expert_mode), 0, "<CheckItem>" },
  { "/Options/Dump Attributes", NULL, G_CALLBACK(cb_toggle_dump_attributes), 0, "<CheckItem>" },

  { "/Albums",                  NULL, NULL, 0, "<Branch>" },
  { "/Albums/tear",             NULL, NULL, 0, "<Tearoff>" },

  //  { "/Options/tear",  NULL,         NULL,           0, "<Tearoff>" },
  //  { "/Options/Check", NULL,         (GtkItemFactoryCallback)print_toggle,   1, "<CheckItem>" },
  //  { "/Options/sep",   NULL,         NULL,           0, "<Separator>" },
  //  { "/Options/Rad1",  NULL,         (GtkItemFactoryCallback)print_selected, 1, "<RadioItem>" },
  //  { "/Options/Rad2",  NULL,         (GtkItemFactoryCallback)print_selected, 2, "/Options/Rad1" },
  //  { "/Options/Rad3",  NULL,         (GtkItemFactoryCallback)print_selected, 3, "/Options/Rad1" },
  //  { "/_Help",         NULL,         NULL,           0, "<LastBranch>" },
  //  { "/_Help/About",   NULL,         NULL,           0, "<Item>" },
  { NULL, NULL, NULL }
};

static void 
cb_collection(gpointer callback_data, guint callback_action, 
	GtkWidget  *menu_item ) 
{

  /* printf("cb_collection: #%d\n", callback_action); */

  if(GTK_CHECK_MENU_ITEM(menu_item)->active) {
    collections[callback_action].active = 1;
  } else {
    collections[callback_action].active = 0;
  }
}


/* Returns a menubar widget made from the above menu */
static GtkWidget *
get_menubar_menu( GtkWidget  *window )
{
  GtkItemFactory *item_factory;
  GtkAccelGroup *accel_group;
  gint nmenu_items;

  /* Make an accelerator group (shortcut keys) */
  accel_group = gtk_accel_group_new ();

  /* Make an ItemFactory (that makes a menubar) */
  item_factory = gtk_item_factory_new (GTK_TYPE_MENU_BAR, "<main>",
                                       accel_group);

  GtkItemFactoryEntry *tmp_menu;
  
  for(tmp_menu = menu_items, nmenu_items = 0;
      (tmp_menu->path);
      tmp_menu++) {
    nmenu_items++;
  }

  /* This function generates the menu items. Pass the item factory,
     the number of items in the array, the array itself, and any
     callback data for the the menu items. */
  gtk_item_factory_create_items (item_factory, nmenu_items, menu_items, NULL);

  /* create more menu items */
  struct collection_t *tmp_coll;
  for(tmp_coll = collections; tmp_coll->name; tmp_coll++) {
    GtkItemFactoryEntry entry;
    char buf[BUFSIZ];

    sprintf(buf, "/Albums/%s", tmp_coll->name);
    for(char *s=buf; *s; s++) {
      if(*s == '_') {
	*s = ' ';
      }
    }
    entry.path = strdup(buf);
    entry.accelerator = NULL;
    entry.callback = G_CALLBACK(cb_collection);
    entry.callback_action = tmp_coll - collections;
    entry.item_type = "<CheckItem>";
    gtk_item_factory_create_item(item_factory,
				 &entry,
				 NULL,
				 1); /* XXX guess, no doc */

    GtkWidget *widget = gtk_item_factory_get_widget(item_factory, buf);
    //gtk_widget_set_sensitive(widget, FALSE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(widget), tmp_coll->active);

    //tmp_menu++;
    //nmenu_items++;
  }

  /* Attach the new accelerator group to the window. */
  gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

  /* Finally, return the actual *menu* bar created by the item factory. */
  return gtk_item_factory_get_widget (item_factory, "<main>");
}



/* 
 * makes the main window 
 */

static void 
create_main_window(void)
{
	GtkWidget * separator;
    GtkWidget *main_vbox;
    GtkWidget *menubar;
    GtkWidget *main_box;

    GUI_THREAD_CHECK(); 

    /*
     * Create the the main window.
     */
    gui.main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    g_signal_connect (G_OBJECT (gui.main_window), "destroy",
		      G_CALLBACK (cb_quit), NULL);

    gtk_window_set_title(GTK_WINDOW (gui.main_window), "Diamond SnapFind");

    main_vbox = gtk_vbox_new (FALSE, 1);
    gtk_container_set_border_width (GTK_CONTAINER (main_vbox), 1);
    gtk_container_add (GTK_CONTAINER (gui.main_window), main_vbox);
    gtk_widget_show(main_vbox);

    menubar = get_menubar_menu (gui.main_window);
    gtk_box_pack_start (GTK_BOX (main_vbox), menubar, FALSE, TRUE, 0);
    gtk_widget_show(menubar);

    main_box = gtk_hbox_new (FALSE, 0);
    //gtk_container_add (GTK_CONTAINER (main_vbox), main_box);
    gtk_box_pack_end (GTK_BOX (main_vbox), main_box, FALSE, TRUE, 0);
    gtk_widget_show (main_box);

    gui.search_box = gtk_vbox_new (FALSE, 0);
    gtk_box_pack_start (GTK_BOX(main_box), gui.search_box, FALSE, FALSE, 10);
    gtk_widget_show (gui.search_box);

    gui.search_widget = create_search_window();
    gtk_box_pack_start (GTK_BOX(gui.search_box), gui.search_widget, 
		FALSE, FALSE, 10);

    separator = gtk_vseparator_new();
    gtk_box_pack_start(GTK_BOX(main_box), separator, FALSE, FALSE, 0);
    gtk_widget_show (separator);

    create_display_region(main_box);

    gtk_widget_show(gui.main_window);
}





static void
usage(char **argv) 
{
  fprintf(stderr, "usage: %s [options]\n", basename(argv[0]));
  fprintf(stderr, "  -h        - help\n");
  fprintf(stderr, "  -e        - expert mode\n");
  fprintf(stderr, "  -s<file>  - histo index file\n");
  fprintf(stderr, "  --similarity <filter>=<val>  - set default threshold (repeatable)\n");
  fprintf(stderr, "  --min-faces=<num>       - set min number of faces \n");
  fprintf(stderr, "  --face-levels=<num>     - set face detector levels \n");
  fprintf(stderr, "  --dump-spec=<file>      - dump spec file and exit \n");
  fprintf(stderr, "  --dump-objects          - start search and dump objects\n");
  fprintf(stderr, "  --read-spec=<file>      - use spec file. requires dump-objects\n");
}



static int
set_export_threshold(char *arg) {
  char *s = arg;

  while(*s && *s != '=') {
    s++;
  }
  if(!*s) return -1;		/* error */
  *s++ = '\0';

  export_threshold_t *et = (export_threshold_t *)malloc(sizeof(export_threshold_t));
  assert(et);
  et->name = arg;
  double d = atof(s);
  if(d > 1) d /= 100.0;
  if(d > 1 || d < 0) {
    fprintf(stderr, "bad distance: %s\n", s);
    return -1;
  }
  et->distance = 1.0 - d;	/* similarity */
  et->index = -1;

  TAILQ_INSERT_TAIL(&export_list, et, link);

  return 0;
}

int 
main(int argc, char *argv[])
{

	pthread_t 	search_thread;
	int		err;
	char *scapeconf = "histo/search_config";
	int c;
	static const char *optstring = "hes:f:";
	struct option long_options[] = {
	  {"dump-spec", required_argument, NULL, 0},
	  {"dump-objects", no_argument, NULL, 0},
	  {"read-spec", required_argument, NULL, 0},
	  {"similarity", required_argument, NULL, 'f'},
	  {"min-faces", required_argument, NULL, 0},
	  {"face-levels", required_argument, NULL, 0},
	  {0, 0, 0, 0}
	};
	int option_index = 0;

	/*
	 * Start the GUI
	 */

	GUI_THREAD_INIT();
    	gtk_init(&argc, &argv);
	gtk_rc_parse("gtkrc");
	gdk_rgb_init();
	printf("Starting main\n");

	while((c = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
		switch(c) {
		case 0:
		  if(strcmp(long_options[option_index].name, "dump-spec") == 0) {
		    dump_spec_file = optarg;
		  } else if(strcmp(long_options[option_index].name, "dump-objects") == 0) {
		    dump_objects = 1;
		  } else if(strcmp(long_options[option_index].name, "read-spec") == 0) {
		    read_spec_filename = optarg;
		  } else if(strcmp(long_options[option_index].name, "min-faces") == 0) {
		    default_min_faces = atoi(optarg);
		  } else if(strcmp(long_options[option_index].name, "face-levels") == 0) {
		    default_face_levels = atoi(optarg);
		  }
		  break;
		case 'e':
		  fprintf(stderr, "expert mode must now be turned on with the menu option\n");
		  //expert_mode = 1;
		  break;
		case 's':
		  scapeconf = optarg;
		  break;
		case 'f':
		  if(set_export_threshold(optarg) < 0) {
		    usage(argv);
		    exit(1);
		  }
		  break;
		case ':':
		  fprintf(stderr, "missing parameter\n");
		  exit(1);
		case 'h':
		case '?':
		default:
		  usage(argv);
		  exit(1);
		  break;
		}		
	}

	printf("Initializing communciation rings...\n");

	/*
	 * Initialize communications rings with the thread
	 * that interacts with the search thread.
	 */
	err = ring_init(&to_search_thread, TO_SEARCH_RING_SIZE);
	if (err) {
		exit(1);
	}

	err = ring_init(&from_search_thread, FROM_SEARCH_RING_SIZE);
	if (err) {
		exit(1);
	}

#ifdef	XXX
	printf("Reading scapes: %s ...\n", scapeconf);
	read_search_config(scapeconf, snap_searches, &num_searches);
	printf("Done reading scapes...\n");
#endif

#ifdef	XXX_NOW
	/* read the histograms while we are at it */
	for(int i=0; i<nscapes; i++) {
		char buf[MAX_PATH];

		sprintf(buf, "%s", scapes[i].file);
		printf("processing %s\n", buf);

		switch(scapes[i].fsp_info.type) {
		case FILTER_TYPE_COLOR:
		  err = patch_spec_make_histograms(dirname(buf), &scapes[i].fsp_info);
		  break;
		case FILTER_TYPE_TEXTURE:
		  err = texture_make_features(dirname(buf), &scapes[i].fsp_info);
		  break;
		case FILTER_TYPE_ARENA:
		  err = arena_make_vectors(dirname(buf), &scapes[i].fsp_info);
		  break;
		}
		if(err) {
		  scapes[i].disabled = 1;
		}
	}
#endif

	/* 
	 * read the list of collections
	 */
	{
	  void *cookie;
	  char *name;
	  int err;
	  int pos = 0;
	  err = nlkup_first_entry(&name, &cookie);
	  while(!err && pos < MAX_ALBUMS) {
	    collections[pos].name = name;
	    collections[pos].active = pos ? 0 : 1; /* first one active */
	    pos++;
	    err = nlkup_next_entry(&name, &cookie);
	  }
	  collections[pos].name = NULL;
	}


	/*
	 * Create the main window.
	 */
	tooltips = gtk_tooltips_new();
	gtk_tooltips_enable(tooltips);


	GUI_THREAD_ENTER();
    	create_main_window();
	GUI_THREAD_LEAVE();

	/* 
	 * initialize and start the background thread 
	 */
	init_search();
	err = pthread_create(&search_thread, PATTR_DEFAULT, sfind_search_main, NULL);
	if (err) {
		perror("failed to create search thread");
		exit(1);
	}
	//gtk_timeout_add(100, func, NULL);


	/*
	 * Start the main loop processing for the GUI.
	 */

	MAIN_THREADS_ENTER(); 
    	gtk_main();
 	MAIN_THREADS_LEAVE();  

	return(0);
}