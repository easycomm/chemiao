/*****************************************************************
|
|   Platinum - Test UPnP A/V MediaRenderer
|
| Copyright (c) 2004-2010, Plutinosoft, LLC.
| All rights reserved.
| http://www.plutinosoft.com
|
| This program is free software; you can redistribute it and/or
| modify it under the terms of the GNU General Public License
| as published by the Free Software Foundation; either version 2
| of the License, or (at your option) any later version.
|
| OEMs, ISVs, VARs and other distributors that combine and 
| distribute commercially licensed software with Platinum software
| and do not wish to distribute the source code for the commercially
| licensed software under version 2, or (at your option) any later
| version, of the GNU General Public License (the "GPL") must enter
| into a commercial license agreement with Plutinosoft, LLC.
| licensing@plutinosoft.com
| 
| This program is distributed in the hope that it will be useful,
| but WITHOUT ANY WARRANTY; without even the implied warranty of
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
| GNU General Public License for more details.
|
| You should have received a copy of the GNU General Public License
| along with this program; see the file LICENSE.txt. If not, write to
| the Free Software Foundation, Inc., 
| 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
| http://www.gnu.org/licenses/gpl-2.0.html
|
 ****************************************************************/

/*----------------------------------------------------------------------
|   includes
+---------------------------------------------------------------------*/
#include "DMRRealize.h"
#include "ffmpeg.h"
#include "FFDecode.h" 
#include "alsa.h"
#include <pthread.h>
#include "statemachine.h"
#include "DMRExtern.h"
#include "alsa.h"
/*
*	we use libdaemon to daemonize
*/
#include <libdaemon/dfork.h>
#include <libdaemon/dsignal.h>
#include <libdaemon/dlog.h>
#include <libdaemon/dpid.h>
#include <libdaemon/dexec.h>
/*----------------------------------------------------------------------
|   globals
+---------------------------------------------------------------------*/

/* extern "C"{
  
  struct statechange{
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  int value;
 };
}
*/

struct delay_action  action_trigger_cond;
NPT_String last_action_name;
volatile bool DMRpause = false; 
volatile bool DMRseek = false;
volatile bool DMRstop = false;
volatile bool DMRpause2stop = false;
volatile bool FirstGetPositionInfo = true;

AVFormatContext *fmt_ctx = NULL;
AVCodecContext  *audio_dec_ctx=NULL;  
int audio_stream_idx = -1;
AVFrame  frame;
AVPacket pkt;
AVStream *audio_stream=NULL;
int channels=0;
int sample_rate = 0;

float seconds=0; float lastseconds=0;
char media_duration[20];

snd_pcm_t *pcm_handle=NULL;
int alsa_write_byte = 0;
int period_frames = 0;
uint8_t *pcm_buffer;
int pcm_buffer_size = 0;
uint8_t *resample_data=NULL;

struct statechange play_pause_play={PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER,0};
struct statechange play_seek_play={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0};
struct statechange seek_wait={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0};
struct statechange stop_wait={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0};
struct statechange setavtransporturi_wait={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0};
//pthread_mutex_t stop2setavtransporturi_mutex=PTHREAD_MUTEX_INITIALIZER;
int previous_state = -1;
int current_state = -1;

NPT_String CurrentTrackURI;

PLT_Service* extern_service;
PLT_Service* Connection_Service;

static int fill_iobuffer(void * opaque,uint8_t *buf, int bufsize)
{
	fprintf(stderr,"In fill_iobuffer\n");
//	char * path = "/tmp/1.mp3";
	int fd = open("/tmp/1.mp3",O_RDWR);
	if(fd < 0){
		fprintf(stderr,"fill_iobuffer:open file error\n");
		return -1;
	}
	int ret = read(fd,buf,bufsize);
	if(ret < 0){
		fprintf(stderr,"fill_iobuffer:read file error\n");
		close(fd);
		return -1;
	}
	else 
	{
		close(fd);
		return ret;
	}
}

int
main(int  argc , char** argv)
{
	
	unsigned char * mp3 = (unsigned char *)av_malloc(4096);
	  
	PLT_UPnP upnp;
	int ret;
//	NPT_LogManager::GetDefault().Configure("plist:.level=FINE;.handlers=ConsoleHandler;.ConsoleHandler.colors=off;.ConsoleHandler.filter=42");
	PLT_MediaRenderer * device = NULL;
	MediaRenderer render("EasyComm-DMR",false,"e6572b54-f3c7-2d91-2fb5-b757f2537e21");	
	device = &render;
	PLT_MediaRendererDelegate *dmr = NULL; 
	PlayControl play;
	dmr = &play;
	action_trigger_cond.first = true;
//	mediarender.SetDelegate(&dmr)
	device->SetDelegate(dmr);
	PLT_DeviceHostReference EasyComm_DMRDevice(device);
//	pthread_mutex_init(&stop2setavtransporturi_mutex,NULL);
	pthread_mutex_init(&play_seek_play.mutex,NULL);
	pthread_mutex_init(&seek_wait.mutex,NULL);
	pthread_mutex_init(&stop_wait.mutex,NULL);
	pthread_mutex_init(&setavtransporturi_wait.mutex,NULL);
	av_register_all();
	avformat_network_init();
	fmt_ctx = avformat_alloc_context(); 
	AVIOContext *avio =avio_alloc_context(mp3, 4096,0,NULL,fill_iobuffer,NULL,NULL);
	fmt_ctx->pb = avio;
	/*
	 AVIOContext *avio_alloc_context(
                    unsigned char *buffer,
                    int buffer_size,
                    int write_flag,
                    void *opaque,
                    int (*read_packet)(void *opaque, uint8_t *buf, int buf_size),
                    int (*write_packet)(void *opaque, uint8_t *buf, int buf_size),
                    int64_t (*seek)(void *opaque, int64_t offset, int whence))

	*/
	ret = avformat_open_input(&fmt_ctx,"nothing",NULL,NULL);
	if(ret < 0)fprintf(stderr,"avformat_open_input error\n");
	ret = avformat_find_stream_info(fmt_ctx, NULL);
	if(ret < 0)fprintf(stderr,"avformat_find_stream_info error\n");
	open_codec_context(&audio_stream_idx, fmt_ctx, AVMEDIA_TYPE_AUDIO);
	av_dump_format(fmt_ctx, 0, NULL, 0);
	if(fmt_ctx != NULL)
	avformat_close_input(&fmt_ctx); 
        if(audio_dec_ctx != NULL)
          avcodec_close(audio_dec_ctx);

//	ret = alsa_init(2,44100*2);
//	printf("In Main,After alsa_init,ret=%d\n",ret);
	printf("before upnp start\n");
 	upnp.Start();
	upnp.AddDevice(EasyComm_DMRDevice);	
	while (1) {
	usleep(30000);
	} 
	upnp.Stop();
//	delete device;
//	delete dmr;
	return 0;
}
