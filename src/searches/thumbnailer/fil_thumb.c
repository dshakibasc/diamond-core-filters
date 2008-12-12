/*
 *  SnapFind
 *  An interactive image search application
 *  Version 1
 *
 *  Copyright (c) 2002-2005 Intel Corporation
 *  Copyright (c) 2006 Larry Huston <larry@thehustons.net>
 *  Copyright (c) 2008 Carnegie Mellon University
 *  All Rights Reserved.
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */

/*
 * Generate a thumbnail preview for an rgb image
 */
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "snapfind_consts.h"
#include "lib_results.h"
#include "lib_filter.h"
#include "lib_sfimage.h"
#include "rgb.h"


#define ASSERT(exp)							\
if(!(exp)) {								\
  lf_log(LOGL_ERR, "Assertion %s failed at ", #exp);		\
  lf_log(LOGL_ERR, "%s, line %d.", __FILE__, __LINE__);	\
  pass = -1;								\
  goto done;								\
}

struct filter_args {
    int width;
    int height;
};

int
f_init_thumbnailer(int numarg, char **args, int blob_len, void *blob,
		   const char *fname, void **data)
{
	struct filter_args *fargs;

	assert(numarg == 2);

	fargs = malloc(sizeof(*fargs));
	assert(fargs);

	fargs->width = atoi(args[0]);
	fargs->height = atoi(args[1]);

	*data = fargs;
	return 0;
}

int
f_fini_thumbnailer(void *data)
{
	free(data);
	return 0;
}

static double dmax(double a, double b)
{
    return (a > b) ? a : b;
}

/*
 * filter eval function to create a thumbnail attribute
 */
int
f_eval_thumbnailer(lf_obj_handle_t ohandle, void *data)
{
	struct filter_args *fargs = (struct filter_args *)data;
	RGBImage       *img = NULL, *scaledimg = NULL;
	size_t		img_size = 0;
	int		err = 0;
	double		scale = 1.0;
	int		pass = 0;

	lf_log(LOGL_TRACE, "f_thumbnailer: enter");

	/* read RGB image data */
	err = lf_read_attr(ohandle, RGB_IMAGE, &img_size, NULL);
	if (err && err != ENOMEM) {
		lf_log(LOGL_ERR, "f_thumbnailer: failed to read img attribute");
		return 0;
	}

	img = (RGBImage *)malloc(img_size);
	ASSERT(img);

	err = lf_read_attr(ohandle, RGB_IMAGE, &img_size, (unsigned char *)img);
	ASSERT(!err);

	/* compute scale factor */
	scale = dmax(scale, (double)img->width / fargs->width);
	scale = dmax(scale, (double)img->height / fargs->height);

	scaledimg = image_gen_image_scale(img, (int)scale);

	/* save thumbnail as an attribute */
	err = lf_write_attr(ohandle, THUMBNAIL_ATTR, scaledimg->nbytes,
			    (unsigned char *)scaledimg);
	ASSERT(!err);
	pass = 1;
done:
	if (img) free(img);
	if (scaledimg) free(scaledimg);
	lf_log(LOGL_TRACE, "f_thumbnailer: done");
	return pass;
}
