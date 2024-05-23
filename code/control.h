/*****************************************************************************
 * Author: A.Gabriel Catel Torres
 *
 * Version: 1.0
 *
 * Date: 30/04/2024
 *
 * File: control.h
 *
 * Description: structure declaration used to synchronize the Xenomai tasks
 *
 ******************************************************************************/

#ifndef CONTROL_H
#define CONTROL_H

#include <stdbool.h>
#include <alchemy/event.h>

typedef struct Ctl_data {
	bool running;
	bool video_running;
	bool convolution_running;
	bool greyscale_running;
	int run_audio;
	RT_EVENT *video_event;
} Ctl_data_t;

#endif