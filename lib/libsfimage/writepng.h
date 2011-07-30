/*
 *  SnapFind
 *  An interactive image search application
 *  Version 1
 *
 *  Copyright (c) 2011 Carnegie Mellon University
 *  All rights reserved
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */

#ifndef _WRITEPNG_H_
#define _WRITEPNG_H_

#include <stdlib.h>
#include "rgb.h"

#ifdef __cplusplus
extern "C"
{
#endif

void *convertRGBImagetoPNG(const RGBImage *img, size_t *len);

#ifdef __cplusplus
}
#endif

#endif
