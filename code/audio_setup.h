/*****************************************************************************
 * Author: A.Gabriel Catel Torres
 *
 * Version: 1.0
 *
 * Date: 30/04/2024
 *
 * File: audio_setup.h
 *
 * Description: functions and defines used to launch Xenomai tasks to perform
 * the acquisition of the audio data coming from the microphone input and write
 * it to the audio output.
 *
 ******************************************************************************/

#ifndef AUDIO_SETUP_H
#define AUDIO_SETUP_H

#include <alchemy/task.h>
#include <stdbool.h>

#include "audio_utils.h"
#include "control.h"
#include <alchemy/queue.h>

#define AUDIO_ACK_TASK_PRIORITY 50
#define AUDIO_PROCESSING_TASK_PRIORITY 50
#define AUDIO_LOG_TASK_PRIORITY 50

#define AUDIO_FILENAME "output.wav"

// Audio defines
#define SAMPLING 48000.
#define FIFO_SIZE 256
#define NB_CHAN 2
#define PERIOD_MARGIN 25
#define FFT_BINS 8192

#define S_IN_NS 1000000000UL

typedef struct Priv_audio_args
{
    Ctl_data_t *ctl;
    RT_TASK acquisition_rt_task;
    RT_TASK processing_rt_task;
    RT_TASK log_rt_task;
    RT_QUEUE acquisition_queue;
    RT_QUEUE processing_queue;
    data_t *samples_buf;
} Priv_audio_args_t;

/**
 * \brief Read the data from the audio input and writes it to the
 * audio output.
 *
 * \param cookie pointer to private data. Can be anything
 */
void acquisition_task(void *cookie);

/**
 * \brief Process the audio data received from the acquisition task.
 *
 * \param cookie pointer to private data. Can be anything
 */
void processing_task(void *cookie);

/**
 * \brief Log the task execution time.
 *
 * \param cookie pointer to private data. Can be anything
 */
void log_task(void *cookie);

#endif // AUDIO_SETUP_H
