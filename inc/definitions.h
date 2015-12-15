#ifndef ___DEFINITIONS_H_INC___
#define ___DEFINITIONS_H_INC___

#include "types.h"

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif
#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

#define OF_FALSE 0
#define OF_TRUE 1
#define NULL_PTR 0

#define OFR_OK		0
#define OFR_OBJECT_HANDLE_INVALID 1
#define OFR_DEVICE_ERROR 2
#define OFR_DEVICE_MEMORY 3
#define OFR_DEVICE_BUSY 4
#define OFR_NOT_AUTHENTICATED 5
#define OFR_FUNCTION_FAILED 6
#define OFR_FUNCTION_INVALID_PARAM 7
#define OFR_SMALL_BUFFER 8

/* meta ids */
#define OFMID_FORMATTING_PAGE 1
#define OFMID_DELETING_OBJECT_ATTR_0 2
#define OFMID_FLAGS_0 3

#define OF_NORMAL_OBJECT_HANDLE_MASK 0x0000
#define OF_INVALID_OBJECT_HANDLE 0xFFFF
#define OF_INVALID_ATTRIBUTE_TYPE 0xFFFF
#define OF_INVALID_SEGMENT_NUMBDER 0xFFFF

#define SIZEOF_VI OF_HW_MIN_RW_SIZE//sizeof(ObjectSegment::validationIndicator)
#define SIZEOF_OH sizeof(OF_OBJECT_HANDLE)//sizeof(ObjectSegment::objectHandler)
#define SIZEOF_AT sizeof(OF_ATTRIBUTE_TYPE)
#define SIZEOF_SF sizeof(OF_BYTE)//sizeof(ObjectSegment::segmentFlags)
#if OF_OBJECT_MAX_ALLOWED_SEGMENTS_PER_ATTRIBUTE <= 0xFF
#define SIZEOF_SN sizeof(OF_BYTE)//sizeof(ObjectSegment::segmentNumber)
#define _segNum *(OF_BYTE_PTR)
#else
#define SIZEOF_SN sizeof(OF_UINT)//sizeof(ObjectSegment::segmentNumber)
#define _segNum *(OF_UINT_PTR)
#endif

#if OF_SEGMENT_MAX_COUNT_PER_PAGE <= 0xFF
	#define _getItrPageNum(itr) (OF_BYTE)((itr) & 0x00FF)
	#define _getItrSegNumInPage(itr) (OF_BYTE)((itr)>>8)
	#define _setItrPageNum(itr, pn) (itr)=(pn | ((itr) & 0xFF00))
	#define _setItrSegNumInPage(itr, sn) (itr)=((sn<<8) | ((itr) & 0x00FF))
#elif OF_SEGMENT_MAX_COUNT_PER_PAGE <= 0xFFFF
	#define _getItrPageNum(itr) (OF_UINT)((itr) & 0x001F)
	#define _getItrSegNumInPage(itr) (OF_UINT)((itr)>>5)
	#define _setItrPageNum(itr, pn) (itr)=((pn) | ((itr) & 0xFFE0))
	#define _setItrSegNumInPage(itr, sn) (itr)=((sn<<5) | ((itr) & 0x001F))
#else
	#error "OF_SEGMENT_MAX_COUNT_PER_PAGE has illegal value"
#endif

#define SEGMENT_FLAG_MASK_TRANSMIT_COUNTER 0x03
#define SEGMENT_FLAG_MASK_LEN_GROUP 0x0C
#define SEGMENT_FLAG_MASK_LEN_MIN 0x00
#define SEGMENT_FLAG_MASK_LEN_AVG 0x04
#define SEGMENT_FLAG_MASK_LEN_MAX 0x08
#define SEGMENT_FLAG_MASK_LAST_ATTR 0x10
#define SEGMENT_FLAG_MASK_PRIVATE 0x20
#define SEGMENT_FLAG_MASK_ENCRYPTED 0x40
#define SEGMENT_STATUS_FREE 0x3
#define SEGMENT_STATUS_VALID_FAS 0x2 /*First Attribute's Segment (the first segment of an attibute)*/
#define SEGMENT_STATUS_VALID 0x1
#define SEGMENT_STATUS_INVALID 0x0
#define _getSegmentCount(sf) ((sf & SEGMENT_FLAG_MASK_LEN_GROUP)==0?1:((sf & SEGMENT_FLAG_MASK_LEN_GROUP)==4?2:4))
#define _getSegmentSize(sf) (_getSegmentCount(sf)*OF_OBJECT_SEGMENT_MIN_SIZE)

#define OBJECT_HANDLE_LIST_GROW_SIZE 32

#endif /*  ___DEFINITIONS_H_INC___  */
