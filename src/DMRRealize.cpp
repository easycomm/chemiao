#include "PltDeviceData.h"
#include "PltService.h"
#include "PltUPnP.h"
#include "PltUtilities.h"
#include "Neptune.h"
#include "DMRRealize.h"
#include "PltService.h"
#include "ffmpeg.h"
#include "FFDecode.h"
#include "util.h"
#include "alsa.h"
#include "statemachine.h"
#include "DMRExtern.h"
#include <math.h>

AVDictionary *stream_opts = NULL;

extern NPT_String CurrentTrackURI;
//uint8_t **resample_data = NULL;
struct SwrContext *swr_ctx;

static int interrupt_cb(void *ctx)
{
	fprintf(stderr,"timeout timeout timeout\n");
	return -1;
} 


static int resample_init(int64_t src_ch_layout, int src_rate, enum AVSampleFormat src_sample_fmt)
{
	int ret;
	int dst_rate = src_rate;
	swr_ctx = swr_alloc();   /*XIA*/
        if (!swr_ctx) {
	fprintf(stderr, "Could not allocate resampler context\n");
	return -1;
     	}
	av_opt_set_int(swr_ctx, "in_channel_layout",src_ch_layout, 0);
	av_opt_set_int(swr_ctx, "in_sample_rate",src_rate, 0);
	av_opt_set_sample_fmt(swr_ctx, "in_sample_fmt", src_sample_fmt, 0);
	av_opt_set_int(swr_ctx, "out_channel_layout",audio_dec_ctx->channel_layout, 0);
	av_opt_set_int(swr_ctx, "out_sample_rate",dst_rate,0);
	av_opt_set_sample_fmt(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
	if ((ret = swr_init(swr_ctx)) < 0) {
		fprintf(stderr, "Failed to initialize the resampling context\n");
	
		return -1;
     }


}
static int resample_begin(uint8_t** src_data,/* uint8_t** dst_data,*/ int src_nb_samples, int src_rate)
{
	int max_dst_nb_samples,  dst_nb_samples;
	int dst_nb_channels = 0;
	int dst_rate = src_rate;
	int dst_linesize;
	int ret;
	int dst_bufsize;
	uint8_t ** dst_data;
	max_dst_nb_samples = dst_nb_samples = av_rescale_rnd(src_nb_samples, dst_rate, src_rate, AV_ROUND_UP);
	dst_nb_channels = av_get_channel_layout_nb_channels(audio_dec_ctx->channel_layout);
	ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,dst_nb_samples, AV_SAMPLE_FMT_S16, 0);
	if (ret < 0) {
	fprintf(stderr, "Could not allocate destination samples\n");
	return -1;
	}
	dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, src_rate) + src_nb_samples, dst_rate/*dst_rate*/, src_rate, AV_ROUND_UP);
	if (dst_nb_samples > max_dst_nb_samples){
	av_freep(&dst_data[0]);
	ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,dst_nb_samples, AV_SAMPLE_FMT_S16/*dst_sample_fmt*/, 1);
	if(ret < 0){
	fprintf(stderr,"av_samples_alloc failed\n");
	return -1;
	}
	max_dst_nb_samples = dst_nb_samples;
	}
	ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
	if(ret < 0){
	fprintf(stderr,"swr_convert failed\n");
	av_freep(dst_data);
	return -1;
	}
	dst_bufsize = av_samples_get_buffer_size(&dst_linesize, dst_nb_channels,ret, AV_SAMPLE_FMT_S16/*dst_sample_fmt*/, 1);
//	resample_data=(uint8_t*)malloc(dst_bufsize*1);
//	memcpy(resample_data,dst_data[0],dst_bufsize);
	if(dst_bufsize < 0){
	fprintf(stderr,"could not get dst_bufsize\n");
	av_freep(dst_data);
	return -1;
	}else{
		resample_data=(uint8_t*)malloc(dst_bufsize*1);
          	memcpy(resample_data,dst_data[0],dst_bufsize);
		av_freep(dst_data);
	}
	return dst_bufsize;
}
static int action_continue(NPT_String &s1, NPT_String &s2, struct timeval now){
	if(s1.Compare(s2.GetChars(),true) == 0){
		printf("same action\n");
		if((now.tv_sec*1000000+now.tv_usec-action_trigger_cond.last_action_trigger_time.tv_sec*1000000-action_trigger_cond.last_action_trigger_time.tv_usec) > 800000)
	{ printf("continue\n"); return 1;/*continue*/ }
		else 
			{printf("stopping\n");return 2;/*stop*/}
	}else{
		return 1;
	}
}
static int xrun_recovery(snd_pcm_t *handle, int err)
{
         if (err == -EPIPE) {    /* under-run */
                 err = snd_pcm_prepare(handle);
                 if (err < 0)
                         printf("Can't recovery from underrun, prepare failed: %s\n", snd_strerror(err));
                 return 0;
         } else if (err == -ESTRPIPE) {
                 while ((err = snd_pcm_resume(handle)) == -EAGAIN){
			fprintf(stderr,"err == -ESTRPIPE");
                         usleep(10000);       /* wait until the suspend flag is released     */
			}
                 if (err < 0) {
                         err = snd_pcm_prepare(handle);
                         if (err < 0)
                                 printf("Can't recovery from suspend, prepare failed    : %s\n", snd_strerror(err));
                 }
                 return 0;
         }
         return err;
 }
static int alsa_playback2(uint8_t **data, int data_size,int channels)
{
	
	int position = 0;
	int writesize = 0;
	int ret = 0;
	while(data_size + pcm_buffer_size >= alsa_write_byte)
	{
		printf("In while\n");
		
		writesize = alsa_write_byte - pcm_buffer_size;
		memcpy(pcm_buffer+pcm_buffer_size, frame.extended_data[0]+position,writesize);
		data_size -= writesize ;
		position += writesize;
		ret = snd_pcm_writei(pcm_handle,pcm_buffer,period_frames);
		if(ret < 0)
		{
		xrun_recovery(pcm_handle, ret);
		fprintf(stderr,"snd_pcm_writei error:%s\n",snd_strerror(ret));
		}
		if(ret>0 && ret<period_frames){
		pcm_buffer_size = 0;
		pcm_buffer_size = (period_frames - ret)*2*channels;
		memcpy(pcm_buffer,pcm_buffer+alsa_write_byte - pcm_buffer_size,pcm_buffer_size); // the size of pcm_buffer is alsa_write_byte.
		}else{
			pcm_buffer_size = 0;
		}
	
	}
	if(data_size + pcm_buffer_size < alsa_write_byte)
	{
		memcpy(pcm_buffer + pcm_buffer_size, frame.extended_data[0]+position, data_size);
		pcm_buffer_size += data_size;
		printf("pcm_buffer_size=%d\n",pcm_buffer_size);
	}
	
	return 0;
}

static int alsa_playback(int frames)
{
	int position = 0;
	int writesize = 0;
	int ret = 0;
	int resample_buffer_size = 0;
//	uint8_t **resample_data;
	printf("before resample_begin\n");
	resample_buffer_size = resample_begin(frame.extended_data,/* &resample_data,*/ frame.nb_samples, audio_dec_ctx->sample_rate);
	printf("after resample_begin\n");
	if(resample_buffer_size < 0){
	fprintf(stderr,"resample_begin error\n");
	return -1;
	}
	printf("resample_buffer_size=%d\n",resample_buffer_size);
	while(resample_buffer_size + pcm_buffer_size >= alsa_write_byte)
	{
		printf("In while\n");
		
		writesize = alsa_write_byte - pcm_buffer_size;
		memcpy(pcm_buffer+pcm_buffer_size, resample_data+position,writesize);
		resample_buffer_size -= writesize ;
		position += writesize;
//		ret = resample_begin(pcm_buffer, resample_data, frame.nb_samples, audio_dec_ctx->sample_rate);//resample_begin(uint8_t** src_data, uint8_t** dst_data, int src_nb_samples, int src_rate)
		printf("before snd_pcm_writei,frames=%d\n",frames);
		ret = snd_pcm_writei(pcm_handle,pcm_buffer,frames);
		printf("after snd_pcm_writei\n");
		if(ret < 0)
		{
		xrun_recovery(pcm_handle, ret);
		fprintf(stderr,"snd_pcm_writei error:%s\n",snd_strerror(ret));
		}
		else
		{
			fprintf(stderr,"snd_pcm_writei:ret=%d\n%s\n",ret,snd_strerror(ret));
		}
		if(ret>0 && ret<frames){
		pcm_buffer_size = 0;
		pcm_buffer_size = (frames - ret)*4;
		memcpy(pcm_buffer,pcm_buffer+alsa_write_byte - pcm_buffer_size,pcm_buffer_size); // the size of pcm_buffer is alsa_write_byte.
		}else{
			pcm_buffer_size = 0;
		}
	
	}
	if(resample_buffer_size + pcm_buffer_size < alsa_write_byte)
	{
		printf("resample_buffer_size=%d\tposition=%d\tpcm_buffer_size=%d\n",resample_buffer_size,position,pcm_buffer_size);
		memcpy((void*)(pcm_buffer + pcm_buffer_size), (void*)(resample_data+position), resample_buffer_size);
		pcm_buffer_size += resample_buffer_size;
		printf("pcm_buffer_size=%d\n",pcm_buffer_size);
	}
	if(resample_data){
	free(resample_data);
	resample_data = NULL;
	}
	return 0;
}

static int  handle_end(int fd)
{
	avcodec_close(audio_dec_ctx);
        avformat_close_input(&fmt_ctx);
        audio_stream = NULL;
	return 0;
		
}

static int playback_optimise(int channels, int samples, int format, uint8_t**data)
{
	fprintf(stderr,"In playback_optimise\n");
	uint8_t *audio_buffer;
	int16_t*paudio_buffer;
	int fd;
	int count = 0;
	float sample;
	float ** pdata = NULL;
	int c = 0;int position = 0;int writesize = 0;int ret = 0;int audio_buffer_size=channels*samples*2;
	audio_buffer = (uint8_t*)malloc(channels*samples*2);
	if(format == AV_SAMPLE_FMT_FLTP)/*remember: we cannot initialize these in switch-case, that will cause compile error*/
	{
		paudio_buffer = (int16_t*)audio_buffer;
		 pdata = (float**)data;

	}
	switch(format)
	{
		case AV_SAMPLE_FMT_S16P:
		fprintf(stderr,"AV_SAMPLE_FMT_S16P\n");
		if(channels == 2)
		{
			for(count=0; count<samples; count++)
			{
				for(c=0; c<2;c++)
				{
					memcpy(audio_buffer+count*4+c*2,&data[c][count*2],2);
				}
			}

	/*                 fd = open("/tmp/record",O_RDWR|O_APPEND);
 		         write(fd,audio_buffer,audio_buffer_size);
 	                close(fd);  */
			
		}
		else
		{
			memcpy(audio_buffer,data[0],samples*2);
		}
		break;
		case AV_SAMPLE_FMT_FLTP:
		fprintf(stderr,"AV_SAMPLE_FMT_FLTP\n");
		if(channels == 2)
		{
			for(count=0; count<samples; count++)
			{
				for(c=0; c<2;c++)
				{
					float sample = pdata[c][count];
					if (sample < -1.0f) sample = -1.0f;
					else if(sample > 1.0f)sample = 1.0f;
					paudio_buffer[count*2+c] = (int16_t)(sample * 32767.0f);	
				}
			}
		}
		if(channels == 1)
		{
			for(count = 0; count<samples; count++)
			{
				sample=pdata[0][count];
				if(sample < -1.0f)sample = -1.0f;
				else if(sample > 1.0f)sample = 1.0f;
				paudio_buffer[count] = (int16_t)(sample*32767.0f);
			}
		}
		break;	
		default:
		fprintf(stderr,"sample format not surpport!\n");
	}
	while(audio_buffer_size + pcm_buffer_size >= alsa_write_byte)
	{
		fprintf(stderr,"In while:audio_buffer_size=%d\tpcm_buffer_size=%d\talsa_write_byte=%d\tperiod_frames=%d\n",audio_buffer_size,pcm_buffer_size,alsa_write_byte,period_frames);
		writesize = alsa_write_byte - pcm_buffer_size;
		memcpy(pcm_buffer+pcm_buffer_size, audio_buffer+position,writesize);
		audio_buffer_size -= writesize ;
		position += writesize;
		fd = open("/tmp/record",O_RDWR|O_APPEND);
		write(fd,pcm_buffer,period_frames*2*channels);
		close(fd);
		ret = snd_pcm_writei(pcm_handle,pcm_buffer,period_frames);
		fprintf(stderr,"After snd_pcm_writei:ret=%d\n",ret);
		if(ret < 0)
		{
		xrun_recovery(pcm_handle, ret);
		fprintf(stderr,"snd_pcm_writei error:%s\n",snd_strerror(ret));
		}
		if(ret>0 && ret<period_frames){
		pcm_buffer_size = 0;
		pcm_buffer_size = (period_frames - ret)*2*channels;
		memcpy(pcm_buffer,pcm_buffer+alsa_write_byte - pcm_buffer_size,pcm_buffer_size); // the size of pcm_buffer is alsa_write_byte.
		}else{
			pcm_buffer_size = 0;
		}  
		
	
	}
	if(audio_buffer_size + pcm_buffer_size < alsa_write_byte)
	{
		printf("audio_buffer_size=%d\tposition=%d\tpcm_buffer_size=%d\n",audio_buffer_size,position,pcm_buffer_size);
		memcpy((void*)(pcm_buffer + pcm_buffer_size), (void*)(audio_buffer+position), audio_buffer_size);
		pcm_buffer_size += audio_buffer_size;
//		printf("pcm_buffer_size=%d\n",pcm_buffer_size);
	}
	if(audio_buffer){
	free(audio_buffer);
	audio_buffer = NULL;
	}
	return 0;
}
 static int play_one_frame(bool firsttime,int frames,PLT_Service* service)
{
	fprintf(stderr,"In play_one_frame\n");
	int unpadded_linesize;
	char playtime[20];
	int ret;
	int position = 0;
	if(firsttime)
	{
	seconds = lastseconds = 0.0;
	if(GetNextFrame(true/*firstTime*/,audio_stream_idx,/*AVFormatContext*/fmt_ctx, /*AVCodecContext*/audio_dec_ctx, &frame/*AVFrame*/,&pkt))
		{
                          unpadded_linesize = frame.nb_samples * av_get_bytes_per_sample((enum AVSampleFormat)frame.format);
		seconds += (float)frame.nb_samples / (float)frame.sample_rate;lastseconds = seconds;
//		alsa_playback(frames);
//		ret = playback_optimise(audio_dec_ctx->channels, frame.nb_samples,frame.format,frame.extended_data);
		alsa_playback2(frame.extended_data, frame.linesize[0],audio_dec_ctx->channels);
		}
	else
		{
    		return -1;
		}
	return 0;
	}
	if(GetNextFrame(false,audio_stream_idx,fmt_ctx,audio_dec_ctx,&frame,&pkt))
	{
//		unpadded_linesize = frame.nb_samples * av_get_bytes_per_sample((enum AVSampleFormat)frame.format);
	/*	 switch(frame.format)
                 {
                         case AV_SAMPLE_FMT_U8:
                         printf("sample_fmt:AV_SAMPLE_FMT_U8\n");
                         break;
                         case AV_SAMPLE_FMT_S16:
                         printf("sample_fmt:AV_SAMPLE_FMT_S16\n");
                         break;
                         case AV_SAMPLE_FMT_S32:
                         printf("sample_fmt:AV_SAMPLE_FMT_S32\n");
                         break;
                         case AV_SAMPLE_FMT_FLT:
                         printf("sample_fmt:AV_SAMPLE_FMT_FLT\n");
                         break;
                         case AV_SAMPLE_FMT_DBL:
                         printf("sample_fmt:AV_SAMPLE_FMT_DBL\n");
                         break;
                         case AV_SAMPLE_FMT_U8P:
                         printf("sample_fmt:AV_SAMPLE_FMT_U8P\n");
                         break;
                         case AV_SAMPLE_FMT_S16P:
                         printf("sample_fmt:AV_SAMPLE_FMT_S16P\n");
			 break;
			 case AV_SAMPLE_FMT_S32P:
                         printf("sample_fmt:AV_SAMPLE_FMT_S32P\n");
                         break;
                         case AV_SAMPLE_FMT_FLTP:
                         printf("sample_fmt:AV_SAMPLE_FMT_FLTP\n");
                         break;
                         case AV_SAMPLE_FMT_DBLP:
                         printf("sample_fmt:AV_SAMPLE_FMT_DBLP\n");
                         break;
                         case AV_SAMPLE_FMT_NB:
                         printf("sample_fmt:AV_SAMPLE_FMT_NB\n");
                         break;
                         default:
                         printf("sample_fmt:UNKNOW\n");
                 }  */


		seconds += (float)frame.nb_samples/(float)frame.sample_rate;
		if(seconds - lastseconds > 1)
		{
		seconds2timestring(playtime,20,seconds);
		printf("seconds-lastseconds>1\n");
		lastseconds = seconds;
		}  
//		alsa_playback(frames);
//		ret = playback_optimise(audio_dec_ctx->channels, frame.nb_samples,frame.format,frame.extended_data);
		alsa_playback2(frame.extended_data, frame.linesize[0],audio_dec_ctx->channels);
		fprintf(stderr,"return from playback_optimise\n");
	
		return 0;
	}
	else
	{ 
	return -1;
	}
	return 0;
}  
    NPT_Result PlayControl::OnGetCurrentConnectionInfo(PLT_ActionReference& action,PLT_Service* service)
{
	return NPT_SUCCESS;
}

    // AVTransport
    NPT_Result PlayControl::OnNext(PLT_ActionReference& action,PLT_Service* service)
	{
		return NPT_SUCCESS;
	}
    NPT_Result PlayControl::OnPause(PLT_ActionReference& action,PLT_Service* service)
	{
		printf("In OnPause\n");
		current_state = 3;
		state_machine(previous_state,current_state);
		extern_service->SetStateVariable("TransportState","PAUSED_PLAYBACK");
		return NPT_SUCCESS;
	}
    NPT_Result PlayControl::OnPlay(PLT_ActionReference& action,PLT_Service* service)
{
	
		NPT_String transportstate;
		int unpadded_linesize;
		int state;
		current_state = 0;
		printf("before state_machine\n");print_time();
		int ret = state_machine(previous_state,current_state);
		printf("after state_machine\n");print_time();
		if(ret == 1){ //play->play, this case we must return, only one play can run.
		//ret=1 also could means pause->play, this case we return.
		return NPT_SUCCESS;
		}
		if(ret == -1)//avformat_find_stream_info error occur, state_machine already done some clean task.
		{
		return NPT_FAILURE;
		}
	if (open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO) >= 0)
	{
		audio_stream = fmt_ctx->streams[audio_stream_idx];
         	audio_dec_ctx = audio_stream->codec;
		channels = audio_dec_ctx->channels;
		sample_rate = audio_dec_ctx->sample_rate;
		switch(audio_dec_ctx->sample_fmt)
		{
			case AV_SAMPLE_FMT_U8:
			printf("sample_fmt:AV_SAMPLE_FMT_U8\n");
			break;
			case AV_SAMPLE_FMT_S16:
			printf("sample_fmt:AV_SAMPLE_FMT_S16\n");
			break;
			case AV_SAMPLE_FMT_S32:
			printf("sample_fmt:AV_SAMPLE_FMT_S32\n");
			break;
			case AV_SAMPLE_FMT_FLT:
			printf("sample_fmt:AV_SAMPLE_FMT_FLT\n");
			break;
			case AV_SAMPLE_FMT_DBL:
			printf("sample_fmt:AV_SAMPLE_FMT_DBL\n");
			break;
			case AV_SAMPLE_FMT_U8P:
			printf("sample_fmt:AV_SAMPLE_FMT_U8P\n");
			break;
			case AV_SAMPLE_FMT_S16P:
			printf("sample_fmt:AV_SAMPLE_FMT_S16P\n");
			break;
			case AV_SAMPLE_FMT_S32P:
			printf("sample_fmt:AV_SAMPLE_FMT_S32P\n");
			break;
			case AV_SAMPLE_FMT_FLTP:
			printf("sample_fmt:AV_SAMPLE_FMT_FLTP\n");
			break;
			case AV_SAMPLE_FMT_DBLP:
			printf("sample_fmt:AV_SAMPLE_FMT_DBLP\n");
			break;
			case AV_SAMPLE_FMT_NB:
			printf("sample_fmt:AV_SAMPLE_FMT_NB\n");
			break;
			default:
			printf("sample_fmt:UNKNOW\n");
		}

		av_dump_format(fmt_ctx, 0, NULL, 0);
         	pkt.size = 0; pkt.data = NULL;
		if(pcm_handle != NULL){
			fprintf(stderr,"close pcm_handle\n");
			ret = snd_pcm_close(pcm_handle);
			if(ret < 0)fprintf(stderr,"close pcm_handle error\n");
			pcm_handle = NULL;
		}
		alsa_write_byte = alsa_init(audio_dec_ctx->channels,audio_dec_ctx->sample_rate,2);  /*buffer time set*/
//		alsa_write_byte = alsa_init(audio_dec_ctx->channels,audio_dec_ctx->sample_rate,1);  /*buffer period set*/
		

		if(alsa_write_byte <=  0){
		extern_service->SetStateVariable("TransportState","STOPPED");
		current_state = 1;

		stop_wait.value = 1;//don't have to wait in state_machine,because we already know we are going to exit.
		if(stop_wait.value == 1){
		printf("stop_wait.value=%d\n",stop_wait.value);
		state_machine(previous_state,current_state); //play->stop;
		}

		return NPT_FAILURE;
		}
		else 
		{	if(pcm_buffer != NULL){
				printf("Release pcm_buffer\n");free(pcm_buffer);
				pcm_buffer = NULL;
			}
			pcm_buffer = (uint8_t *)malloc(alsa_write_byte*sizeof(uint8_t));
//			frames = (alsa_write_byte / 2) / channels;
//			period_frames = alsa_write_byte / 2 / audio_dec_ctx->channels;
//			ret = resample_init(audio_dec_ctx->channel_layout/*int64_t src_ch_layout*/, audio_dec_ctx->sample_rate/*int src_rate*/, audio_dec_ctx->sample_fmt/*enum AVSampleFormat src_sample_fmt*/);	
/*			if(ret < 0){
				free(pcm_buffer);
				current_state = 1;stop_wait.value = 1;
				if(stop_wait.value == 1)state_machine(previous_state,current_state);
				return NPT_FAILURE;
			}  */
		}
		// if everything before before is OK, we now in playing status;
		extern_service->SetStateVariable("TransportState","PLAYING");
//		av_dict_set(&stream_opts, "timeout", "360000", 0);//mseconds
		int result = play_one_frame(true,period_frames,extern_service);
		while(!result)
		{
		//playback...
			result = play_one_frame(false,period_frames,extern_service);
			fprintf(stderr,"return from play_one_frame\n");
			if(DMRpause)
			{
			printf("In DMRpause\n");
			pthread_mutex_lock(&play_pause_play.mutex);
			while(play_pause_play.value == 0){
			pthread_cond_wait(&play_pause_play.cond,&play_pause_play.mutex);
			}
			pthread_mutex_unlock(&play_pause_play.mutex);
			play_pause_play.value = 0;
			DMRpause = false;
			printf("pause wake up\n");
			if(DMRpause2stop)
			{
			printf("IN DMRpause2stop\n");
			DMRpause2stop = false;
			extern_service->SetStateVariable("TransportState","STOPPED");
			return NPT_SUCCESS;
			}
			}
			if(DMRseek)  //this occurs in play->seek case, pause->seek we directly deal with is in OnSeek function.
			{
			printf("In DMRseek\n");
			usleep(5000);
			pthread_mutex_lock(&seek_wait.mutex);
			seek_wait.value = 1;
			pthread_cond_signal(&seek_wait.cond);
			pthread_mutex_unlock(&seek_wait.mutex);
			printf("play_seek_play sleep\n");
			pthread_mutex_lock(&play_seek_play.mutex);
			while(play_seek_play.value == 0){
			pthread_cond_wait(&play_seek_play.cond,&play_seek_play.mutex);
			DMRseek = false;
			printf("play_seek_play wakeup\n");
			}
			pthread_mutex_unlock(&play_seek_play.mutex);
			previous_state = 0;
			current_state = 0;
			play_seek_play.value = 0;
			}
			if(DMRstop)
			{
			printf("In DMRstop\n");
			pthread_mutex_lock(&stop_wait.mutex);
			stop_wait.value = 1;
			pthread_cond_signal(&stop_wait.cond);
			pthread_mutex_unlock(&stop_wait.mutex);
	/*we just tell state_machine that we got DMRstop signal and we are ready to quit */
			DMRstop = false;
			return NPT_SUCCESS;
			}

		}
		//maybe song has finished or decode could have error occured.
		extern_service->SetStateVariable("TransportState","STOPPED");
		current_state = 1;
		
		stop_wait.value =1;
		if(stop_wait.value == 1){
		printf("finish playing\n");
		state_machine(previous_state, current_state);
		}
	}
	else 
	{
	extern_service->SetStateVariable("TransportState","STOPPED");
	current_state = 1;
	stop_wait.value = 1;
	state_machine(previous_state, current_state);
	return NPT_FAILURE;
	}
	
	return NPT_SUCCESS;
}

    NPT_Result PlayControl::OnPrevious(PLT_ActionReference& action,PLT_Service* service)
	{
		return NPT_SUCCESS;
	}
    NPT_Result PlayControl::OnSeek(PLT_ActionReference& action,PLT_Service* service)
	{
		NPT_String unit;
		NPT_String target;
		NPT_String transportstate;
		float real_seconds;
		int flags;
	/*	pthread_mutex_lock(&play_seek_play.mutex);
		play_seek_play.value = 1;
		pthread_cond_signal(&play_seek_play.cond);
		pthread_mutex_unlock(&play_seek_play.mutex);
	*/	current_state = 2;
		int ret = state_machine(previous_state,current_state);
		if(ret == 1)
		{//when we get here,  that means previous state is pause.
		printf("Test2\n");
		action->GetArgumentValue("Unit",unit);
		action->GetArgumentValue("Target",target);
		service->SetStateVariable("RelativeTimePosition",target.GetChars());
	
		real_seconds = time2sec(target.GetChars());
	//     we should flush the audio AVPacket,how to do it?av_free_packet,is it right?
		if(pkt.data != NULL)av_free_packet(&pkt);
		AVRational timeBase = fmt_ctx->streams[audio_stream_idx]->time_base;
		int64_t seek_pos =(int64_t)(real_seconds*AV_TIME_BASE);
		int64_t seek_target = av_rescale_q(seek_pos,AV_TIME_BASE_Q,timeBase);
		flags = real_seconds>seconds?0:AVSEEK_FLAG_BACKWARD; 
		av_seek_frame(fmt_ctx,audio_stream_idx,seek_target,flags);
		avcodec_flush_buffers(audio_dec_ctx);
		seconds = lastseconds = real_seconds;
		extern_service->SetStateVariable("TransportState","PAUSED_PLAYBACK");	
		 //return to the pause state;
		previous_state = current_state = 3;
		return NPT_SUCCESS;
		}
		if(ret == -1)return NPT_SUCCESS;
		printf("seek sleep\n");
		pthread_mutex_lock(&seek_wait.mutex);		
		while(seek_wait.value==0){
		pthread_cond_wait(&seek_wait.cond,&seek_wait.mutex);
		}
		pthread_mutex_unlock(&seek_wait.mutex);
		printf("seek wake up!!!\n");
		seek_wait.value = 0;
		action->GetArgumentValue("Unit",unit);
		action->GetArgumentValue("Target",target);
		extern_service->SetStateVariable("RelativeTimePosition",target.GetChars());
		real_seconds = time2sec(target.GetChars());
	//     we should flush the audio AVPacket,how to do it?av_free_packet,is it right?
		if(pkt.data != NULL)av_free_packet(&pkt);
		AVRational timeBase = fmt_ctx->streams[audio_stream_idx]->time_base;
		int64_t seek_pos =(int64_t)(real_seconds*AV_TIME_BASE);
		int64_t seek_target = av_rescale_q(seek_pos,AV_TIME_BASE_Q,timeBase);
		flags = real_seconds>seconds?0:AVSEEK_FLAG_BACKWARD; 
		av_seek_frame(fmt_ctx,audio_stream_idx,seek_target,flags);
		avcodec_flush_buffers(audio_dec_ctx);
		
		seconds=lastseconds=real_seconds;
		pthread_mutex_lock(&play_seek_play.mutex);
		play_seek_play.value = 1;
		pthread_cond_signal(&play_seek_play.cond);
		pthread_mutex_unlock(&play_seek_play.mutex);
		extern_service->SetStateVariable("TransportState","PLAYING");
		previous_state = 0;
		return NPT_SUCCESS;
	}
    NPT_Result PlayControl::OnStop(PLT_ActionReference& action,PLT_Service* service)
	{
		int ret = 0;
		current_state = 1;
		state_machine(previous_state,current_state);
		extern_service->SetStateVariable("TransportState","STOPPED");
		return NPT_SUCCESS;
	}

NPT_Result PlayControl::OnSetAVTransportURI(PLT_ActionReference& action, PLT_Service* serviceAVT)
{
	printf("In OnSetAVTransportURI\n");
	        NPT_String uri;
		NPT_String metadata;
		NPT_String TransportState;
		NPT_String A_ARG_TYPE_InstanceID;
		int ret=0;
		current_state = 4;
		ret = state_machine(previous_state, current_state);
		if(ret < 0)return NPT_SUCCESS;
	    	action->GetArgumentValue("CurrentURI", CurrentTrackURI);
		uri = CurrentTrackURI;
		action->GetArgumentValue("CurrentURIMetaData", metadata);
		extern_service->SetStateVariable("AVTransportURIMetaData",metadata);
/*
		if(strcmp("NO_MEDIA_PRESENT",TransportState.GetChars()) == 0)
		{
			printf("NO_MEDIA_PRESENT\n");
			extern_service->SetStateVariable("TransportState", "STOPPED");
			return NPT_SUCCESS;		
		}
*/
//		if(previous_state != -1){
//		while(setavtransporturi_wait.value == 0)
//		pthread_cond_wait(&setavtransporturi_wait.cond,&setavtransporturi_wait.mutex);
//		printf("setavtransporturi wake up value=%d\n",setavtransporturi_wait.value);
//		AVDictionary *stream_opts = NULL;
//		av_dict_set(&optionsDict, "rtsp_transport", "tcp", 0);
		av_dict_set(&stream_opts, "timeout", "180000000", 0);//mseconds
//		printf("before avformat_open_input\n");print_time();
		ret = avformat_open_input(&fmt_ctx, uri.GetChars(), NULL, &stream_opts);
//		printf("after avformat_open_input\n");print_time();
//		fmt_ctx->interrupt_callback.callback = interrupt_cb;
//		fmt_ctx->interrupt_callback.opaque = fmt_ctx;
//		}
//		setavtransporturi_wait.value = 0;
		if (ret < 0) {
         		fprintf(stderr, "Could not open source file %s\n", CurrentTrackURI.GetChars());
			extern_service->SetStateVariable("TransportState", "STOPPED");
			current_state = 1;
			state_machine(previous_state,current_state);
     		    	return NPT_FAILURE;
     		}
		return NPT_SUCCESS;

}
NPT_Result PlayControl::OnSetPlayMode(PLT_ActionReference& action,PLT_Service* service)
{
	return NPT_SUCCESS;
}

NPT_Result PlayControl::GetPositionInfo(PLT_ActionReference& action,PLT_Service* service)
{
		//not used, we implement this function in MediaRenderer::GetPositionInfo
		//ofcourse we can implement in here	
	return NPT_SUCCESS;
}
NPT_Result PlayControl::GetTransportInfo(PLT_ActionReference& action, PLT_Service* service)
{
	printf("In GetTransportInfo\n");
	if(current_state == 0)extern_service->SetStateVariable("TransportState","PLAYING");
	if(current_state == 1 || current_state == -1)extern_service->SetStateVariable("TransportState","STOPPED");
	if(current_state == 3)extern_service->SetStateVariable("TransportState","PAUSED_PLAYBACK");
	//Seek
	if(current_state == 2)extern_service->SetStateVariable("TransportState","TRANSITIONING");
	//AVTransportURI
	if(current_state == 4)extern_service->SetStateVariable("TransportState","TRANSITIONING");
	extern_service->SetStateVariable("TransportPlaySpeed","1");
	action->SetArgumentsOutFromStateVariable();
	return NPT_SUCCESS;
}

    // RenderingControl
NPT_Result PlayControl::OnSetVolume(PLT_ActionReference& action)
{
	return NPT_SUCCESS;
}
NPT_Result PlayControl::OnSetVolumeDB(PLT_ActionReference& action)
{
	return NPT_SUCCESS;
}
NPT_Result PlayControl::OnGetVolumeDBRange(PLT_ActionReference& action)
{
	return NPT_SUCCESS;
}
NPT_Result PlayControl::OnSetMute(PLT_ActionReference& action)
{
	return NPT_SUCCESS;
}


MediaRenderer::MediaRenderer(const char* friendlyname, bool show_ip,const char* uuid, unsigned int port, bool port_rebind):
PLT_MediaRenderer(friendlyname,show_ip,uuid,port,port_rebind)
{
	//do nothing
}

MediaRenderer::~MediaRenderer()
{
	//do nothing
}


NPT_Result MediaRenderer::OnAction(PLT_ActionReference& action,
                             const PLT_HttpRequestContext& context)
{
	struct timeval action_time;
	gettimeofday(&action_time,NULL);
	NPT_COMPILER_UNUSED(context);
     /* parse the action name */
	NPT_String name = action->GetActionDesc().GetName();
	if(action_trigger_cond.first == true){
		printf("first action\n");
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time = action_time;
		action_trigger_cond.first = false;
	}
 
     // since all actions take an instance ID and we only support 1 instance
     // verify that the Instance ID is 0 and return an error here now if not
	NPT_String serviceType = action->GetActionDesc().GetService()->GetServiceType();
	if (serviceType.Compare("urn:schemas-upnp-org:service:AVTransport:1", true) == 0) {
		if (NPT_FAILED(action->VerifyArgumentValue("InstanceID", "0"))) {
		action->SetError(718, "Not valid InstanceID");
		return NPT_FAILURE;
		}
	}
	serviceType = action->GetActionDesc().GetService()->GetServiceType();
	if (serviceType.Compare("urn:schemas-upnp-org:service:RenderingControl:1", true) == 0) {
		if (NPT_FAILED(action->VerifyArgumentValue("InstanceID", "0"))) {
                         action->SetError(702, "Not valid InstanceID");
                         return NPT_FAILURE;
                 }
         }
       
//	fprintf(stderr,"OnAction action name is %s\n",name.GetChars());
         /* Is it a ConnectionManager Service Action ? */
	
	if (name.Compare("GetCurrentConnectionInfo", true) == 0) {
		FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
		return OnGetCurrentConnectionInfo(action);
         }
	if(name.Compare("GetProtocolInfo",true) == 0){
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", Connection_Service);
	return GetProtocolInfo(action);
	}
	 
         /* Is it a AVTransport Service Action ? */
	if(name.Compare("GetTransportInfo",true) == 0){//out:TransportState,TransportStatus,TransportPlaySpeed.
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
	return GetTransportInfo(action);
	}
	if(name.Compare("GetPositionInfo",true) == 0){
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
	return GetPositionInfo(action);
	}   
	if (name.Compare("Next", true) == 0) {
	if(action_continue(name, last_action_name, action_time)==2)
		{
		printf("time interval is too small\n");
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time.tv_sec = action_time.tv_sec;
		action_trigger_cond.last_action_trigger_time.tv_usec = action_time.tv_usec;
		return NPT_SUCCESS;
		}
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
	last_action_name = name;
        action_trigger_cond.last_action_trigger_time = action_time;
	return OnNext(action);
	}
	if (name.Compare("Pause", true) == 0) {
        if(action_continue(name, last_action_name, action_time)==2)
        	{
                printf("time interval is too small\n");
                last_action_name = name;
                action_trigger_cond.last_action_trigger_time = action_time;
                return NPT_SUCCESS;
                }
        last_action_name = name;
        action_trigger_cond.last_action_trigger_time = action_time;
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
	return OnPause(action);
	}
	if (name.Compare("Play", true) == 0) {
	          if(action_continue(name, last_action_name, action_time)==2)
                  {
                  printf("time interval is too small\n");
                  last_action_name = name;
                  action_trigger_cond.last_action_trigger_time = action_time;
                  return NPT_SUCCESS;
                  }
          last_action_name = name;
          action_trigger_cond.last_action_trigger_time = action_time;

	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
        return OnPlay(action);
	}
	if (name.Compare("Previous", true) == 0) {
		if(action_continue(name, last_action_name, action_time)==2)
		{
		printf("time interval is too small\n");
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time = action_time;	
		return NPT_SUCCESS;
		}
          last_action_name = name;
          action_trigger_cond.last_action_trigger_time = action_time;

	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
        return OnPrevious(action);
	}
	if (name.Compare("Seek", true) == 0) {
		if(action_continue(name, last_action_name, action_time)==2)
		{
		printf("time interval is too small\n");
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time = action_time;
		return NPT_SUCCESS;
                }
          last_action_name = name;
          action_trigger_cond.last_action_trigger_time = action_time;

	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
        return OnSeek(action);
	}
	if (name.Compare("Stop", true) == 0) {
	if(action_continue(name, last_action_name, action_time)==2)
                 {
                 printf("time interval is too small\n");
                 last_action_name = name;
                 action_trigger_cond.last_action_trigger_time = action_time;
                 return NPT_SUCCESS;
                 }
	last_action_name = name;
        action_trigger_cond.last_action_trigger_time = action_time;
	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
        return OnStop(action);
	}
	if (name.Compare("SetAVTransportURI", true) == 0) {
		if(action_continue(name, last_action_name, action_time)==2)
		{
		printf("time interval is too small\n");
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time = action_time;
		return NPT_SUCCESS;
                }
		last_action_name = name;
		action_trigger_cond.last_action_trigger_time = action_time;

	FindServiceByType("urn:schemas-upnp-org:service:AVTransport:1", extern_service);
        return OnSetAVTransportURI(action);
	}
	if (name.Compare("SetPlayMode", true) == 0) {
        return OnSetPlayMode(action);
	}
 
     /* Is it a RendererControl Service Action ? */
	if (name.Compare("SetVolume", true) == 0) {
        return OnSetVolume(action);
	}
	if (name.Compare("SetVolumeDB", true) == 0) {
	return OnSetVolumeDB(action);
	}
	if (name.Compare("GetVolumeDBRange", true) == 0) {
                 return OnGetVolumeDBRange(action);
	}
	if (name.Compare("SetMute", true) == 0) {
	return OnSetMute(action);
	}
 
     // other actions rely on state variables
     NPT_CHECK_LABEL_WARNING(action->SetArgumentsOutFromStateVariable(), failure);
     return NPT_SUCCESS;
 
 failure:
     action->SetError(401,"No Such Action.");
     return NPT_FAILURE;

	
}



 NPT_Result MediaRenderer::ProcessHttpPostRequest(NPT_HttpRequest& request,const NPT_HttpRequestContext& context,
                                               NPT_HttpResponse& response)
{
     NPT_Result                res;
     NPT_String                service_type;
     NPT_String                str;
     NPT_XmlElementNode*       xml = NULL;
     NPT_String                soap_action_header;
     PLT_Service*              service;
     NPT_XmlElementNode*       soap_body;
     NPT_XmlElementNode*       soap_action;
     PLT_ActionDesc*           action_desc;
     PLT_ActionReference       action;
     NPT_MemoryStreamReference resp(new NPT_MemoryStream);
     NPT_String                ip_address  = context.GetRemoteAddress().GetIpAddress().ToString();
     NPT_String                method      = request.GetMethod();
     NPT_String                url         = request.GetUrl().ToRequestString();
     NPT_String                protocol    = request.GetProtocol();
     NPT_List<NPT_String>      components;
     NPT_String                soap_action_name;
 
 #if defined(PLATINUM_UPNP_SPECS_STRICT)
     const NPT_String*         attr;
 #endif
 
     if (NPT_FAILED(FindServiceByControlURL(url, service, true)))
         goto bad_request;
 
     if (!request.GetHeaders().GetHeaderValue("SOAPAction"))
         goto bad_request;
 
     // extract the soap action name from the header
     soap_action_header = *request.GetHeaders().GetHeaderValue("SOAPAction");
     soap_action_header.TrimLeft('"');
     soap_action_header.TrimRight('"');
 
     components = soap_action_header.Split("#");
     if (components.GetItemCount() != 2)
         goto bad_request;
 
     soap_action_name = *components.GetItem(1);
 
     // read the xml body and parse it
     if (NPT_FAILED(PLT_HttpHelper::ParseBody(request, xml)))
         goto bad_request;
 
     // check envelope
     if (xml->GetTag().Compare("Envelope", true))
         goto bad_request;
 
 #if defined(PLATINUM_UPNP_SPECS_STRICT)
     // check namespace
     if (!xml->GetNamespace() || xml->GetNamespace()->Compare("http://schemas.xmlsoap.org/soap/envelope/"))
         goto bad_request;
 
     // check encoding
     attr = xml->GetAttribute("encodingStyle", "http://schemas.xmlsoap.org/soap/envelope/");
     if (!attr || attr->Compare("http://schemas.xmlsoap.org/soap/encoding/"))
         goto bad_request;
 #endif
 
     // read action
     soap_body = PLT_XmlHelper::GetChild(xml, "Body");
     if (soap_body == NULL)
         goto bad_request;
     PLT_XmlHelper::GetChild(soap_body, soap_action);
     if (soap_action == NULL)
         goto bad_request;
 
     // verify action name is identical to SOAPACTION header*/
     if (soap_action->GetTag().Compare(soap_action_name, true))
         goto bad_request;
 
     // verify namespace
     if (!soap_action->GetNamespace() || soap_action->GetNamespace()->Compare(service->GetServiceType()))
         goto bad_request;
 
     // create a buffer for our response body and call the service
     if ((action_desc = service->FindActionDesc(soap_action_name)) == NULL) {
         // create a bastard soap response
         PLT_Action::FormatSoapError(401, "Invalid Action", *resp);
         goto error;
     }
 
     // create a new action object
     action = new PLT_Action(*action_desc);
 
     // read all the arguments if any
     for (NPT_List<NPT_XmlNode*>::Iterator args = soap_action->GetChildren().GetFirstItem();
                  args;
                  args++) {
         NPT_XmlElementNode* child = (*args)->AsElementNode();
         if (!child) continue;
 
         // Total HACK for xbox360 upnp uncompliance!
         NPT_String name = child->GetTag();
         if (action_desc->GetName() == "Browse" && name == "ContainerID") {
             name = "ObjectID";
         }
 
         res = action->SetArgumentValue(
             name,
             child->GetText()?*child->GetText():"");
 
                 // test if value was correct
                 if (res == NPT_ERROR_INVALID_PARAMETERS) {
                         action->SetError(701, "Invalid Name");
                         goto error;
                 }
     }
 
         // verify all required arguments were passed
     if (NPT_FAILED(action->VerifyArguments(true))) {
         action->SetError(402, "Invalid or Missing Args");
         goto error;
     }
 
     NPT_LOG_FINE_2("Processing action \"%s\" from %s",
                    (const char*)action->GetActionDesc().GetName(),
                    (const char*)context.GetRemoteAddress().GetIpAddress().ToString());
     // call the virtual function, it's all good
     if (NPT_FAILED(OnAction(action, PLT_HttpRequestContext(request, context)))) {
         goto error;
     }
 
     // create the soap response now
     action->FormatSoapResponse(*resp);
     goto done;
 
 error:
     if (!action.IsNull()) {
         // set the error in case it wasn't done already
         if (action->GetErrorCode() == 0) {
             action->SetError(501, "Action Failed");
         }
         NPT_LOG_WARNING_3("Error while processing action %s: %d %s",
             (const char*)action->GetActionDesc().GetName(),
             action->GetErrorCode(),
             action->GetError());
 
         action->FormatSoapResponse(*resp);
     }
 
     response.SetStatus(500, "Internal Server Error");
 
 done:
     NPT_LargeSize resp_body_size;
     if (NPT_SUCCEEDED(resp->GetAvailable(resp_body_size))) {
         NPT_HttpEntity* entity;
         PLT_HttpHelper::SetBody(response,
                                 (NPT_InputStreamReference)resp,
                                 &entity);
         entity->SetContentType("text/xml; charset=\"utf-8\"");
         response.GetHeaders().SetHeader("Ext", ""); // should only be for M-POST but oh well
     }
 
     delete xml;
     return NPT_SUCCESS;
 
 bad_request:
     delete xml;
     response.SetStatus(500, "Bad Request");
     return NPT_SUCCESS; 

}



NPT_Result MediaRenderer::GetProtocolInfo(PLT_ActionReference& action)
{
	//list of transfer protocols and data formats supported by the MediaRenderer 
	//is returned to the control point.
	PLT_Service *service;
	this->FindServiceByType("urn:schemas-upnp-org:service:ConnectionManager:1", service);
	service->SetStateVariable("SinkProtocolInfo",/*"http-get:*:audio/L16;rate=44100;channels=1:DLNA.ORG_PN=LPCM,http-get:*:audio/L16;rate=44100;channels=2:DLNA.ORG_PN=LPCM,*/"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL,http-get:*:video/mpeg:D    LNA.ORG_PN=MPEG_PS_NTSC,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_NTSC_XAC3,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG_PS_PAL_XAC3,http-get:*:video/mpeg:DLNA.ORG_PN=MPEG1");
	service->SetStateVariable("SourceProtocolInfo","");
	action->SetArgumentsOutFromStateVariable();
	return NPT_SUCCESS;

}



NPT_Result MediaRenderer::GetPositionInfo(PLT_ActionReference& action)
{
	printf("In MediaRenderer::GetPositionInfo\n");
	char playtime[20];
        seconds2timestring(playtime,20,seconds);
	extern_service->SetStateVariable("RelativeTimePosition",playtime);
	extern_service->SetStateVariable("CurrentTrackDuration",media_duration);
	extern_service->SetStateVariable("CurrentTrack","1");
	extern_service->SetStateVariable("CurrentTrackURI",CurrentTrackURI);
	extern_service->SetStateVariable("CurrentTransportActions","Play,GetPositionInfo");
/*	action->SetArgumentOutFromStateVariable("RelativeTimePosition");
	action->SetArgumentOutFromStateVariable("CurrentTrackDuration");
	action->SetArgumentOutFromStateVariable("CurrentTrack");*/
	action->SetArgumentsOutFromStateVariable();
	return NPT_SUCCESS;
}





