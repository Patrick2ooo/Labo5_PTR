#include "audio_setup.h"
#include <math.h>

typedef double complex cplx;

void log_task(void *cookie)
{
    int err = 0;
    double *log;
    Priv_audio_args_t *priv = (Priv_audio_args_t *)cookie;
    
    while(priv->ctl->running)
    {
            if(rt_queue_receive(&priv->processing_queue, (void**) &log, TM_INFINITE) < 0)
            {
                rt_printf("Could not receive data from the log queue\n");
                continue;
            }
            rt_printf("Dominant frequency : %f Hz, Computation time : %f ms\n", log[0], log[1]);
            err = rt_queue_free(&priv->processing_queue, log);
            if(err != 0){
                rt_printf("Could not free the log queue\n");
                if(err == -EINVAL){
                    rt_printf("The queue is not valid\n");
                }
                continue;
            }

    }
    rt_printf("Terminating log task\n");
}

void acquisition_task(void *cookie)
{
    Priv_audio_args_t *priv = (Priv_audio_args_t *)cookie;
    int err;
    data_t* msg;
    // Calculate the minimum frequency of the task
    uint32_t audio_task_freq = (SAMPLING * sizeof(data_t)) / (FIFO_SIZE);   //375Hz
    // Calculate the period of the task with a margin to ensure not any data is lost
    uint64_t period_in_ns = S_IN_NS / (audio_task_freq + PERIOD_MARGIN); //2.5ms

    if((err = rt_queue_create(&priv->acquisition_queue, "acquisition_queue", 1000 * FIFO_SIZE * NB_CHAN, Q_UNLIMITED, Q_FIFO)) != 0){
        rt_printf("Could not create the audio queue\n");
        return;
    }
    if (rt_task_set_periodic(NULL, TM_NOW, period_in_ns) < 0)
    {
        rt_printf("Could not set period of acquisition task\n");
        return;
    }
    while (priv->ctl->running){
        if(priv->ctl->run_audio){
            msg = rt_queue_alloc(&priv->acquisition_queue, FIFO_SIZE * NB_CHAN);
            if(msg == NULL)
            {
                rt_printf("Could not allocate memory for the audio queue\n");
                continue;
            }
            // Get the data from the buffer
            ssize_t read = read_samples(msg, FIFO_SIZE * NB_CHAN);
            if (read)
            {
                // send the data to the audio queue
                err = rt_queue_send(&priv->acquisition_queue, msg, read, Q_NORMAL);
                if(err < 0) 
                {           
                    rt_queue_free(&priv->acquisition_queue, msg);
                    rt_printf("Could not send data to the audio queue\n");
                    if(err == -ENOMEM){
                        rt_printf("The queue is full\n");
                    }
                    if(err == -EIDRM){
                        rt_printf("The queue has been deleted\n");
                    }
                    if(err == -EINVAL){
                        rt_printf("The queue is not valid\n");
                    }  
                    continue;
                }
            }
        }
        if(rt_task_wait_period(NULL)){
            rt_printf("Could not wait for the period of the acquisition task\n");
        }
    }
    rt_queue_free(&priv->acquisition_queue, msg);
    rt_queue_delete(&priv->acquisition_queue);
    rt_printf("Terminating acquisition task\n");
}

void processing_task(void *cookie)
{
    Priv_audio_args_t *priv = (Priv_audio_args_t *)cookie;
    RTIME last_time, current_time;
    data_t* msg;
    int err;
    double max_power = 0;
    size_t max_index = 0;
    rt_printf("Processing task\n");
    if(rt_queue_create(&priv->processing_queue, "log_queue", 2 * sizeof(double), Q_UNLIMITED, Q_FIFO) != 0)
    {
        rt_printf("Could not create the audio queue\n");
        return;
    }
    ssize_t size = 0;
    ssize_t bytes_received = 0;

    while(priv->ctl->running){
            if((bytes_received = rt_queue_receive(&priv->acquisition_queue,(void **) &msg, TM_INFINITE)) <= 0){ 
                rt_printf("Could not receive data from the audio queue\n");
                continue;
            }

            memcpy(priv->samples_buf + size, msg, FIFO_SIZE * NB_CHAN);
            size += bytes_received;
            if((err = rt_queue_free(&priv->acquisition_queue, msg)) != 0){  
                rt_printf("Could not free the audio queue\n");
                if(err == -EINVAL){
                    rt_printf("The queue is not valid\n");
                }
                continue;
            }
            bytes_received = 0;
            if(size >= (2 * FFT_BINS)){
                size = 0;
                last_time = rt_timer_read();
                /* EXAMPLE using the fft function : */
                for (size_t i = 0; i < FFT_BINS; i++) 
                {
                    priv->buf[i] = priv->samples_buf[i * 2] + 0 * I;  // Only take left channel !!!
                }
                fft(priv->buf, priv->out, FFT_BINS);
                for (size_t i = 1; i < (FFT_BINS / 2); i++)     // Skip the first element   
                {   
                    double re = crealf(priv->buf[i]);
                    double im = cimagf(priv->buf[i]);
                    priv->power[i] = re * re + im * im;
                    if (priv->power[i] > max_power)
                    {
                       max_power = priv->power[i];
                       max_index = i;
                    }
                }
                double *log = rt_queue_alloc(&priv->processing_queue, 2 * sizeof(double));
                if(log == NULL)
                {
                    rt_printf("Could not allocate memory for the frequency\n");
                    continue;
                }
                // Calculate the frequency and time to execute the fft
                log[0] = ((double)max_index * (double)SAMPLING) / (double)FFT_BINS;
                current_time = rt_timer_read();
                log[1] = (double)(current_time - last_time)/(double)1000000;           
                rt_queue_send(&priv->processing_queue, log, 2 * sizeof(double), Q_NORMAL);
                max_power = 0;
                max_index = 0;
            }
    }
    rt_queue_free(&priv->processing_queue, log);
    rt_queue_delete(&priv->processing_queue);
    rt_printf("Terminating processing task\n");
}
