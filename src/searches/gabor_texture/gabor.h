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

#ifndef	_GABOR_H_
#define	_GABOR_H_	1

#include "gabor_filter.h"

class gabor {
public:
	gabor(int angles, int radius, int freq, float max_freq, 
		float min_freq, float sigma);
	~gabor(void);

                                                                                
	int get_responses(RGBImage* image, int x, int y, int size, float *rvec);

private:
	int		gab_angles;		
	int		gab_radius;
	int		gab_freq;
	float		gab_sigma;
	float		gab_max_freq;
	float		gab_min_freq;
	gabor_filter **	gab_filters;
};


#endif	/* !_GABOR_H_ */