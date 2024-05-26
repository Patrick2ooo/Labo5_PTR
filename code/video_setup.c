#include <stdio.h>

#include <alchemy/task.h>

#include "video_setup.h"
#include "video_utils.h"
#include "image.h"
#include "convolution.h"
#include "grayscale.h"
#define SIZEARRAY 1000

void video_task(void *cookie)
{
	Priv_video_args_t *priv = (Priv_video_args_t *)cookie;
	uint64_t period_in_ns = S_IN_NS / FRAMERATE;

	struct img_1D_t img, greyscale_img;
	
	img.data = priv->buffer;
	greyscale_img.data = priv->greyscale_buffer;
	
	img.width, greyscale_img.width, priv->result_conv.width  = WIDTH;
	img.height, greyscale_img.height, priv->result_conv.height  = HEIGHT;
	img.components, greyscale_img.components, priv->result_conv.components = BYTES_PER_PIXEL;

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
				rgba_to_grayscale8(&img, priv->greyscale_buffer);

				convolution_grayscale(priv->greyscale_buffer,
						      priv->convolution_buffer, WIDTH,
						      HEIGHT);

				grayscale_to_rgba(priv->convolution_buffer,
						  &priv->result_conv);

				memcpy(get_video_buffer(), priv->result_conv.data,
				       WIDTH * HEIGHT * BYTES_PER_PIXEL);
			} else {
				memcpy(get_video_buffer(), priv->buffer,
				       WIDTH * HEIGHT * BYTES_PER_PIXEL);
			}

			rt_task_wait_period(NULL);

		}

		
	}
	// Clear the video buffer and black screen
	memset(get_video_buffer(), 0, WIDTH * HEIGHT * BYTES_PER_PIXEL);
	rt_printf("Terminating video task.\n");
}
