#include "config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <sys/select.h>
#include <alsa/asoundlib.h>

#include "internal/thread_audio.h"
#include "internal/runtime.h"
#include "internal/module_ce.h"
#include "internal/module_fb.h"

#define FrameSourceSize		153600
#define ImageSourceFormat	1448695129

#define NO_ERROR		0
#define OPEN_ERROR		1
#define MALLOC_ERROR	2
#define ANY_ERROR		3
#define ACCESS_ERROR	4
#define FORMAT_ERROR	5
#define RATE_ERROR		6
#define CHANNELS_ERROR	7
#define PARAMS_ERROR	8
#define PREPARE_ERROR	9
#define FOPEN_ERROR		10
#define FCLOSE_ERROR	11
#define SNDREAD_ERROR	12
#define START_ERROR		13

#define SND_BUF_SIZE	512

static char* snd_device = "default";
snd_pcm_t* capture_handle;
snd_pcm_hw_params_t* hw_params;
snd_pcm_info_t* s_info;
unsigned int srate = 44100;
unsigned int nchan = 2;

volatile long long proc_frames = 0;

/// Open and init default sound card params
int init_soundcard()
{
	int err = 0;

	if ((err = snd_pcm_open(&capture_handle, snd_device,
			SND_PCM_STREAM_CAPTURE, 0)) < 0)
	{
		fprintf(stderr, "cannot open audio device %s (%s, %d)\n",
				snd_device, snd_strerror(err), err);
		return OPEN_ERROR;
	}

	if ((err = snd_pcm_hw_params_malloc(&hw_params)) < 0)
	{
		fprintf(
				stderr,
				"cannot allocate hardware parameter structure (%s, %d)\n",
				snd_strerror(err), err);
		return MALLOC_ERROR;
	}

	if ((err = snd_pcm_hw_params_any(capture_handle, hw_params)) < 0)
	{
		fprintf(
				stderr,
				"cannot initialize hardware parameter structure (%s, %d)\n",
				snd_strerror(err), err);
		return ANY_ERROR;
	}

	if ((err = snd_pcm_hw_params_set_access(capture_handle, hw_params,
			SND_PCM_ACCESS_RW_INTERLEAVED)) < 0)
	{
		fprintf(stderr, "cannot set access type (%s, %d)\n",
				snd_strerror(err), err);
		return ACCESS_ERROR;
	}

	if ((err = snd_pcm_hw_params_set_format(capture_handle, hw_params,
			SND_PCM_FORMAT_S16_LE)) < 0)
	{
		fprintf(stderr, "cannot set sample format (%s, %d)\n",
				snd_strerror(err), err);
		return FORMAT_ERROR;
	}

	if ((err = snd_pcm_hw_params_set_rate_near(capture_handle, hw_params,
			&srate, 0)) < 0)
	{
		fprintf(stderr, "cannot set sample rate (%s, %d)\n",
				snd_strerror(err), err);
		return RATE_ERROR;
	}

	if ((err = snd_pcm_hw_params_set_channels(capture_handle, hw_params, nchan))
			< 0)
	{
		fprintf(stderr, "cannot set channel count (%s, %d)\n",
				snd_strerror(err), err);
		return CHANNELS_ERROR;
	}

	if ((err = snd_pcm_hw_params(capture_handle, hw_params)) < 0)
	{
		fprintf(stderr, "cannot set parameters (%s, %d)\n",
				snd_strerror(err), err);
		return PARAMS_ERROR;
	}

	if ((err = snd_pcm_prepare(capture_handle)) < 0)
	{
		fprintf(
				stderr,
				"cannot prepare audio interface for use (%s, %d)\n",
				snd_strerror(err), err);
		return PREPARE_ERROR;
	}

	// Start reading data from sound card
	if ((err = snd_pcm_start(capture_handle)) < 0)
	{
		fprintf(stderr, "cannot start soundcard (%s, %d)\n", snd_strerror(err),
				err);
		return START_ERROR;
	}

	snd_pcm_hw_params_free(hw_params);

	return NO_ERROR;
}

/// Close soundcard
int close_soundcard()
{
	return snd_pcm_close(capture_handle);
}

// Measure speed in FPS
int InputReportFPS(long long _ms)
{
	long long kfps = (proc_frames * 1000 * 1000) / _ms;

	fprintf(stderr, "Process speed %llu.%03llu fps\n", kfps/1000, kfps%1000);
	fprintf(stderr, "Processed %llu frames\n", proc_frames);
	proc_frames = 0;

	return 0;
}

// Audio thread loop cycle
static int threadAudioSelectLoop(Runtime* _runtime, CodecEngine* _ce, FBOutput* _fb)
{
  int res = 0;
  int err = 0;

  void* frameDstPtr;
  size_t frameDstSize;

  const void* frameSrcPtr;
  size_t frameSrcSize;

  char buffer1[FrameSourceSize]; // Buffer with image

  //uint32_t ncount = FrameSourceSize / (SND_BUF_SIZE * nchan * 2);
  uint32_t ncount = 32; // x 512 * 2 * 2 = 65536 - number of readings data from siound card

  char wav_data[SND_BUF_SIZE * nchan * 2];

  TargetDetectParams  targetDetectParams;
  TargetDetectCommand targetDetectCommand;
  TargetLocation      targetLocation;
  TargetDetectParams  targetDetectParamsResult;

  if (_runtime == NULL || _ce == NULL || _fb == NULL)
    return EINVAL;

  if ((res = fbOutputGetFrame(_fb, &frameDstPtr, &frameDstSize)) != 0)
  {
    fprintf(stderr, "fbOutputGetFrame() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeGetTargetDetectParams(_runtime, &targetDetectParams)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectParams() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeFetchTargetDetectCommand(_runtime, &targetDetectCommand)) != 0)
  {
    fprintf(stderr, "runtimeGetTargetDetectCommand() failed: %d\n", res);
    return res;
  }

  if ((res = runtimeGetVideoOutParams(_runtime, &(_ce->m_videoOutEnable))) != 0)
  {
    fprintf(stderr, "runtimeGetVideoOutParams() failed: %d\n", res);
    return res;
  }

  size_t frameDstUsed = frameDstSize;
  frameSrcSize = FrameSourceSize;


  // This is a buffer to send to DSP
  memset(buffer1, 0, frameSrcSize);

  // Reading data
  for (uint32_t i = 0; i < ncount; i++)
	{
		if ((err = snd_pcm_readi(capture_handle, wav_data, SND_BUF_SIZE)) != SND_BUF_SIZE)
		{
			fprintf(stderr, "read from audio interface failed (%s, %d)\n",
					snd_strerror(err), err);
			if (err == -32) // Broken pipe
			{
				if ((err = snd_pcm_prepare(capture_handle)) < 0)
				{
					fprintf(stderr,	"cannot prepare audio interface for use (%s, %d)\n",
							snd_strerror(err), err);
					return PREPARE_ERROR;
				}
			}
			else
			{
				return SNDREAD_ERROR;
			}

		}
		for (uint32_t j = 0; j < (SND_BUF_SIZE * nchan * 2); j++)
		{
			buffer1[i * SND_BUF_SIZE * nchan * 2 + j] = wav_data[j];
		}
	}

  frameSrcPtr = buffer1;

  if ((res = codecEngineTranscodeFrame(_ce,
                                       frameSrcPtr, frameSrcSize,
                                       frameDstPtr, frameDstSize, &frameDstUsed,
                                       &targetDetectParams,
                                       &targetDetectCommand,
                                       &targetLocation,
                                       &targetDetectParamsResult)) != 0)
  {
    fprintf(stderr, "codecEngineTranscodeFrame(%p[%zu] -> %p[%zu]) failed: %d\n",
            frameSrcPtr, frameSrcSize, frameDstPtr, frameDstSize, res);
    return res;
  }

  if ((res = fbOutputPutFrame(_fb)) != 0)
  {
    fprintf(stderr, "fbOutputPutFrame() failed: %d\n", res);
    return res;
  }

  switch (targetDetectCommand.m_cmd)
  {
    case 1:
      if ((res = runtimeReportTargetDetectParams(_runtime, &targetDetectParamsResult)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetDetectParams() failed: %d\n", res);
        return res;
      }
      break;

    case 0:
    default:
      if ((res = runtimeReportTargetLocation(_runtime, &targetLocation)) != 0)
      {
        fprintf(stderr, "runtimeReportTargetLocation() failed: %d\n", res);
        return res;
      }
      break;
  }

  proc_frames ++;

  return 0;
}

// Audio thread
void* threadAudio(void* _arg)
{
	intptr_t exit_code = 0;
	Runtime* runtime = (Runtime*)_arg;
	CodecEngine* ce;
	FBOutput* fb;
	int res = 0;

	struct timespec last_fps_report_time;

	ImageDescription srcImageDesc;
	ImageDescription dstImageDesc;

	if (runtime == NULL)
	{
		exit_code = EINVAL;
		goto exit;
	}

	if ((ce   = runtimeModCodecEngine(runtime)) == NULL
			|| (fb   = runtimeModFBOutput(runtime))    == NULL)
	{
		exit_code = EINVAL;
		goto exit;
	}

	if ((res = codecEngineOpen(ce, runtimeCfgCodecEngine(runtime))) != 0)
	{
		fprintf(stderr, "codecEngineOpen() failed: %d\n", res);
		exit_code = res;
		goto exit;
	}

	if ((res = fbOutputOpen(fb, runtimeCfgFBOutput(runtime))) != 0)
	{
		fprintf(stderr, "fbOutputOpen() failed: %d\n", res);
		exit_code = res;
		goto exit_ce_close;
	}

	if ((res = fbOutputGetFormat(fb, &dstImageDesc)) != 0)
	{
		fprintf(stderr, "fbOutputGetFormat() failed: %d\n", res);
		exit_code = res;
		goto exit_fb_close;
	}

	memcpy(&srcImageDesc, &dstImageDesc, sizeof(srcImageDesc));
	srcImageDesc.m_format = ImageSourceFormat;
	srcImageDesc.m_imageSize = FrameSourceSize;

	if ((res = codecEngineStart(ce, runtimeCfgCodecEngine(runtime), &srcImageDesc, &dstImageDesc)) != 0)
	{
		fprintf(stderr, "codecEngineStart() failed: %d\n", res);
		exit_code = res;
		goto exit_fb_close;
	}

	if ((res = fbOutputStart(fb)) != 0)
	{
		fprintf(stderr, "fbOutputStart() failed: %d\n", res);
		exit_code = res;
		goto exit_ce_close;
	}

	if ((res = clock_gettime(CLOCK_MONOTONIC, &last_fps_report_time)) != 0)
	{
		fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %d\n", errno);
		exit_code = res;
		goto exit_fb_stop;
	}


	printf("Open default soundcard\n");
	init_soundcard();

	printf("Entering audio thread loop\n");
	while (!runtimeGetTerminate(runtime))
	{
		struct timespec now;
		long long last_fps_report_elapsed_ms;

		if ((res = clock_gettime(CLOCK_MONOTONIC, &now)) != 0)
		{
			fprintf(stderr, "clock_gettime(CLOCK_MONOTONIC) failed: %d\n", errno);
			exit_code = res;
			goto exit_fb_stop;
		}

		last_fps_report_elapsed_ms = (now.tv_sec  - last_fps_report_time.tv_sec )*1000
				+ (now.tv_nsec - last_fps_report_time.tv_nsec)/1000000;

		if (last_fps_report_elapsed_ms >= 10*1000)
		{
			last_fps_report_time.tv_sec += 10;

			if ((res = codecEngineReportLoad(ce, last_fps_report_elapsed_ms)) != 0)
				fprintf(stderr, "codecEngineReportLoad() failed: %d\n", res);

			if ((res = InputReportFPS(last_fps_report_elapsed_ms)) != 0)
				fprintf(stderr, "InputReportFPS() failed: %d\n", res);

		}

		if ((res = threadAudioSelectLoop(runtime, ce, fb)) != 0)
		{
			fprintf(stderr, "threadAudioSelectLoop() failed: %d\n", res);
			exit_code = res;
			goto exit_fb_stop;
		}
	}
	printf("Left audio thread loop\n");

	exit_fb_stop:
	if ((res = fbOutputStop(fb)) != 0)
		fprintf(stderr, "fbOutputStop() failed: %d\n", res);

	exit_ce_stop:
	if ((res = codecEngineStop(ce)) != 0)
		fprintf(stderr, "codecEngineStop() failed: %d\n", res);

	exit_fb_close:
	if ((res = fbOutputClose(fb)) != 0)
		fprintf(stderr, "fbOutputClose() failed: %d\n", res);

	exit_ce_close:
	if ((res = codecEngineClose(ce)) != 0)
		fprintf(stderr, "codecEngineClose() failed: %d\n", res);

	exit:
	runtimeSetTerminate(runtime);

	printf("Close default soundcard\n");
	close_soundcard();

	return (void*)exit_code;
}





