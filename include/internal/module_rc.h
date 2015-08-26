#ifndef TRIK_V4L2_DSP_FB_INTERNAL_MODULE_RC_H_
#define TRIK_V4L2_DSP_FB_INTERNAL_MODULE_RC_H_

#include <stdbool.h>

#include "internal/common.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


typedef struct RCConfig // what user wants to set
{
  const char* m_fifoInput;
  const char* m_fifoOutput;
  bool m_videoOutEnable;
} RCConfig;

typedef struct RCInput
{
  int                      m_fifoInputFd;
  char*                    m_fifoInputName;
  char*                    m_fifoInputReadBuffer;
  size_t                   m_fifoInputReadBufferSize;
  size_t                   m_fifoInputReadBufferUsed;

  int                      m_fifoOutputFd;
  char*                    m_fifoOutputName;

  bool                     m_targetDetectParamsUpdated;

  unsigned int				m_volumeCoefficient;
  unsigned int				m_micDistance;
  unsigned int				m_windowSize;
  unsigned int				m_numSamples;

  bool                     m_targetDetectCommandUpdated;
  int                      m_targetDetectCommand;

  bool                     m_videoOutParamsUpdated;
  bool                     m_videoOutEnable;
} RCInput;




int rcInputInit(bool _verbose);
int rcInputFini();

int rcInputOpen(RCInput* _rc, const RCConfig* _config);
int rcInputClose(RCInput* _rc);
int rcInputStart(RCInput* _rc);
int rcInputStop(RCInput* _rc);

int rcInputReadFifoInput(RCInput* _rc);

int rcInputGetTargetDetectParams(RCInput* _rc, TargetDetectParams* _targetDetectParams);
int rcInputGetTargetDetectCommand(RCInput* _rc, TargetDetectCommand* _targetDetectCommand);

int rcInputGetVideoOutParams(RCInput* _rc, bool *_videoOutEnable);

int rcInputUnsafeReportTargetLocation(RCInput* _rc, const TargetLocation* _targetLocation);
int rcInputUnsafeReportTargetDetectParams(RCInput* _rc, const TargetDetectParams* _targetDetectParams);

#ifdef __cplusplus
} // extern "C"
#endif // __cplusplus

#endif // !TRIK_V4L2_DSP_FB_INTERNAL_MODULE_RC_H_
