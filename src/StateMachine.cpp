#include "statemachine.h"
#include "util.h"
int state_machine(int previous, int current)
{
//when a state is over, we do some clean task.
//when a state is start, we also do some clean task.
//remember that at any time, there is only one state.
printf("in state_machine previous_state=%d\tcurrent_state=%d\n",previous_state,current_state);
printf("in state_machine previous=%d\tcurrent=%d\n",previous,current);
if(previous == 0)
{
	if(current == 0)/*play->play,in some cases like upnpmonkey,this happens
	*I have to Say that UPnPMonkey is so strange, after activating a play action, it actvates play action again.	
	*/
	{
		return 1;
	}
	if(current == 1)  //play->stop, stop occurs in many cases, like something error lead to stop and stop action leads to stop.
	{
	//	stop_wait.value = 0;
		DMRstop = true;
		while(stop_wait.value == 0)
		{
			printf("stop sleep\n");
			pthread_cond_wait(&stop_wait.cond,&stop_wait.mutex);
			printf("wake up\n");
		}
		stop_wait.value = 0;
	/*if we wake up ,that means we stop playing,and let us clear some 
	* context.
	*/
//	pthread_mutex_lock(&setavtransporturi_wait.mutex);
	if(audio_dec_ctx != NULL)
	avcodec_close(audio_dec_ctx);
	if(fmt_ctx != NULL)
	avformat_close_input(&fmt_ctx);
	audio_stream = NULL;
//	setavtransporturi_wait.value = 1;
//	pthread_cond_signal(&setavtransporturi_wait.cond);
//	pthread_mutex_unlock(&setavtransporturi_wait.mutex);
	if(pkt.data != NULL)
		av_free_packet(&pkt);	
	DMRstop = false;
	}
	if(current == 3)//play->pause
	{
		DMRpause =true;
		printf("in state_machine set DMRpause as TRUE\n");
		//I'm not sure after we set DMRpause = true
		//the play function could imediatly get the signal,but some delay is acceptable.
	}
	if(current == 2)//play->seek
	{
		printf("*******************\n************\n**********\n************\n***********\n");
		DMRseek = true;
	}
	if(current == 0)//play->play
	{
	
	}
	if(current == 4)//play->SetAVTransportURI, that is illegal
	{
		return -1;
	}
}

if(previous == 1)
{
	if(current == 4)//stop to avtransport
	{
	/*
	* In most cases, DMC use stop action before SetAVTransportURI action
	* other cases, we don't think about it. If something other occurs, this whole program may crush.
 	*/
	}
	
	if(current == 2)//stop->seek that is really exist
	{
		current_state = previous_state = 1;
		return -1;
	}
	if(current == 0){
		current_state = previous_state = 1;
		return -1;
	}
}

if(previous == 2)
{
	if(current == 2)  //seek->seek
	{
		previous_state = current;
		return 1;
	}
	if(current == 1) //seek->stop
	{
	}
	if(current == 3)//seek->pause
	{
	}
	if(current == 0)//seek->play
	{
	DMRpause = false;
	pthread_mutex_lock(&play_pause_play.mutex);
        play_pause_play.value = 1;
      	pthread_cond_signal(&play_pause_play.cond);
       	pthread_mutex_unlock(&play_pause_play.mutex);
	previous_state = current;
	return 1;

	}
}

if(previous == 3)
{
	if(current == 0)//pause->play
	{
		//new play function start,and we must wakeup the old play function
		//and return.
		DMRpause = false;
		pthread_mutex_lock(&play_pause_play.mutex);
		play_pause_play.value = 1;
		pthread_cond_signal(&play_pause_play.cond);
		pthread_mutex_unlock(&play_pause_play.mutex);
		previous_state = current;
		return 1;
		//after returning, play function return NPT_SUCCESS and
		//reset service->state to PLAYING.
	}
	if(current == 1)//pause->stop
	{
		/*if we got pause to stop action, first we should tell OnPlay function to exit
		* then we should do some clean task. 
		*/
		printf("In statemachine:pause->stop\n");	
		DMRpause2stop = true;
		pthread_mutex_lock(&play_pause_play.mutex);
		play_pause_play.value = 1;
		pthread_cond_signal(&play_pause_play.cond);
		pthread_mutex_unlock(&play_pause_play.mutex);
		
		/*
		* we sleep until we got signal that claim OnPlay is ready to exit
		*/
		while(DMRpause2stop)usleep(5000);
		avcodec_close(audio_dec_ctx);
		avformat_close_input(&fmt_ctx);
		audio_stream = NULL;
		if(pkt.data != NULL)
			av_free_packet(&pkt);	
	}
		
	
	if(current == 2)//pause->seek
	{
		previous_state = current;
		return 1;
	}

}
if(previous == 4)//avtransport state
{
	if(current == 0)//avtransport->play 
	{
		//put works like finding codec and finding stream here	
		if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
		fprintf(stderr, "Could not find stream information\n");
		extern_service->SetStateVariable("TransportState", "STOPPED");
		previous_state = 1;
		if(fmt_ctx != NULL)avformat_close_input(&fmt_ctx);
		return -1;  
		}
		float duration = fmt_ctx->duration/AV_TIME_BASE;
		seconds2timestring(media_duration,20,duration);
		printf("media_duration=%s\n",media_duration);
	}

	if(current == 1)//avtransport->stop
	{
	//close contex
	if(fmt_ctx != NULL)avformat_close_input(&fmt_ctx);
	if(audio_dec_ctx != NULL)avcodec_close(audio_dec_ctx);
	}
}
	//update the previous_state
	previous_state = current;
	return 0;
}






