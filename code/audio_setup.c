#include "audio_setup.h"
#include <math.h>
#include <complex.h>
#include <fft_utils.h>




typedef double complex cplx;

void log_task(void *cookie)
{
    /* TODO : afficher à la console la fréquence ayant la puissance maximale
     * et le temps nécessaire pour calculer cette valeur.
     * Exemple d'affichage : Dominant frequency : 64Hz, computation time : 800ms */
    int err = 0;
    double *log;
    Priv_audio_args_t *priv = (Priv_audio_args_t *)cookie;
    
    while(priv->ctl->running)
    {
            if(rt_queue_receive(&priv->processing_queue, (void**) &log, TM_INFINITE) < 0)
            {
                rt_printf("Could not receive data from the log queue\n");
                break;
            }
            rt_printf("Dominant frequency : %f Hz, Computation time : %f ms\n", log[0], log[1]);
            err = rt_queue_free(&priv->processing_queue, log);
            if(err != 0){
                rt_printf("Could not free the log queue\n");
                if(err == -EINVAL){
                    rt_printf("The queue is not valid\n");
                }
                break;
            }
    }
}

void acquisition_task(void *cookie)
{
    /* TODO : reprendre et adapter le code d'exemple ci-dessous pour
     * effectuer une acquisition d'un certain nombre d'échantillons puis les
     * envoyer à la tâche *processing_task*.
     *
     * Vous allez effectuer un échantillonage d'un nombre d'éléments égal à
     * FFT_BINS. C'est sur cette ensemble entier que la tâche processing doit
     * appliquer son traitement.
     */
    Priv_audio_args_t *priv = (Priv_audio_args_t *)cookie;
    int err;
    data_t* msg;
    // Calculate the minimum frequency of the task
    uint32_t audio_task_freq = (SAMPLING * sizeof(data_t)) / (FIFO_SIZE);   //added a multiplication by 2 to take into account the stereo signal
    // Calculate the period of the task with a margin to ensure not any data is lost
    uint64_t period_in_ns = S_IN_NS / (audio_task_freq + PERIOD_MARGIN);
    if((err = rt_queue_create(&priv->acquisition_queue, "acquisition_queue", 1000 * FIFO_SIZE * NB_CHAN, Q_UNLIMITED, Q_FIFO)) != 0){ //on doit avoir plus que 1 message ou 1 buffer plus grand sinon logiquement on peux plus send parce que la queue est occupée
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
                break;
            }
            // Get the data from the buffer
            ssize_t read = read_samples(msg, FIFO_SIZE * NB_CHAN);
            if (read)
            {
                // Write the data to the audio output
                err = rt_queue_send(&priv->acquisition_queue, msg, read, Q_NORMAL); //mettre en Q_NORMAL
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
                    break;
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

    /* TODO : compléter la tâche processing de telle sorte qu'elle recoive les
     * échantillons de la tâche acquisition. Une fois reçu les échantillons,
     * appliquer une FFT à l'aide de la fonction fft fournie, puis trouver
     * la fréquence à laquelle la puissance est maximale.
     * Enfin, envoyer à la tâche *log* le résultat, ainsi
     * que le temps qui a été nécessaire pour effectuer le calcul.
     *
     * Notez bien que le codec audio échantillone à 48 Khz.
     * Notez aussi que le driver audio renvoie des échantillons stéréo interlacés.
     * Vous n'effectuerez une FFT que sur un seul canal. En conséquence, prenez un
     * échantillon sur deux. */

    cplx *out = (cplx*)malloc(sizeof(cplx) * FFT_BINS); // Auxiliary array for fft function
    cplx *buf = (cplx*)malloc(sizeof(cplx) * FFT_BINS);
    double *power = (double*)malloc(sizeof(double) * FFT_BINS);
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
            //rt_printf("Size = %zd\n", (size_t)size);
            if((bytes_received = rt_queue_receive(&priv->acquisition_queue,(void **) &msg, TM_INFINITE)) <= 0){ 
                rt_printf("Could not receive data from the audio queue\n");
                break;
            }
            //rt_printf("data received\n"); //ok
            memcpy(priv->samples_buf + size, msg, FIFO_SIZE * NB_CHAN);
            size += bytes_received;
            if((err = rt_queue_free(&priv->acquisition_queue, msg)) != 0){  
                rt_printf("Could not free the audio queue\n");
                if(err == -EINVAL){
                    rt_printf("The queue is not valid\n");
                }
                break;
            }
            bytes_received = 0;
            if(size >= (2 * FFT_BINS)){
                size = 0;
                //rt_printf("Processing data\n");
                last_time = rt_timer_read();
                /* EXAMPLE using the fft function : */
                for (size_t i = 0; i < FFT_BINS; i++) 
                {
                    buf[i] = priv->samples_buf[i * 2] + 0 * I;  // Only take left channel !!!
                }
                fft(buf, out, FFT_BINS);
                for (size_t i = 1; i < (FFT_BINS / 2); i++)         // Skip the first element   
                {   
                    double re = crealf(buf[i]);
                    double im = cimagf(buf[i]);
                    power[i] = re * re + im * im;
                    if (power[i] > max_power)
                    {
                       max_power = power[i];
                       max_index = i;
                    }
                }
                double *log = rt_queue_alloc(&priv->processing_queue, 2 * sizeof(double));
                if(log == NULL)
                {
                    rt_printf("Could not allocate memory for the frequency\n");
                    break;
                }
                // Calculate the frequency
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
    //free(out);
    //free(buf);
    //free(power);
    rt_printf("Terminating processing task\n");
}
