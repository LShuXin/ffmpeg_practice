#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#define STREAM_DURATION 10.0
#define STREAM_FRAME_RATE 25
#define STREAM_PIX_FMT AV_PIX_FMT_YUV420P

int main(int argc, char* argv[]) {
  printf("%d--%d\n", 100, 200);
  return 0;
}


