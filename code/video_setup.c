#include <stdio.h>

#include <alchemy/task.h>

#include "video_setup.h"
#include "video_utils.h"
#include "image.h"
#include "convolution.h"
#include "grayscale.h"
#define SIZEARRAY 1000

/*TODO: clean the variables useless and add buffers in the private struct and do the mallocs in the main or use rt_alloc
* And add gotoes in the main to free the buffers
*/
void video_task(void *cookie)
{
	//------------------------------
	// Charactérisation de la tâche
	RTIME now, previous;
	RTIME diff[SIZEARRAY];
	int count = 0;
	int end = 0;
	previous = rt_timer_read();
	//------------------------------

	Priv_video_args_t *priv = (Priv_video_args_t *)cookie;
	uint64_t period_in_ns = S_IN_NS / FRAMERATE;

	struct img_1D_t img, greyscale_img, greyscale_img_for_conv, rgba_result;

	img.data = priv->buffer;
	img.width = WIDTH;
	img.height = HEIGHT;
	img.components = BYTES_PER_PIXEL;

	greyscale_img.data = priv->greyscale_buffer;
	greyscale_img.width = WIDTH;
	greyscale_img.height = HEIGHT;
	greyscale_img.components = 4; // Niveaux de gris
	uint8_t *convolution_buffer =
		(uint8_t *)malloc(WIDTH * HEIGHT * sizeof(uint8_t));
	uint8_t *greyscale_buffer =
		(uint8_t *)malloc(WIDTH * HEIGHT * sizeof(uint8_t));

	rgba_result.data = (uint8_t *)malloc(WIDTH * HEIGHT * BYTES_PER_PIXEL);
	rgba_result.width = WIDTH;
	rgba_result.height = HEIGHT;
	rgba_result.components = 4; // Niveaux de gris

	int indexFrame = 0;

	rt_task_set_periodic(NULL, TM_NOW, period_in_ns);

	// Loop that reads a file with raw data
	while (priv->ctl->running) {
		rt_event_wait(priv->ctl->video_event, VIDEO_EVENT, NULL, EV_ALL,
			      TM_INFINITE);
		if (!priv->ctl->running) {
			break;
		}
		rt_printf("Video task on.\n");

		fseek(priv->file, indexFrame * WIDTH * HEIGHT * BYTES_PER_PIXEL,
		      SEEK_SET);
		// Read each frame from the file
		for (; indexFrame < NB_FRAMES; indexFrame++) {
			now = rt_timer_read();
			if (!priv->ctl->running || !priv->ctl->video_running) {
				break;
			}

			// Copy the data from the file to a buffer
			fread(priv->buffer, WIDTH * HEIGHT * BYTES_PER_PIXEL, 1,
			      priv->file);

			if (priv->ctl->greyscale_running &&
			    !priv->ctl->convolution_running) {
				rgba_to_grayscale32(&img, &greyscale_img);
				memcpy(get_video_buffer(),
				       priv->greyscale_buffer,
				       WIDTH * HEIGHT * BYTES_PER_PIXEL);
			} else if (priv->ctl->convolution_running &&
				   !priv->ctl->greyscale_running) {
				rgba_to_grayscale8(&img, greyscale_buffer);

				convolution_grayscale(greyscale_buffer,
						      convolution_buffer, WIDTH,
						      HEIGHT);

				grayscale_to_rgba(convolution_buffer,
						  &rgba_result);

				memcpy(get_video_buffer(), rgba_result.data,
				       WIDTH * HEIGHT * BYTES_PER_PIXEL);
			} else {
				memcpy(get_video_buffer(), priv->buffer,
				       WIDTH * HEIGHT * BYTES_PER_PIXEL);
			}

			rt_task_wait_period(NULL);

			//------------------------------
			//Characterisation de la tache
			if (count < SIZEARRAY) {
				diff[count] = now - previous;
				previous = now;
				count += 1;
				if (count % 100 == 0)
					rt_printf("Frame %d\n", count);
			} else {
				rt_printf("Fin de calcul\n");
				end = 1;
				break;
			}
			//------------------------------
		}

		if (end) {
			break;
		}
		if (indexFrame >= NB_FRAMES) {
			indexFrame = 0;
		}
	}
	//------------------------------
	//Characterisation de la tache
	rt_printf("Time taken by video\n");
	for (int i = 0; i < count; i++) {
		if (i % 100 == 0) {
			rt_printf("Frame %d\n", i);
		}

		rt_printf("%lld\n", diff[i]);
	}
	//------------------------------
	free(convolution_buffer);
	free(greyscale_buffer);
	free(rgba_result.data);
	priv->ctl->running = false;

	// Clear the video buffer and black screen
	memset(get_video_buffer(), 0, WIDTH * HEIGHT * BYTES_PER_PIXEL);
	rt_printf("Terminating video task.\n");
}
