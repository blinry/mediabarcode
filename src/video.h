#ifndef INCLUDE_video_h__
#define INCLUDE_video_h__

#include "common.h"

typedef struct video video;

video* video_init(char *filename, int width);

column* video_get_column(video *v, double min_percent, double max_percent, nordlicht_style s);

void video_build_keyframe_index(video *v, int width);

void video_free(video *v);

#endif
