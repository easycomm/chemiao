#include "ffmpeg.h"

/*
extern int decode_packet(int *got_frame, int cached,int fd)
{
    int ret = 0;
    int decoded = pkt.size;

    *got_frame = 0;

    if (pkt.stream_index == video_stream_idx) {
        ret = avcodec_decode_video2(video_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding video frame (%s)\n", av_err2str(ret));
            return ret;
        }

        if (*got_frame) {
            printf("video_frame%s n:%d coded_n:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   video_frame_count++, frame->coded_picture_number,
                   av_ts2timestr(frame->pts, &video_dec_ctx->time_base));

            av_image_copy(video_dst_data, video_dst_linesize,
                          (const uint8_t **)(frame->data), frame->linesize,
                          video_dec_ctx->pix_fmt, video_dec_ctx->width, video_dec_ctx->height);

            fwrite(video_dst_data[0], 1, video_dst_bufsize, video_dst_file);
        }
    } else if (pkt.stream_index == audio_stream_idx) {
        ret = avcodec_decode_audio4(audio_dec_ctx, frame, got_frame, &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error decoding audio frame (%s)\n", av_err2str(ret));
            return ret;
        }
        decoded = FFMIN(ret, pkt.size);

        if (*got_frame) {
            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(frame->format);
            printf("audio_frame%s n:%d nb_samples:%d pts:%s\n",
                   cached ? "(cached)" : "",
                   audio_frame_count++, frame->nb_samples,
                   av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

    return decoded;
}
*/
extern  int open_codec_context(int *stream_idx,
                              AVFormatContext *fmt_ctx, enum AVMediaType type)
{
    int ret;
    AVStream *st;
    AVStream *audioStream;
    AVCodecContext *dec_ctx = NULL;
    AVCodec *dec = NULL;
    AVDictionary *opts = NULL;

for (unsigned int i = 0; i < fmt_ctx->nb_streams; ++i)
	{
		if (fmt_ctx->streams[i]->codec->codec_type == type)
		{
			audioStream = fmt_ctx->streams[i];
			ret = i;
			break;
		}
	}

	if (audioStream == NULL)
	{
		fprintf(stderr, "Could not find stream in inputfile\n");
		return -1;
	}

//    ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
    if (ret < 0) {
        fprintf(stderr, "Could not find %s stream in input file\n",
                av_get_media_type_string(type));
        return ret;
    } else {
        *stream_idx = ret;
        st = fmt_ctx->streams[*stream_idx];

        /* find decoder for the stream */
        dec_ctx = st->codec;
        dec = avcodec_find_decoder(dec_ctx->codec_id);
        if (!dec) {
            fprintf(stderr, "Failed to find %s codec\n",
                    av_get_media_type_string(type));
            return AVERROR(EINVAL);
        }

        /* Init the decoders, with or without reference counting */
  //      if (api_mode == API_MODE_NEW_API_REF_COUNT)
  //          av_dict_set(&opts, "refcounted_frames", "1", 0);
        if ((ret = avcodec_open2(dec_ctx, dec, &opts)) < 0) {
            fprintf(stderr, "Failed to open %s codec\n",
                    av_get_media_type_string(type));
            return ret;
        }
    }

    return 0;
}

bool GetNextFrame(bool fFirstTime,int audioStream,AVFormatContext *pFormatCtx, AVCodecContext *pCodecCtx, AVFrame *pFrame,AVPacket *packet)
{
   
    int bytesDecoded;
    int frameFinished;

//  我们第一次调用时，将 packet.data 设置为NULL指明它不用释放了
    if(fFirstTime)
    {
        fFirstTime=false;
        packet->data=NULL;
	packet->size = 0;
	printf("first time\n");
    }

// 解码直到成功解码完整的一帧
    while(true)
    {
         //  除非解码完毕，否则一直在当前包中工作
        while(packet->size > 0)
        {
//	printf("packet->size=%d\n",packet->size);
        //  解码下一块数据
 //           bytesDecoded=avcodec_decode_video(pCodecCtx, pFrame,
 //               &frameFinished, rawData, bytesRemaining);
        bytesDecoded = avcodec_decode_audio4(pCodecCtx, pFrame, &frameFinished, packet);

                // 出错了？
            if(bytesDecoded < 0)
            {
                fprintf(stderr, "Error while decoding frame\n");
                return false;
            }

            packet->size -= bytesDecoded;    // in some rare situation this may>0
            packet->data += bytesDecoded;

                // 我们完成当前帧了吗？接着我们返回
            if(frameFinished)
                return true;
        }
/*     while (av_read_frame(fmt_ctx, &pkt) >= 0) { 
         AVPacket orig_pkt = pkt;
         do {
             ret = decode_packet(&got_frame, 0);
             if (ret < 0)
                 break;
             pkt.data += ret;
             pkt.size -= ret;
         } while (pkt.size > 0);
         av_free_packet(&orig_pkt);
     }   */         
        // 读取下一包，跳过所有不属于这个流的包
   //     do
   //     {
            // 释放旧的包
            if(packet->data!=NULL)
                av_free_packet(packet);

            // 读取新的包
            if(av_read_frame(pFormatCtx, packet)<0)
                goto loop_exit;
   //     } while(packet->stream_index!=audioStream);

  //      bytesRemaining=packet->size;
 //       rawData=packet->data;
    }

loop_exit:

        // 解码最后一帧的余下部分
        bytesDecoded = avcodec_decode_audio4(pCodecCtx, pFrame, &frameFinished, packet);

        // 释放最后一个包
    if(packet->data!=NULL)
        av_free_packet(packet);

    return frameFinished!=0;
}
