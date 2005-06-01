/*
 * 	SnapFind (Release 0.9)
 *      An interactive image search application
 *
 *      Copyright (c) 2002-2005, Intel Corporation
 *      All Rights Reserved
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */


/*
 * color histogram filter
 * rgb reader
 */

#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>

#include "face.h"
#include "common_consts.h"
#include "filter_api.h"
#include "fil_file.h"
#include "fil_image_tools.h"
#include "rgb.h"
#include "histo.h"
#include "fil_histo.h"
#include "fil_tools.h"

/* #define VERBOSE 1 */

typedef struct
{
	int             	scale;
	histo_type_t	type;
}
hintegrate_data_t;

typedef struct
{
	int             num_hist;
}
hpass_data_t;


/* call to read the cycle counter */
#define rdtscll(val) __asm__ __volatile__("rdtsc" : "=A" (val))

static unsigned long long
read_cycle()
{
	unsigned long long      foo;
	rdtscll(foo);
	return(foo);
}


#define ASSERT(exp)							\
if(!(exp)) {								\
  lf_log(fhandle, LOGL_ERR, "Assertion %s failed at ", #exp);		\
  lf_log(fhandle, LOGL_ERR, "%s, line %d.", __FILE__, __LINE__);	\
  pass = -1;								\
  goto done;								\
}

int
f_init_pnm2rgb(int numarg, char **args, int blob_len, void *blob, void **data)
{

	assert(numarg == 0);
	*data = NULL;
	return (0);
}

int
f_fini_pnm2rgb(void *data)
{
	return (0);
}

/*
 * filter eval function to create an RGB_IMAGE attribute
 */

int
f_eval_pnm2rgb(lf_obj_handle_t ohandle, int numout,
               lf_obj_handle_t * ohandles, void *user_data)
{
	RGBImage       *img;
	int             err = 0, pass = 1;
	lf_fhandle_t    fhandle = 0;

	lf_log(fhandle, LOGL_TRACE, "f_pnm2rgb: enter");

	img = get_rgb_img(ohandle);
	if (img == NULL) {
		return(0);
	}

	/*
	 * save some attribs 
	 */
#ifdef	XXX
	lf_write_attr(fhandle, ohandle, IMG_HEADERLEN, sizeof(int),
	              (char *) &headerlen);
#endif

	lf_write_attr(fhandle, ohandle, ROWS, sizeof(int), (char *) &img->height);
	lf_write_attr(fhandle, ohandle, COLS, sizeof(int), (char *) &img->width);

	/*
	 * save img as an attribute 
	 */
	err = lf_write_attr(fhandle, ohandle, RGB_IMAGE, img->nbytes, (char *) img);
	ASSERT(!err);
done:
	if (img)
		lf_free_buffer(fhandle, (char *) img);
	lf_log(fhandle, LOGL_TRACE, "f_pnm2rgb: done");
	return pass;
}


/*
 ********************************************************************** */

/*
 * read in volatile state from args. also see patch_spec_write_args 
 */
static int
patch_spec_read_args(lf_fhandle_t fhandle, histo_config_t * hconfig,
                     int argc, char **args)
{
	int             i,
	j,
	err;
	patch_t        *patch;
	double          sum;
	int             pass = 1;
	int             nbins = HBINS * HBINS * HBINS;  /* XXX */

	/*
	 * XXX for now until we allow variable amount of bins 
	 */
	assert(HBINS == hconfig->bins);

	TAILQ_INIT(&hconfig->patchlist);
	for (i = 0; i < hconfig->num_patches; i++) {
		err = lf_alloc_buffer(fhandle, sizeof(patch_t), (char **) &patch);
		ASSERT(patch);
		ASSERT(!err);
		histo_clear(&patch->histo);

		argc -= nbins;
		ASSERT(argc >= 0);

		sum = 0.0;
		for (j = 0; j < nbins; j++) {
			patch->histo.data[j] = atof(*args++);
			sum += patch->histo.data[j];
		}
		patch->histo.weight = 1.0;
		/*
		 * any errors should be close to 1.0 
		 */
		assert(fabs(sum - 1.0) < 0.001);

		// argc -= 4; ASSERT(argc >= 0);
		// patch->histo.weight = atof(*args++);
		// patch->threshold = atof(*args++);
		// patch->minx = atoi(*args++);
		// patch->miny = atoi(*args++);
		TAILQ_INSERT_TAIL(&hconfig->patchlist, patch, link);
	}

done:
	return pass;
}




typedef struct write_notify_context_t
{
	lf_fhandle_t    fhandle;
	lf_obj_handle_t ohandle;

}
write_notify_context_t;


#ifdef __cplusplus
extern          "C"
{
#endif

	static void     write_notify_f(void *cont, search_param_t * param);

#ifdef __cplusplus
}
#endif

static void
write_notify_f(void *cont, search_param_t * param)
{
	write_notify_context_t *context = (write_notify_context_t *) cont;

	write_param(context->fhandle, context->ohandle, HISTO_BBOX_FMT, param,
	            param->id);
	lf_log(context->fhandle, LOGL_TRACE, "found histo match");
}


/*
 ********************************************************************** */
/*
 * Initialize filter to detect histograms.
 */
int
f_init_histo_detect(int numarg, char **args, int blob_len,
                    void *blob, void **data)
{
	histo_config_t *hconfig;
	lf_fhandle_t    fhandle = 0;    /* XXX */
	int             err;

	/*
	 * filter initialization
	 */
	err = lf_alloc_buffer(fhandle, sizeof(*hconfig), (char **) &hconfig);
	assert(!err);
	assert(hconfig);

	assert(numarg > 6);
	hconfig->name = strdup(args[0]);
	assert(hconfig->name != NULL);

	hconfig->scale = atof(args[1]);
	hconfig->xsize = atoi(args[2]);
	hconfig->ysize = atoi(args[3]);
	hconfig->stride = atoi(args[4]);
	hconfig->req_matches = atoi(args[5]);
	hconfig->bins = atoi(args[6]);
	hconfig->simularity = atof(args[7]);
	hconfig->distance_type = atoi(args[8]);
	hconfig->type = atoi(args[9]);
	hconfig->num_patches = atoi(args[10]);

	/*
	 * read the histogram patches in 
	 */
	err = patch_spec_read_args(fhandle, hconfig, numarg - 11, args + 11);
	assert(err);

	/*
	 * save the data pointer 
	 */
	*data = (void *) hconfig;
	return (0);
}

int
f_fini_histo_detect(void *data)
{
	patch_t        *patch;
	lf_fhandle_t    fhandle = 0;    /* XXX */
	histo_config_t *hconfig = (histo_config_t *) data;

	while ((patch = TAILQ_FIRST(&hconfig->patchlist))) {
		TAILQ_REMOVE(&hconfig->patchlist, patch, link);
		lf_free_buffer(fhandle, (char *) patch);
	}
	lf_free_buffer(fhandle, (char *) hconfig);

	return (0);
}


int
f_eval_histo_detect(lf_obj_handle_t ohandle, int numout,
                    lf_obj_handle_t * ohandles, void *f_data)
{
	int             pass = 0;
	int             err;
	RGBImage       *img = NULL;
	off_t           bsize;
	bbox_list_t		blist;
	lf_fhandle_t    fhandle = 0;    /* XXX */
	histo_config_t *hconfig = (histo_config_t *) f_data;
	int             nhisto;
	float           min_simularity;
	HistoII        *ii = NULL;
	int             rv = 0;     /* return value */
	bbox_t *		cur_box;
	int				i;
	int				ii_alloc = 0, img_alloc = 0;
	off_t			len;


	lf_log(fhandle, LOGL_TRACE, "f_histo_detect: enter");

	/*
	 * get the img 
	 */
	err = lf_ref_attr(fhandle, ohandle, RGB_IMAGE, &len, (char**)&img);
	if (err != 0) {
		img_alloc = 1;
		img = get_rgb_img(ohandle);
	}
	ASSERT(img->type == IMAGE_PPM);

	err = lf_ref_attr(fhandle, ohandle, HISTO_II, &len, (char **)&ii);
	if (err != 0) {
		ii_alloc = 1;
		ii = histo_get_ii(hconfig, img);
	}

	/*
	 * get nhisto 
	 */
	bsize = sizeof(int);
	err = lf_read_attr(fhandle, ohandle, NUM_HISTO, &bsize, (char *) &nhisto);
	if (err) {
		nhisto = 0;             /* XXX */
	}


	/*
	 * scan the image
	 */
	write_notify_context_t context;
	context.fhandle = fhandle;
	context.ohandle = ohandle;

	TAILQ_INIT(&blist);
	pass = 	histo_scan_image(hconfig->name, img, ii, hconfig,
	                         hconfig->req_matches, &blist);

	i = nhisto;
	min_simularity = 2.0;
	while (!(TAILQ_EMPTY(&blist))) {
		search_param_t param;
		cur_box = TAILQ_FIRST(&blist);
		param.type = PARAM_HISTO;
		param.bbox.xmin = cur_box->min_x;
		param.bbox.ymin = cur_box->min_y;
		param.bbox.xsiz = cur_box->max_x - cur_box->min_x;
		param.bbox.ysiz = cur_box->max_y - cur_box->min_y;
		param.distance = cur_box->distance;
		if ((1.0 - cur_box->distance) < min_simularity) {
			min_simularity = 1.0 - cur_box->distance;
		}
		strncpy(param.name, hconfig->name, PARAM_NAME_MAX);
		param.name[PARAM_NAME_MAX] = '\0';
		param.id = i;
		write_notify_f(&context, &param);
		TAILQ_REMOVE(&blist, cur_box, link);
		free(cur_box);
		i++;
	}

	/*
	 * save some stats 
	 */
	nhisto += pass;
	err = lf_write_attr(fhandle, ohandle, NUM_HISTO, sizeof(int),
	                    (char *) &nhisto);
	ASSERT(!err);

	/*
	 * XXX ?? 
	 */
	if (min_simularity == 2.0) {
		rv = 0;
	} else {
		rv = (int)(100.0 * min_simularity);
	}

done:
	if (ii_alloc) {
		ft_free(fhandle, (char *) ii);
	}

	if (img_alloc) {
		ft_free(fhandle, (char *) img);
	}

	return rv;
}


int
f_init_hpass(int numarg, char **args, int blob_len, void *blob, void **data)
{
	hpass_data_t   *fstate;

	fstate = (hpass_data_t *) malloc(sizeof(*fstate));
	if (fstate == NULL) {
		/*
		 * XXX log 
		 */
		return (ENOMEM);
	}

	assert(numarg == 1);
	fstate->num_hist = atoi(args[0]);

	*data = fstate;
	return (0);
}


int
f_fini_hpass(void *data)
{
	hpass_data_t   *fstate = (hpass_data_t *) data;
	free(fstate);

	return (0);
}


int
f_eval_hpass(lf_obj_handle_t ohandle, int numout,
             lf_obj_handle_t * ohandles, void *f_data)
{
	int             nhisto;
	lf_fhandle_t    fhandle = 0;    /* XXX */
	int             err,
	pass;
	off_t           bsize;
	hpass_data_t   *fstate = (hpass_data_t *) f_data;
	;


	/*
	 * get nhisto 
	 */
	bsize = sizeof(int);
	err = lf_read_attr(fhandle, ohandle, NUM_HISTO, &bsize, (char *) &nhisto);
	ASSERT(!err);

	pass = (nhisto >= fstate->num_hist);

done:
	return pass;
}


int
f_init_hintegrate(int numarg, char **args, int blob_len, void *blob,
                  void **data)
{
	hintegrate_data_t *fstate;

	fstate = (hintegrate_data_t *) malloc(sizeof(*fstate));
	if (fstate == NULL) {
		/*
		 * XXX log 
		 */
		assert(0);
		return (ENOMEM);
	}

	/*
	 * read args 
	 */
	assert(numarg == 2);
	fstate->scale = atoi(args[0]);
	fstate->type = atoi(args[1]);
	// printf("fstate !!! %p \n", *data);
	*data = fstate;
	return (0);
}

int
f_fini_hintegrate(void *data)
{
	hintegrate_data_t *fstate = (hintegrate_data_t *) data;
	free(fstate);
	return (0);
}



int
f_eval_hintegrate(lf_obj_handle_t ihandle, int numout,
                  lf_obj_handle_t * ohandles, void *f_data)
{
	int             pass = 1;
	RGBImage       *img = NULL;
	HistoII        *ii = NULL;
	int             err;
	lf_fhandle_t    fhandle = 0;    /* XXX */
	size_t          nbytes;
	hintegrate_data_t *fstate = (hintegrate_data_t *) f_data;
	int             width,
	height;
	int             scalebits;
	int				img_alloc = 0;
	off_t			len;

	assert(f_data != NULL);
	// printf("f_data: %p \n", f_data);
	lf_log(fhandle, LOGL_TRACE, "f_hintegrate: start");


	/*
	 * get the img 
	 */
	err = lf_ref_attr(fhandle, ihandle, RGB_IMAGE, &len, (char**)&img);
	if (err != 0) {
		img_alloc = 1;
		img = get_rgb_img(ihandle);
	}

	ASSERT(img);
	ASSERT(img->type == IMAGE_PPM);

	/*
	 * initialize a new ii 
	 */
	scalebits = log2_int(fstate->scale);
	width = (img->width >> scalebits) + 1;
	height = (img->height >> scalebits) + 1;
	nbytes = width * height * sizeof(Histo) + sizeof(HistoII);


	err = lf_alloc_buffer(fhandle, nbytes, (char **) &ii);
	ASSERT(!err);
	ASSERT(ii);
	ii->nbytes = nbytes;
	ii->width = width;
	ii->height = height;
	ii->scalebits = scalebits;

	histo_compute_ii(img, ii, fstate->scale, fstate->scale, fstate->type);


	err = lf_write_attr(fhandle, ohandles[0], HISTO_II, ii->nbytes,
	                    (char *) ii);
	ASSERT(!err);
done:
	if (img_alloc) {
		lf_free_buffer(fhandle, (char *) img);
	}
	if (ii)
		lf_free_buffer(fhandle, (char *) ii);
	lf_log(fhandle, LOGL_TRACE, "f_hintegrate: done");
	return pass;
}


