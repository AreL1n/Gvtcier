#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef char ALCchar;
typedef char ALCboolean;
typedef char ALCubyte;
typedef char ALCbyte;
typedef int ALCint;
typedef unsigned int ALCuint;
typedef int ALCsizei;
typedef int ALCenum;
typedef void ALCdevice;
typedef void ALCcontext;

typedef char ALboolean;
typedef char ALchar;
typedef signed char ALbyte;
typedef unsigned char ALubyte;
typedef short ALshort;
typedef unsigned short ALushort;
typedef int ALint;
typedef unsigned int ALuint;
typedef int ALsizei;
typedef int ALenum;
typedef float ALfloat;
typedef double ALdouble;

#define AL_NO_ERROR 0
#define AL_FALSE 0
#define AL_TRUE 1
#define AL_SOURCE_RELATIVE 0x0202
#define AL_CONE_INNER_ANGLE 0x1001
#define AL_CONE_OUTER_ANGLE 0x1002
#define AL_PITCH 0x1003
#define AL_POSITION 0x1004
#define AL_DIRECTION 0x1005
#define AL_LOOPING 0x1007
#define AL_BUFFER 0x1009
#define AL_GAIN 0x100A
#define AL_MIN_GAIN 0x100D
#define AL_MAX_GAIN 0x100E
#define AL_ORIENTATION 0x100F
#define AL_SOURCE_STATE 0x1010
#define AL_INITIAL 0x1011
#define AL_PLAYING 0x1012
#define AL_PAUSED 0x1013
#define AL_STOPPED 0x1014
#define AL_BUFFERS_QUEUED 0x1015
#define AL_BUFFERS_PROCESSED 0x1016
#define AL_SOURCE_TYPE 0x1027
#define AL_STATIC 0x1028
#define AL_STREAMING 0x1029
#define AL_UNDETERMINED 0x1030
#define AL_FORMAT_MONO8 0x1100
#define AL_FORMAT_MONO16 0x1101
#define AL_FORMAT_STEREO8 0x1102
#define AL_FORMAT_STEREO16 0x1103
#define AL_REFERENCE_DISTANCE 0x1020
#define AL_ROLLOFF_FACTOR 0x1021
#define AL_MAX_DISTANCE 0x1023
#define AL_FREQUENCY 0x2001
#define AL_BITS 0x2002
#define AL_CHANNELS 0x2003
#define AL_SIZE 0x2004
#define AL_SEC_OFFSET 0x1024
#define AL_VENDOR 0xB001
#define AL_VERSION 0xB002
#define AL_RENDERER 0xB003

#define ALC_DEFAULT_DEVICE_SPECIFIER 0x1004
#define ALC_DEVICE_SPECIFIER 0x1005
#define ALC_FREQUENCY 0x1007
#define ALC_REFRESH 0x1008
#define ALC_SYNC 0x1009
#define ALC_MONO_SOURCES 0x1010
#define ALC_STEREO_SOURCES 0x1011
#define ALC_MAJOR_VERSION 0x1000
#define ALC_MINOR_VERSION 0x1001
#define ALC_ATTRIBUTES_SIZE 0x1002
#define ALC_ALL_ATTRIBUTES 0x1003
#define ALC_EXTENSIONS 0x1006

#ifdef __cplusplus
}
#endif
