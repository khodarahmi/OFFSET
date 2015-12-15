#ifndef ___TYPES_H_INC___
#define ___TYPES_H_INC___

#include "settings.h"
#include <stdint.h>

#define OF_PTR *

typedef uint8_t OF_BOOL;
typedef uint8_t OF_BYTE;
typedef int16_t OF_INT;
typedef uint16_t OF_UINT;
typedef int32_t OF_LONG;
typedef uint32_t OF_ULONG;
typedef uint16_t OF_ATTRIBUTE_TYPE;
typedef uint16_t OF_OBJECT_HANDLE;
typedef uint16_t OF_METADATA_HANDLE;
typedef uint16_t OF_RV;
typedef void OF_VOID;

typedef OF_BOOL OF_PTR OF_BOOL_PTR;
typedef OF_BYTE OF_PTR OF_BYTE_PTR;
typedef OF_INT OF_PTR OF_INT_PTR;
typedef OF_UINT OF_PTR OF_UINT_PTR;
typedef OF_LONG OF_PTR OF_LONG_PTR;
typedef OF_ULONG OF_PTR OF_ULONG_PTR;
typedef OF_ATTRIBUTE_TYPE OF_PTR OF_ATTRIBUTE_TYPE_PTR;
typedef OF_OBJECT_HANDLE OF_PTR OF_OBJECT_HANDLE_PTR;
typedef OF_METADATA_HANDLE OF_PTR OF_METADATA_HANDLE_PTR;
typedef OF_VOID OF_PTR OF_VOID_PTR;

typedef struct ObjectSegment {
#if OF_HW_MIN_RW_SIZE == 4
	OF_ULONG validationIndicator;//if((char)vi=0xFFFFFFFF) then segment is valid
#elif OF_HW_MIN_RW_SIZE == 2
	OF_UINT validationIndicator;//if((char)vi=0xFFFF) then segment is valid
#elif OF_HW_MIN_RW_SIZE == 1
	OF_BYTE validationIndicator;//if(vi=0xFF) then segment is valid
#else
	#error "OF_HW_MIN_RW_SIZE has illegal value"
#endif
	OF_OBJECT_HANDLE objectHandler;
	OF_ATTRIBUTE_TYPE attrType;
	OF_BYTE segmentFlags;// sf[2-7]: reserved, sf[0-1]: transmit counter
#if OF_OBJECT_MAX_ALLOWED_SEGMENTS_PER_ATTRIBUTE <= 0xFF
	OF_BYTE segmentNumber;
#elif OF_OBJECT_MAX_ALLOWED_SEGMENTS_PER_ATTRIBUTE <= 0xFFFF
	OF_UINT segmentNumber;
#else
	#error "OF_OBJECT_MAX_ALLOWED_SEGMENTS_PER_ATTRIBUTE has illegal value"
#endif
	OF_BYTE content[OF_OBJECT_SEGMENT_CONTENT_MAX_SIZE];
} ObjectSegment;// each segment size is OF_OBJECT_SEGMENT_MAX_SIZE bytes

typedef struct
{
	OF_ATTRIBUTE_TYPE type;
	OF_ULONG ulValueLen;
	OF_VOID_PTR pValue;
}OF_ATTRIBUTE;

typedef OF_ATTRIBUTE OF_PTR OF_ATTRIBUTE_PTR;

#endif /*  ___TYPES_H_INC___  */