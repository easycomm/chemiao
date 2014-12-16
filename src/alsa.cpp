#include "alsa.h"
extern snd_pcm_t *pcm_handle;
extern int period_frames;
//static snd_pcm_sw_params_t *swparams=NULL; 

int static pcm_handle_accessble(snd_pcm_t *handle)
{
	printf("period_frames=%d\n",period_frames);
	char test[period_frames*2];
	int ret = snd_pcm_writei(handle,test,period_frames);
	if(ret == -EPIPE)return 0;
	else if(ret == -ESTRPIPE)return 0;
	else return -1;
	
}

int alsa_init(int inchannels, int codecrate, int flag)
{
	int ret=0;
	unsigned int rate =codecrate;// codecrate/inchannels;
	unsigned int real_rate = rate;
	fprintf(stderr,"In alsa_init:inchannels=%d\tcodecrate=%d\tflag=%d\n",inchannels,codecrate,flag);
	snd_pcm_hw_params_t *hw_params;
	snd_pcm_uframes_t  buffer_size = 98304,buf_size = 0;
	snd_pcm_uframes_t  period_size = 98304/4;
	snd_pcm_uframes_t  start_threshold, stop_threshold;
	snd_pcm_sw_params_t *swparams=NULL;
	static unsigned int buffer_time = 500000;               /* ring buffer length in us     */
	static unsigned int period_time = 100000;

//	if(pcm_handle != NULL){
//	printf("pcm_handle != NULL\n");
/*	if(pcm_handle_accessble(pcm_handle) == -1){  */
//	printf("pcm_handle_accessble return -1\n");
//	ret = snd_pcm_close(pcm_handle);
//	if(ret<0){printf("pcm_handle close failed\n");return -1;}
//	ret = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);  
 //   	if (ret < 0) {  
//        fprintf(stderr,"snd_pcm_open error\n");  
//        return -1;  
//    	}  
//	}   
//	}else{
//		printf("pcm_handle == NULL\n");
//	    ret = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);  
// 	    if (ret < 0) {  
// 	    fprintf(stderr,"snd_pcm_open error\n");  
// 	    return -1; 
//	}
//	}
	if(pcm_handle == NULL){
		ret = snd_pcm_open(&pcm_handle, "default", SND_PCM_STREAM_PLAYBACK, 0);  
 	        if (ret < 0) {  
 	        fprintf(stderr,"snd_pcm_open error\n");  
 	        return -1; 
	}
	}
	ret = snd_pcm_sw_params_malloc(&swparams);
	if(ret < 0){
	fprintf(stderr,"snd_pcm_sw_params_alloca error\n");
	return -1;
	}
	ret = snd_pcm_hw_params_malloc(&hw_params);  
    	if (ret < 0) {  
        perror("snd_pcm_hw_params_malloc");  
        return -1;  
    	}  
	ret = snd_pcm_hw_params_any(pcm_handle, hw_params);  
    	if (ret < 0) {  
        perror("snd_pcm_hw_params_any");  
        return -1;  
    }	  

	 ret = snd_pcm_hw_params_set_access(pcm_handle, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);  
    if (ret < 0) {  
        perror("snd_pcm_hw_params_set_access");  
        return -1;  
    }  
	 ret = snd_pcm_hw_params_set_format(pcm_handle, hw_params, SND_PCM_FORMAT_S16_LE);  
    if (ret < 0) {  
        perror("snd_pcm_hw_params_set_format");  
        return -1;
    }  
	if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &real_rate, 0) < 0){
                fprintf(stderr, "Error setting rate. \n");
                return(-1);
        }

        if (rate != real_rate){
                fprintf(stderr, "The rate %d Hz is not supported by your hardware. \n==> Using %d Hz instead. \n", rate, real_rate);
        }
	ret = snd_pcm_hw_params_set_channels(pcm_handle, hw_params, inchannels);  
    if (ret < 0) {  
        perror("snd_pcm_hw_params_set_channels");  
        return -1;  
    }  
	int dir=0;
	if(flag == 1){
  
    ret = snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params, &period_size, 0);  
    if (ret < 0)   
    {  
        printf("Unable to set period size\n");  
	return -1;
    }  
	ret = snd_pcm_hw_params_set_buffer_size_near(pcm_handle, hw_params, &buffer_size);  
    if (ret < 0)   
    {  
         printf("Unable to set buffer size\n");  
	return -1;
           
    }  
	
}
                                   

	if(flag == 2){
	ret = snd_pcm_hw_params_get_buffer_time_max(hw_params,&buffer_time,0);
	period_time = buffer_time / 16;
	ret = snd_pcm_hw_params_set_period_time_near(pcm_handle, hw_params, &period_time, &dir);
	if(ret<0)
	{
	printf("set period time failed\n");return -1;
	}
	dir = 0;
	ret = snd_pcm_hw_params_set_buffer_time_near(pcm_handle,hw_params,&buffer_time,&dir);
	if(ret < 0)
	{
		printf("set buffer time failed\n");
		return -1;
	}  
	}

	ret = snd_pcm_hw_params(pcm_handle, hw_params);  
	if (ret < 0) {  
        fprintf(stderr,"snd_pcm_hw_params:%s\n",snd_strerror(ret));  
        return -1;  
    } 
 
	snd_pcm_uframes_t  frames;
	/* Use a buffer large enough to hold one period */  
	snd_pcm_hw_params_get_period_size(hw_params, &frames, &dir);
	period_frames = frames;
/*	ret = snd_pcm_sw_params_current(pcm_handle,swparams); 
	snd_pcm_sw_params_set_avail_min(pcm_handle,swparams,frames);
	ret = snd_pcm_hw_params_get_buffer_size(hw_params,&buf_size);
	if(ret < 0)
	{
		fprintf(stderr,"snd_pcm_hw_params_get_buffer_size error\n");
		return -1;
	}
	start_threshold = buf_size;
	ret = snd_pcm_sw_params_set_start_threshold(pcm_handle,swparams,start_threshold);
	if(ret < 0)
	{
		fprintf(stderr,"snd_pcm_sw_params_set_start_threshold error\n");
	}
	stop_threshold = buf_size;
	ret = snd_pcm_sw_params(pcm_handle,swparams); 
	if(ret < 0)
	{
		fprintf(stderr,"snd_pcm_sw_params error\n");
	}    */
	snd_pcm_hw_params_free(hw_params);
	return period_frames*2*inchannels;
}



