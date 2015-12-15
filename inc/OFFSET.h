#ifndef ___OFFSET_H_INC___
#define ___OFFSET_H_INC___

#include "definitions.h"
#include "DeviceInterface.h"

class OFFSET
{
private:
	OF_BOOL disableGC, isTotalFormatting;
	DeviceInterface* di;
	OF_UINT freeSegmentsCount;
	OF_ULONG lastGeneratedObjectHandle;
#if OF_CACHE_L1
	OF_BYTE segStatusCache[
#if OF_CACHE_L2
#if OF_CACHE_L3
#if OF_CACHE_L4
	OF_SEGMENT_MAX_COUNT*2
#else
	OF_SEGMENT_MAX_COUNT
#endif
#else
	OF_SEGMENT_MAX_COUNT/2
#endif
#else
	OF_SEGMENT_MAX_COUNT/4
#endif
	];
#endif
	OF_UINT_PTR oHandles;
	OF_UINT oHandlesCount;

public:
	OFFSET(DeviceInterface* di, OF_BOOL autoFormat = OF_FALSE);
	~OFFSET();

	OF_BOOL format(OF_BOOL formatMetaPages = OF_FALSE);
	OF_VOID gc();
	
	OF_ULONG getTotalMemory();
	OF_ULONG getFreeMemory(OF_BOOL doGC);
	OF_BOOL canAllocateMemory(OF_ULONG requiredMemory, OF_ULONG attrCount);
	
	OF_BOOL iterateOnObjects(OF_ULONG_PTR itr, OF_OBJECT_HANDLE_PTR phObj, OF_BOOL bypassLogin=OF_FALSE, OF_BOOL onlyPrivateObjects=OF_FALSE, OF_BOOL onlyPublicObjects=OF_FALSE);
	OF_BOOL iterateOnObjectAttributes(OF_OBJECT_HANDLE hObj, OF_ULONG_PTR itr, OF_UINT_PTR attItr, OF_ATTRIBUTE_TYPE* pAttrType=NULL_PTR, OF_BOOL bypassLogin=OF_FALSE);
	OF_BOOL objectExists(OF_OBJECT_HANDLE hObj, OF_BOOL bypassLogin=OF_FALSE);
	OF_ULONG getObjectSize(OF_OBJECT_HANDLE hObj);
	OF_ULONG getPureObjectSize(OF_OBJECT_HANDLE hObj);
	OF_RV setObjectAttribute(OF_OBJECT_HANDLE_PTR pObj, OF_ATTRIBUTE_TYPE attrType, OF_VOID_PTR pAttrValue, OF_ULONG ulAttrValueLen, OF_BOOL isObjectPrivate = OF_FALSE, OF_BOOL isAttrSensitive = OF_FALSE);
	OF_RV getObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType, OF_ULONG_PTR pulAttrValueLen, OF_VOID_PTR pAttrValue);
	OF_RV deleteObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType);
	OF_UINT getObjectAttributeCount(OF_OBJECT_HANDLE hObj);
	OF_RV duplicateObject(OF_OBJECT_HANDLE hObj, OF_OBJECT_HANDLE_PTR phNewObj, OF_BYTE_PTR pNewTemplateStream = NULL_PTR);
	OF_RV destroyObject(OF_OBJECT_HANDLE hObj, OF_BOOL bypassLogin = OF_FALSE);

private:	
	OF_VOID initialize();
	OF_VOID transferPage(OF_UINT srcPageIndex, OF_UINT dstPageIndex, OF_UINT_PTR srcValidCount, OF_UINT_PTR srcInvalidCount, OF_UINT_PTR dstValidCount, OF_UINT_PTR dstFreeCount);
	OF_BOOL formatPage(OF_UINT pageIndex, OF_BOOL isDataPage = OF_TRUE, OF_BOOL forceFormat = OF_FALSE);
	
	inline OF_BYTE getMinimizedSegmentCount(OF_UINT segmentGlobalIndex, OF_BOOL useCache=OF_TRUE);
#if OF_CACHE_L1
	inline OF_BYTE getSegmentStatus(OF_UINT segmentGlobalIndex);
	inline OF_VOID setSegmentStatus(OF_UINT segmentGlobalIndex, OF_BYTE status, OF_BYTE segCount = 1
#if OF_CACHE_L3
	, OF_UINT segNum = 0x000F
#if OF_CACHE_L4
	, OF_OBJECT_HANDLE oHandle = OF_INVALID_OBJECT_HANDLE, OF_ATTRIBUTE_TYPE attrType  = OF_INVALID_ATTRIBUTE_TYPE
#endif
#endif
	);
	inline OF_BYTE getSegmentStatusItr(OF_UINT itr, OF_BYTE segLen);
	inline OF_VOID setSegmentStatusItr(OF_UINT itr, OF_BYTE status, OF_BYTE segLen
#if OF_CACHE_L3
	, OF_UINT segNum = 0x000F
#if OF_CACHE_L4
	, OF_OBJECT_HANDLE oHandle = OF_INVALID_OBJECT_HANDLE, OF_ATTRIBUTE_TYPE attrType = OF_INVALID_ATTRIBUTE_TYPE
#endif
#endif
	);
#if OF_CACHE_L2
	inline OF_BYTE getSegmentCount(OF_UINT segmentGlobalIndex);
#if OF_CACHE_L3
	inline OF_BYTE getSegmentNumModulus(OF_UINT segmentGlobalIndex);
#if OF_CACHE_L4
	inline OF_BYTE getObjHandleModulus(OF_UINT segmentGlobalIndex);
	inline OF_BYTE getObjAttrTypeModulus(OF_UINT segmentGlobalIndex);
#endif
#endif
#endif
#endif

	OF_BOOL allocateNewObjectHandle(OF_OBJECT_HANDLE_PTR pObj);
	OF_BOOL isPrivateObject(OF_OBJECT_HANDLE hObj);
	OF_UINT getObjectSegmentCount(OF_OBJECT_HANDLE hObj);
	OF_BOOL freeObjectSegment(ObjectSegment* os);
	OF_BOOL writeSegment(ObjectSegment* os, OF_BYTE usedContentLen, OF_BOOL doGC=OF_TRUE);
	OF_BYTE iterateOnSegments(OF_UINT_PTR itr, OF_UINT_PTR begItr, OF_UINT segmentNumber = 0xFFFF, OF_OBJECT_HANDLE oHandle = OF_INVALID_OBJECT_HANDLE, OF_ATTRIBUTE_TYPE attrType = OF_INVALID_ATTRIBUTE_TYPE, OF_BOOL onlyValidSegments = OF_FALSE, OF_BOOL onlyNonFreeSegments = OF_FALSE, OF_BOOL onlyFreeSegments = OF_FALSE, OF_BYTE requiredSize = 0, OF_BOOL onlyPrivateSegments = OF_FALSE, OF_BOOL onlyPublicSegments = OF_FALSE);
	OF_BYTE iterateOnObjectAttributeSegments(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType, OF_ULONG_PTR itr);
	OF_BYTE iterateOnObjectTemplateStreamAtrributes(OF_ATTRIBUTE_PTR pAttr, const OF_BYTE_PTR pStreamValue, OF_ULONG_PTR itr);

	OF_RV validateObjectAttributeValue(OF_ATTRIBUTE_TYPE attrType, OF_VOID_PTR pAttrValue, OF_ULONG_PTR pulAttrValueLen);
	
	OF_BOOL beginDeletingObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType = (OF_ATTRIBUTE_TYPE)OF_CONST_FREE_WORD_VALUE, OF_BYTE flags = (OF_BYTE)OF_CONST_FREE_QWORD_VALUE);
	OF_VOID endDeletingObjectAttribute();
	
	OF_BOOL beginFormattingPage(OF_UINT pageIndex, OF_UINT isDataPage);
	OF_VOID endFormattingPage();
	
	OF_BOOL addOHandle(OF_OBJECT_HANDLE hObject);
	OF_BOOL deleteOHandle(OF_OBJECT_HANDLE hObject);
};

#endif /*  ___OFFSET_H_INC___  */
