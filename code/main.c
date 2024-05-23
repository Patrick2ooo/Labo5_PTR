/*****************************************************************************
 * Author: A.Gabriel Catel Torres
 *
 * Version: 1.0
 *
 * Date: 30/04/2024
 *
 * File: main.c
 *
 * Description: setup of audio, video and control tasks that will run in
 * parallel. This application is used to overload the CPU and analyse the
 * tasks execution time.
 *
 ******************************************************************************/

#include <unistd.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/mman.h>
#include <cobalt/stdio.h>

#include <alchemy/task.h>

#include "audio_utils.h"
#include "video_utils.h"
#include "io_utils.h"
#include "audio_setup.h"
#include "video_setup.h"
#include "control.h"

// IOCTL defines
#define KEY0		  0x0
#define SWITCH0		  0x1
#define SWITCH1		  (1 << 1)
#define SWITCH8		  (1 << 8)
#define SWITCH9		  (1 << 9)

// Control task defines
#define CTL_TASK_PERIOD	  100000000 // 10HZ
#define CTL_TASK_PRIORITY 50
#define END_VIDEO_EVENT	  0xf

void ioctl_ctl_task(void *cookie)
{
	Ctl_data_t *priv = (Ctl_data_t *)cookie;

	rt_task_set_periodic(NULL, TM_NOW, CTL_TASK_PERIOD);

	while (priv->running) {
		unsigned keys = read_key(MMAP);
		// Check if the key0 is pressed
		unsigned switches = read_switch(MMAP);

		if (switches & SWITCH0) {
			priv->run_audio = true;
		} else {
			priv->run_audio = false;
		}

		if (switches & SWITCH1) {
			rt_event_clear(priv->video_event, 0x2, NULL);
			priv->video_running = true;
			rt_event_signal(priv->video_event, VIDEO_EVENT);

			priv->greyscale_running = switches & SWITCH8 ? true :
								       false;
			priv->convolution_running = switches & SWITCH9 ? true :
									 false;

		} else {
			priv->video_running = false;
			rt_event_clear(priv->video_event, END_VIDEO_EVENT,
				       NULL);
		}

		if (keys | KEY0) {
			rt_printf("Key0 pressed, exiting the program\n");
			priv->running = false;
		}
		rt_task_wait_period(NULL);
	}
}

int main(int argc, char *argv[])
{
	int ret;

	printf("----------------------------------\n");
	printf("PTR24 - lab05\n");
	printf("----------------------------------\n");

	mlockall(MCL_CURRENT | MCL_FUTURE);

	// Ioctl setup
	if (init_ioctl()) {
		perror("Could not init IOCTL...\n");
		exit(EXIT_FAILURE);
	}

	// Init structure used to control the program flow
	Ctl_data_t ctl;
	ctl.running = true;

	RT_TASK ioctl_ctl_rt_task;
	// Create the IOCTL control task
	if (rt_task_spawn(&ioctl_ctl_rt_task, "program control task", 0,
			  CTL_TASK_PRIORITY, T_JOINABLE, ioctl_ctl_task,
			  &ctl) != 0) {
		perror("Error while starting acquisition_task");
		exit(EXIT_FAILURE);
	}
	printf("Launched IOCTL task\n");

	// Audio setup
	if (init_audio()) {
		perror("Could not init the audio...\n");
		exit(EXIT_FAILURE);
	}

	// Init private data used for the audio tasks
	Priv_audio_args_t priv_audio;
	priv_audio.samples_buf =
		(data_t *)malloc(2 * FFT_BINS * sizeof(data_t));
	priv_audio.ctl = &ctl;

	// Create the audio acquisition task
	if (rt_task_spawn(&priv_audio.acquisition_rt_task, "audio task", 0,
			  AUDIO_ACK_TASK_PRIORITY, T_JOINABLE, acquisition_task,
			  &priv_audio) != 0) {
		perror("Error while starting acquisition_task");
		exit(EXIT_FAILURE);
	}
	printf("Launched audio acquisition task\n");

	// Create the audio processing task
	if (rt_task_spawn(&priv_audio.processing_rt_task, "processing task", 0,
			  AUDIO_PROCESSING_TASK_PRIORITY, T_JOINABLE,
			  processing_task, &priv_audio) != 0) {
		perror("Error while starting acquisition_task");
		exit(EXIT_FAILURE);
	}
	printf("Launched audio processing task\n");

	// Create the audio log task
	if (rt_task_spawn(&priv_audio.log_rt_task, "log task", 0,
			  AUDIO_LOG_TASK_PRIORITY, T_JOINABLE, log_task,
			  &priv_audio) != 0) {
		perror("Error while starting acquisition_task");
		exit(EXIT_FAILURE);
	}
	printf("Launched audio log task\n");

	// Create the video event
	RT_EVENT video_event;

	if (rt_event_create(&video_event, "video_event", 0, EV_PRIO) != 0) {
		perror("Error while creating video event");
		exit(EXIT_FAILURE);
	}
	ctl.video_event = &video_event;

	// Video setup
	ret = init_video();
	if (ret < 0) {
		perror("Could not init the video...\n");
		return ret;
	}

	// Init private data used for the video tasks
	Priv_video_args_t priv_video;
	priv_video.buffer = (uint8_t *)malloc(HEIGHT * WIDTH * BYTES_PER_PIXEL);
	if (!priv_video.buffer) {
		perror("Error: Couldn't allocate memory for video buffer.\n");
		exit(EXIT_FAILURE);
	}
	priv_video.greyscale_buffer =
		(uint8_t *)malloc(HEIGHT * WIDTH * BYTES_PER_PIXEL);
	if (!priv_video.greyscale_buffer) {
		perror("Error: Couldn't allocate memory for greyscale buffer.\n");
		exit(EXIT_FAILURE);
	}
	priv_video.convolution_buffer =
		(uint8_t *)malloc(HEIGHT * WIDTH * BYTES_PER_PIXEL);

	if (!priv_video.convolution_buffer) {
		perror("Error: Couldn't allocate memory for convolution buffer.\n");
		exit(EXIT_FAILURE);
	}
	priv_video.ctl = &ctl;
	ctl.video_running = true;

	// Open the video file
	priv_video.file = fopen(VIDEO_FILENAME, "rb");
	if (!priv_video.file) {
		perror("Error: Couldn't open raw video file.\n");
		exit(EXIT_FAILURE);
	}

	// Create the video acquisition task
	if (rt_task_spawn(&priv_video.video_rt_task, "video task", 0,
			  VIDEO_ACK_TASK_PRIORITY, T_JOINABLE, video_task,
			  &priv_video) != 0) {
		perror("Error while starting video_function");
		exit(EXIT_FAILURE);
	}

	printf("Launched video task\n");

	printf("----------------------------------\n");
	printf("Press KEY0 to exit the program\n");
	printf("----------------------------------\n");

	// Waiting for the end of the program (coming from ctrl + c)
	rt_task_join(&ioctl_ctl_rt_task);
	rt_task_join(&priv_audio.acquisition_rt_task);
	rt_task_join(&priv_audio.processing_rt_task);
	rt_task_join(&priv_audio.log_rt_task);
	rt_task_join(&priv_video.video_rt_task);
	// Free all resources of the video/audio/ioctl

	printf("All tasks have been joined\n");

	clear_ioctl();
	clear_audio();
	clear_video();
	printf("Cleared all resources\n");
	// Free everything else
	free(priv_audio.samples_buf);
	free(priv_video.buffer);
	free(priv_video.greyscale_buffer);
	free(priv_video.convolution_buffer);
    // Close the video file
    fclose(priv_video.file);
    
	munlockall();

	rt_printf("Application has correctly been terminated.\n");

	return EXIT_SUCCESS;
}
