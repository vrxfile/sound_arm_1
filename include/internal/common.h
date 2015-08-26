#ifndef TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_
#define TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_

#include <stdbool.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


typedef struct ImageDescription
{
  size_t m_width;
  size_t m_height;
  size_t m_lineLength;
  size_t m_imageSize;
  uint32_t m_format;
} ImageDescription;

typedef struct TargetDetectParams
{
	unsigned int m_volumeCoefficient;
	unsigned int m_micDistance;
	unsigned int m_windowSize;
	unsigned int m_numSamples;
} TargetDetectParams;

typedef struct TargetDetectCommand
{
  int m_cmd;
} TargetDetectCommand;

typedef struct TargetLocation
{
	int m_targetAngle;
	unsigned int m_targetLeftVolume;
	unsigned int m_targetRightVolume;
} TargetLocation;


#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus


#endif // !TRIK_V4L2_DSP_FB_INTERNAL_COMMON_H_
