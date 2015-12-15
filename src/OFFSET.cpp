#include "OFFSET.h"

class GCUtil
{
public:
	static OF_VOID sort(OF_UINT list[][2], OF_BOOL ascending)
	{
		OF_BOOL changed = OF_TRUE;
		for(OF_BYTE i=0;changed && i<OF_HW_DATAPAGE_COUNT;i++)
		{
			changed = OF_FALSE;
			for(OF_BYTE j=0;j<OF_HW_DATAPAGE_COUNT-1;j++)
			{
				if((ascending && list[j][1] > list[j+1][1]) || (!ascending && list[j][1] < list[j+1][1]))
				{
					OF_UINT temp = list[j][0];
					list[j][0] = list[j+1][0];
					list[j+1][0] = temp;
					temp = list[j][1];
					list[j][1] = list[j+1][1];
					list[j+1][1] = temp;
					changed = OF_TRUE;
				}
			}
		}
	}

	static OF_BOOL areCoSector(DeviceInterface* di, OF_BYTE pageIndex1, OF_BYTE pageIndex2)
	{
		return pageIndex1 == pageIndex2 || getPageSectorIndex(di, pageIndex1) == getPageSectorIndex(di, pageIndex2);
	}

	static OF_UINT getPageSectorIndex(DeviceInterface* di, OF_UINT pageIndex)
	{
		OF_UINT pageOffset = (pageIndex + OF_HW_FIRST_DATAPAGE_OFFSET)*OF_HW_PAGE_SIZE;
		for (OF_UINT offset = 0, i = 0; offset < OF_CONST_TOTAL_SIZE; i++)
		{
			OF_UINT sectorSize = di->getSectorSize(i);
			if (offset >= pageOffset && offset < pageOffset + sectorSize)
			{
				return i;
			}
			offset += sectorSize;
		}
		return -1;
	}

	static OF_UINT countSectorValidPages(DeviceInterface* di, OF_BYTE sectorIndex, OF_UINT minValid[][2])
	{
		OF_UINT validCount = 0;
		for(OF_BYTE i=0;i<OF_HW_DATAPAGE_COUNT;i++)
		{
			if(getPageSectorIndex(di, minValid[i][0]) == sectorIndex)
			{
				validCount += minValid[i][1];
			}
		}
		return validCount;
	}

	static OF_UINT countSectorInvalidPages(DeviceInterface* di, OF_BYTE sectorIndex, OF_UINT minValid[][2], OF_UINT maxFree[][2])
	{
		OF_UINT invalidCount = OF_SEGMENT_MAX_COUNT_PER_PAGE - countSectorValidPages(di, sectorIndex, minValid);
		for(OF_BYTE i=0;i<OF_HW_DATAPAGE_COUNT;i++)
		{
			if(getPageSectorIndex(di, maxFree[i][0]) == sectorIndex)
			{
				invalidCount -= maxFree[i][1];
			}
		}
		return invalidCount;
	}
	
	static OF_UINT countInvalid(OF_BYTE i, OF_UINT minValid[][2], OF_UINT maxFree[][2])
	{
		OF_UINT invalidCount = OF_SEGMENT_MAX_COUNT_PER_PAGE - minValid[i][1];
		for(OF_BYTE k=0;k<OF_HW_DATAPAGE_COUNT;k++)
		{
			if(maxFree[k][0] == minValid[i][0])
			{
				invalidCount -= maxFree[k][1];
				break;
			}
		}
		return invalidCount;
	}
	
	static OF_BOOL doesGCLead2FormatPage(DeviceInterface* di, OF_BYTE i, OF_UINT minValid[][2], OF_UINT maxFree[][2])
	{
		OF_UINT availOutOfSectorFree = 0;
		for(OF_BYTE j=0;j<OF_HW_DATAPAGE_COUNT;j++)
		{
			if(!GCUtil::areCoSector(di, (OF_BYTE) minValid[i][0], (OF_BYTE) maxFree[j][0]))
			{
				availOutOfSectorFree += maxFree[j][1];
				if(availOutOfSectorFree >= minValid[i][1])
				{
					return OF_TRUE;
				}
			}
		}
		return OF_FALSE;
	}
};

OFFSET::OFFSET(DeviceInterface* di, OF_BOOL autoFormat)
{
	this->di = di;
	this->lastGeneratedObjectHandle = 0;
	this->freeSegmentsCount = 0;
	this->disableGC = this->isTotalFormatting = OF_FALSE;
	
	this->oHandles = (OF_UINT_PTR) di->malloc(OBJECT_HANDLE_LIST_GROW_SIZE*sizeof(OF_UINT));
	this->oHandlesCount = OBJECT_HANDLE_LIST_GROW_SIZE;
	di->memset(this->oHandles, -1, OBJECT_HANDLE_LIST_GROW_SIZE*sizeof(OF_UINT));
	
	if(autoFormat)
	{
/*#if OF_CACHE_L1
		for(OF_UINT i=0;i<OF_SEGMENT_MAX_COUNT;i++)
		{
			setSegmentStatus(i, SEGMENT_STATUS_INVALID, 1);
		}
#endif*/
		format();
	}
	
	initialize();
}

OFFSET::~OFFSET()
{
	if (oHandles)
	{
		di->free(oHandles);
	}
	if(!this->di)
	{
		delete this->di;
	}
}

OF_VOID OFFSET::initialize()
{
	di->disableIRQ();//disable all interrupts
	
	//repairing power failure inconsistencies & setting lastGeneratedObjectHandle
	lastGeneratedObjectHandle = 0;
	freeSegmentsCount = 0;
	OF_UINT buf;
	OF_BYTE segCount = getMinimizedSegmentCount(0, OF_FALSE);
	OF_BYTE headerBuf[SIZEOF_VI+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF+SIZEOF_SN], dBuf[8];
	
	for(OF_UINT i=0;i<OF_SEGMENT_MAX_COUNT;i+=segCount)
	{
		segCount = getMinimizedSegmentCount(i, OF_FALSE);
		
		di->seekg(OF_CONST_DATABASE_ADDRESS+i*OF_SEGMENT_MIN_SIZE, OF_TRUE);
		di->read(headerBuf, sizeof(headerBuf));//reading segment header info
		if((unsigned char)headerBuf[0] == OF_CONST_FREE_QWORD_VALUE)
		{//valid segment
			buf = *(OF_UINT_PTR)(headerBuf+SIZEOF_VI);
			if(buf != OF_CONST_FREE_HWORD_VALUE)
			{//non-free segment
				if(!_segNum(headerBuf+SIZEOF_VI+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF))
				{
					addOHandle((OF_OBJECT_HANDLE)(buf | OF_NORMAL_OBJECT_HANDLE_MASK));
				}
				if(buf > lastGeneratedObjectHandle)
				{
					lastGeneratedObjectHandle = buf;
				}
#if OF_CACHE_L1
				setSegmentStatus(i, _segNum(headerBuf+SIZEOF_VI+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF) ? SEGMENT_STATUS_VALID : SEGMENT_STATUS_VALID_FAS, segCount
#if OF_CACHE_L3
					, _segNum(headerBuf+SIZEOF_VI+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF)
#if OF_CACHE_L4
					, buf, *(OF_ATTRIBUTE_TYPE*)(headerBuf+SIZEOF_VI+SIZEOF_OH)
#endif
#endif
					);
#endif
			}
			else
			{//free segment
#if OF_CACHE_L1
				setSegmentStatus(i, SEGMENT_STATUS_FREE, segCount);
#endif
				freeSegmentsCount++;
			}			
		}
#if OF_CACHE_L1
		else
		{//invalid segment
			setSegmentStatus(i, SEGMENT_STATUS_INVALID, segCount);
		}
#endif
	}
	
	if (di->readMetaData(OFMID_FORMATTING_PAGE, &buf, sizeof(buf)) == sizeof(buf) && buf != (OF_UINT)-1)
	{//continue intruppted (failed) page formating
		formatPage(buf>>8, buf & 0x00FF);
		endFormattingPage();
	}
	
	buf = 0xFFFF;
	if (di->readMetaData(OFMID_DELETING_OBJECT_ATTR_0, dBuf, sizeof(dBuf)) == sizeof(dBuf) && di->memcmp(dBuf, &buf, sizeof(buf)))
	{//continue intruppted (failed) object or object attribute deleting
		
		OF_UINT oHandle = *(OF_UINT_PTR)dBuf;
		OF_ATTRIBUTE_TYPE attrType = *(OF_ATTRIBUTE_TYPE*)(dBuf+sizeof(OF_UINT));
		OF_BYTE oldSegFlags = *(OF_BYTE_PTR)(dBuf+sizeof(OF_UINT)+sizeof(OF_ATTRIBUTE_TYPE)), segLen;
		OF_UINT itr = 0, cntItr = 0;
		
		while(OF_TRUE)
		{
			segLen = iterateOnSegments(&itr, &cntItr, (OF_UINT)-1, oHandle, attrType, OF_TRUE, OF_TRUE);
			if(!segLen)
			{
				break;
			}
			di->read(dBuf, SIZEOF_OH + SIZEOF_AT + SIZEOF_SF);
			if(*(OF_UINT_PTR)dBuf == oHandle && (attrType == (OF_ATTRIBUTE_TYPE)OF_CONST_FREE_WORD_VALUE || attrType == *(OF_ATTRIBUTE_TYPE*)(dBuf+SIZEOF_OH)) && (oldSegFlags == (OF_BYTE)OF_CONST_FREE_QWORD_VALUE || (oldSegFlags & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER) == ((*(OF_BYTE_PTR)(dBuf+SIZEOF_OH+SIZEOF_AT)) & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER)))
			{//invalidate segment
				di->seekp(-1 * (int)SIZEOF_VI);
				buf = 0;
				di->write((OF_BYTE_PTR)&buf, SIZEOF_VI);
#if OF_CACHE_L1
				setSegmentStatusItr(itr, SEGMENT_STATUS_INVALID, segLen);
#endif
			}
		}
	}

	gc();
}

OF_BOOL OFFSET::format(OF_BOOL formatMetaPages)
{
	OF_ULONG buf = (OF_ULONG)-1;
	di->writeMetaData(OFMID_FLAGS_0, &buf, sizeof(buf));
	
	isTotalFormatting = OF_TRUE;
	di->disableIRQ();//disable all interrupts
	freeSegmentsCount = 0;
	
#if OF_HW_METAPAGE_COUNT
	if(formatMetaPages)
	{
		for(OF_BYTE i=0;i<OF_HW_METAPAGE_COUNT;i++)
		{
			formatPage(i, OF_FALSE, OF_TRUE);
		}
	}
#endif
	
	for(OF_BYTE i=0;i<OF_HW_DATAPAGE_COUNT;i++)
	{
		formatPage(i, OF_TRUE, OF_TRUE);
	}

	di->flush();
	di->enableIRQ();//enable interrupts
	lastGeneratedObjectHandle = 0;
	isTotalFormatting = OF_FALSE;
	
	return freeSegmentsCount == OF_SEGMENT_MAX_COUNT;
}

OF_BOOL OFFSET::formatPage(OF_UINT pageIndex, OF_BOOL isDataPage, OF_BOOL forceFormat)
{
	OF_UINT sectorIndex = GCUtil::getPageSectorIndex(di, pageIndex);
	if (!di->canFormatSector(sectorIndex, forceFormat))
	{//now allowed to format sector
		return OF_FALSE;
	}

	di->disableIRQ();
	beginFormattingPage(pageIndex, isDataPage);
	
	di->formatSector(sectorIndex);
	OF_UINT pageGranularityFactor = di->getSectorSize(sectorIndex) / OF_HW_PAGE_SIZE;

	endFormattingPage();
	di->enableIRQ();

#if OF_CACHE_L1
#if OF_CACHE_L2
#if OF_CACHE_L3
#if OF_CACHE_L4
	di->memset(segStatusCache + pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE * 2, 0x03, OF_SEGMENT_MAX_COUNT_PER_PAGE * 2 * pageGranularityFactor);
#else
	di->memset(segStatusCache + pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE, 0x03, OF_SEGMENT_MAX_COUNT_PER_PAGE*pageGranularityFactor);
#endif
#else
	di->memset(segStatusCache + pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE/2, 0x33, (OF_SEGMENT_MAX_COUNT_PER_PAGE/2)*pageGranularityFactor);
#endif
#else
	di->memset(segStatusCache + pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE/4, 0xFF, (OF_SEGMENT_MAX_COUNT_PER_PAGE/4)*pageGranularityFactor);
#endif
#endif
	freeSegmentsCount += OF_SEGMENT_MAX_COUNT_PER_PAGE*pageGranularityFactor;
	return OF_TRUE;
}

OF_BOOL OFFSET::writeSegment(ObjectSegment* os, OF_BYTE usedContentLen, OF_BOOL doGC)
{
	OF_BYTE srcSegLen = _getSegmentSize(os->segmentFlags);

	for(OF_BYTE retries=0;retries<2;retries++)
	{
		if(retries && doGC)
		{
			gc();
		}

		OF_UINT itr = 0, cntItr = 0;
		if(iterateOnSegments(&itr, &cntItr, (OF_UINT)-1, (OF_UINT)-1, (OF_ATTRIBUTE_TYPE)-1, OF_TRUE, OF_FALSE, OF_TRUE, srcSegLen))
		{
			OF_BYTE buf[sizeof(ObjectSegment)];
			di->memcpy(buf, &os->objectHandler, SIZEOF_OH);
			di->memcpy(buf + SIZEOF_OH, &os->attrType, SIZEOF_AT);
			di->memcpy(buf + SIZEOF_OH + SIZEOF_AT, &os->segmentFlags, SIZEOF_SF);
			di->memcpy(buf + SIZEOF_OH + SIZEOF_AT + SIZEOF_SF, &os->segmentNumber, SIZEOF_SN);
			di->memcpy(buf + SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN, os->content, usedContentLen);
			di->write(buf, SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN + usedContentLen);
			
#if OF_CACHE_L1
			setSegmentStatusItr(itr, os->segmentNumber ? SEGMENT_STATUS_VALID : SEGMENT_STATUS_VALID_FAS, srcSegLen
#if OF_CACHE_L3
				, os->segmentNumber
#if OF_CACHE_L4
				, os->objectHandler, os->attrType
#endif
#endif
				);
#endif

			freeSegmentsCount -= (srcSegLen/OF_OBJECT_SEGMENT_MIN_SIZE);
			return OF_TRUE;
		}
	}
	return OF_FALSE;//no free space
}

OF_ULONG OFFSET::getTotalMemory()
{
	return (((OF_ULONG)OF_SEGMENT_AVG_COUNT)*((OF_ULONG)OF_OBJECT_SEGMENT_CONTENT_AVG_SIZE));
}

OF_ULONG OFFSET::getFreeMemory(OF_BOOL doGC)
{
	if(doGC)
	{
		gc();
	}
	return (freeSegmentsCount / 2)*((OF_ULONG)OF_OBJECT_SEGMENT_CONTENT_AVG_SIZE);
}

OF_BOOL OFFSET::canAllocateMemory(OF_ULONG requiredMemory, OF_ULONG attrCount)
{
	if(getFreeMemory(OF_FALSE) < requiredMemory + sizeof(OF_ULONG)*attrCount)
	{
		return getFreeMemory(OF_TRUE) >= requiredMemory + sizeof(OF_ULONG)*attrCount;
	}
	return OF_TRUE;
}

OF_VOID OFFSET::gc()
{
	if(disableGC)
	{
		return;
	}
	
	di->disableIRQ();//disable all interrupts

	//gc algorithm:
	//step 1: calculate number of valid & free segments each page
	//step 2: create two priority lists according to number of valid & free segments: minValid & maxFree
	//step 3: while possible, move segments from pages with min valid segments to pages with max free segments
	//step 4: if possible, format non-free pages with no valid segment

	//begin step 1
	OF_UINT minValid[OF_HW_DATAPAGE_COUNT][2];
	OF_UINT maxFree[OF_HW_DATAPAGE_COUNT][2];
	OF_UINT itr = 0, cntItr = 0;
#if !OF_CACHE_L1
	OF_UINT buf = 0, buf2 = 0;
#endif
	OF_BYTE segLen;
	OF_UINT totalInvalidSegmentsCount = 0, totalFreeSegmentsCount = 0;

	for(OF_BYTE i=0;i<OF_HW_DATAPAGE_COUNT;i++)
	{
		minValid[i][0] = maxFree[i][0] = i;
		minValid[i][1] = maxFree[i][1] = 0;
	}

	while(OF_TRUE)
	{
		segLen = iterateOnSegments(&itr, &cntItr);
		if(!segLen)
		{
			break;
		}
		
		OF_BYTE pageNum = _getItrPageNum(itr);

#if OF_CACHE_L1

		switch(getSegmentStatusItr(itr, segLen))
		{
		case SEGMENT_STATUS_VALID:
		case SEGMENT_STATUS_VALID_FAS:
			minValid[pageNum][1] += segLen/OF_SEGMENT_MIN_SIZE;
			break;
		case SEGMENT_STATUS_FREE:
			maxFree[pageNum][1]++;
			totalFreeSegmentsCount++;
			break;
		case SEGMENT_STATUS_INVALID:
			totalInvalidSegmentsCount++;
		}

#else

		di->seekg(-1*(int)SIZEOF_VI);
		di->read((char*)&buf, SIZEOF_VI);
		di->read((char*)&buf2, SIZEOF_OH);
		if((unsigned char)buf == OF_CONST_FREE_QWORD_VALUE && buf2 == OF_CONST_FREE_HWORD_VALUE)
		{//free segment
			maxFree[pageNum][1]++;
		}
		else if((unsigned char)buf == OF_CONST_FREE_QWORD_VALUE)
		{//valid non-free segment
			minValid[pageNum][1] += segLen/OF_SEGMENT_MIN_SIZE;
		}
		else
		{//invalid segment
			totalInvalidSegmentsCount++;
		}

#endif
	}
	//end step 1
	
	if (totalInvalidSegmentsCount < OF_SEGMENT_MAX_COUNT_PER_PAGE && totalFreeSegmentsCount > OF_SEGMENT_MAX_COUNT_PER_PAGE)
	{//at least OF_SEGMENT_MAX_COUNT_PER_PAGE invalid segments must be exists for doing GC
		di->flush();
		di->enableIRQ();//enable interrupts
		return;
	}
	
	//begin step 2
		
	GCUtil::sort(minValid, OF_TRUE);
	GCUtil::sort(maxFree, OF_FALSE);
	
	//end step 2

	//begin step 3
	
	OF_UINT movedSegments = 0;
	do
	{
		movedSegments = 0;

		for(OF_BYTE i=0;i<OF_HW_DATAPAGE_COUNT;i++)
		{
			OF_UINT cntInvalid = GCUtil::countInvalid(i, minValid, maxFree);
			if(cntInvalid < OF_SEGMENT_MAX_COUNT_PER_PAGE/3 || !GCUtil::doesGCLead2FormatPage(di, i, minValid, maxFree))
			{
				continue;
			}
			for(OF_BYTE j=0;minValid[i][1] && maxFree[j][1] && j<OF_HW_DATAPAGE_COUNT;j++)
			{
				if(!GCUtil::areCoSector(di, (OF_BYTE) minValid[i][0], (OF_BYTE) maxFree[j][0]))
				{
					OF_UINT srcInvalid = cntInvalid;
					OF_BYTE k=0;
					for(;k<OF_HW_DATAPAGE_COUNT;k++)
					{
						if(minValid[k][0] == maxFree[j][0])
						{
							break;
						}
					}
					transferPage(minValid[i][0], maxFree[j][0], &minValid[i][1], &srcInvalid, &minValid[k][1], &maxFree[j][1]);
					movedSegments += (srcInvalid-cntInvalid);
				}
			}
			if(movedSegments)
			{
				GCUtil::sort(minValid, OF_TRUE);
				GCUtil::sort(maxFree, OF_FALSE);
				break;
			}
		}

	}while(movedSegments);

	//end step 3

	//begin step 4

	for(OF_BYTE i=0,j=OF_HW_FIRST_DATAPAGE_OFFSET;i<OF_HW_DATAPAGE_COUNT;j++)
	{
		OF_UINT sectorIndex = GCUtil::getPageSectorIndex(di, i);
		OF_UINT sectorPagesCount = di->getSectorSize(sectorIndex)/OF_HW_PAGE_SIZE;
		if (di->canFormatSector(sectorIndex, OF_TRUE) && !GCUtil::countSectorValidPages(di, i, minValid) && GCUtil::countSectorInvalidPages(di, i, minValid, maxFree) > 0/*OF_SEGMENT_MAX_COUNT_PER_PAGE*(1.0/sectorPagesCount)*/)
		{
			formatPage(i, OF_TRUE, OF_TRUE);
		}
		i += sectorPagesCount;
	}

	//end step 4
	
	di->flush();
	di->enableIRQ();//enable interrupts
}

OF_VOID OFFSET::transferPage(OF_UINT srcPageIndex, OF_UINT dstPageIndex, OF_UINT_PTR srcValidCount, OF_UINT_PTR srcInvalidCount, OF_UINT_PTR dstValidCount, OF_UINT_PTR dstFreeCount)
{
	OF_BYTE cBuf[OF_OBJECT_SEGMENT_MAX_SIZE-SIZEOF_VI];

	OF_BYTE srcSegLen, dstSegLen = 0;
	OF_UINT srcItr = srcPageIndex, srcCntItr = 0, dstItr = dstPageIndex, dstCntItr = 0;
	OF_ULONG mark;

	while(OF_TRUE)
	{//iterate on valid source page segments
		srcSegLen = iterateOnSegments(&srcItr, &srcCntItr, (OF_UINT)-1, (OF_UINT)-1, (OF_ATTRIBUTE_TYPE)-1, OF_TRUE, OF_TRUE);
		if(!srcSegLen || _getItrPageNum(srcItr) != srcPageIndex)
		{//end of source page segments
			break;
		}
		di->markg(&mark);
		if(dstSegLen > srcSegLen)
		{
			dstItr = dstPageIndex;
			dstCntItr = 0;
		}
		while(OF_TRUE)
		{//iterate on free destination page segments with len srcSegLen
			dstSegLen = iterateOnSegments(&dstItr, &dstCntItr, (OF_UINT)-1, (OF_UINT)-1, (OF_ATTRIBUTE_TYPE)-1, OF_TRUE, OF_FALSE, OF_TRUE, srcSegLen);
			if(!dstSegLen || _getItrPageNum(dstItr) != dstPageIndex)
			{//end of destination page segments
				break;
			}
			
			//reading src segment
			di->restoreg(mark);
			di->read(cBuf, srcSegLen-SIZEOF_VI);//reading src segment (header & content)
			cBuf[SIZEOF_OH+SIZEOF_AT] = (cBuf[SIZEOF_OH+SIZEOF_AT] & ~SEGMENT_FLAG_MASK_TRANSMIT_COUNTER) | (((cBuf[SIZEOF_OH+SIZEOF_AT] & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER)+1)%4);//modifying segmentFlags

			di->write(cBuf, srcSegLen - SIZEOF_VI);//transferring segment

#if OF_CACHE_L1
			setSegmentStatusItr(dstItr, _segNum(cBuf+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF) ? SEGMENT_STATUS_VALID : SEGMENT_STATUS_VALID_FAS, dstSegLen
#if OF_CACHE_L3
						, _segNum(cBuf+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF)
#if OF_CACHE_L4
						, *(OF_UINT_PTR)cBuf, *(OF_ATTRIBUTE_TYPE*)(cBuf+SIZEOF_OH)
#endif
#endif				
				);
#endif
			
			mark -= SIZEOF_VI;
			di->restorep(mark);
			mark = 0;
			di->write((OF_BYTE_PTR)&mark, SIZEOF_VI);//invalidate src segment

#if OF_CACHE_L1
			setSegmentStatusItr(srcItr, SEGMENT_STATUS_INVALID, srcSegLen);
#endif

			mark = srcSegLen/OF_SEGMENT_MIN_SIZE;
			*srcValidCount -= (OF_BYTE) mark;
			*srcInvalidCount += (OF_BYTE) mark;
			*dstValidCount += (OF_BYTE) mark;
			*dstFreeCount -= (OF_BYTE) mark;
			freeSegmentsCount -= (OF_BYTE) mark;
			if(!*srcValidCount || !*dstFreeCount)
			{
				return;
			}
			break;
		}
	}
}

////////////////////

OF_BOOL OFFSET::iterateOnObjects(OF_ULONG_PTR itr, OF_OBJECT_HANDLE_PTR phObj, OF_BOOL bypassLogin, OF_BOOL onlyPrivateObjects, OF_BOOL onlyPublicObjects)
{
	OF_UINT iPos = (OF_UINT) (*itr);
	*phObj = OF_INVALID_OBJECT_HANDLE;

	for(;iPos<oHandlesCount;iPos++)
	{
		if(oHandles[iPos] != (OF_UINT) -1)
		{
#ifdef OF_METADATA_OBJECT_HANDLE
			if((oHandles[iPos] & ~OF_NORMAL_OBJECT_HANDLE_MASK) == OF_METADATA_OBJECT_HANDLE)
			{
				continue;//could not iterate on metadata object
			}
#endif
			if(!objectExists(oHandles[iPos] | OF_NORMAL_OBJECT_HANDLE_MASK, OF_TRUE))
			{
				destroyObject(oHandles[iPos] | OF_NORMAL_OBJECT_HANDLE_MASK, OF_TRUE);
				continue;
			}
			OF_BOOL isPrivateObj = isPrivateObject((OF_ULONG)(oHandles[iPos] | OF_NORMAL_OBJECT_HANDLE_MASK));
			if(!bypassLogin && !this->di->isAuthenticated() && isPrivateObj)
			{
				continue;//could not return handle of private object before login
			}
			if((onlyPublicObjects && isPrivateObj) || (onlyPrivateObjects && !isPrivateObj))
			{
				continue;
			}

			*itr = iPos+1;
			*phObj = oHandles[iPos];

			break;
		}
	}

	return *phObj != OF_INVALID_OBJECT_HANDLE;
}

OF_BOOL OFFSET::objectExists(OF_OBJECT_HANDLE hObj, OF_BOOL bypassLogin)
{
	OF_ULONG itr = 0;
	OF_UINT attItr = (OF_UINT) -1;
	OF_ATTRIBUTE_TYPE attrType;
	return iterateOnObjectAttributes(hObj, &itr, &attItr, &attrType, bypassLogin) != 0;
}

OF_ULONG OFFSET::getObjectSize(OF_OBJECT_HANDLE hObj)
{
	return getObjectSegmentCount(hObj) * OF_SEGMENT_MIN_SIZE;
}

OF_ULONG OFFSET::getPureObjectSize(OF_OBJECT_HANDLE hObj)
{
	OF_ULONG objectSize = 0, itr = 0;
	OF_UINT attItr = (OF_UINT) -1;
	OF_BYTE cBuf[SIZEOF_SF + SIZEOF_SN + sizeof(OF_UINT)];
	while(iterateOnObjectAttributes(hObj, &itr, &attItr, (OF_ATTRIBUTE_TYPE*)cBuf))
	{
		di->seekg(-1 * (int)(SIZEOF_SF + SIZEOF_SN));
		di->read(cBuf, SIZEOF_SF + SIZEOF_SN + sizeof(OF_UINT));
		objectSize += (_getSegmentSize(cBuf[0]) < OF_SEGMENT_MAX_SIZE ? *(OF_BYTE_PTR)(cBuf+SIZEOF_SF+SIZEOF_SN) : *(OF_UINT_PTR)(cBuf+SIZEOF_SF+SIZEOF_SN)) + sizeof(OF_ATTRIBUTE_TYPE) + sizeof(OF_ULONG);
	}
	return objectSize > 0 ? objectSize + sizeof(OF_OBJECT_HANDLE) : 0;
}

OF_UINT OFFSET::getObjectSegmentCount(OF_OBJECT_HANDLE hObj)
{
	OF_UINT segmentCount = 0, segmentSize;
	OF_ATTRIBUTE_TYPE attrType;
	OF_ULONG itr = 0, itr2 = 0;
	OF_UINT attItr = (OF_UINT)-1;
	while(iterateOnObjectAttributes(hObj, &itr, &attItr, &attrType))
	{
		itr2 &= 0xFFFF0000;
		while(OF_TRUE)
		{
			segmentSize = iterateOnObjectAttributeSegments(hObj, attrType, &itr2);
			if(!segmentSize)
			{
				break;
			}
			segmentCount += (segmentSize/OF_OBJECT_SEGMENT_MIN_SIZE);
		}
	}
	return segmentCount;
}

OF_UINT OFFSET::getObjectAttributeCount(OF_OBJECT_HANDLE hObj)
{
	OF_UINT attrCount = 0;
	OF_ULONG itr = 0;
	OF_UINT attItr = (OF_UINT)-1;

	while(iterateOnObjectAttributes(hObj, &itr, &attItr))
	{
		attrCount++;
	}

	return attrCount;
}

OF_RV OFFSET::setObjectAttribute(OF_OBJECT_HANDLE_PTR phObj, OF_ATTRIBUTE_TYPE attrType, OF_VOID_PTR pAttrValue, OF_ULONG ulAttrValueLen, OF_BOOL isObjectPrivate, OF_BOOL isAttrSensitive)
{
	if(isTotalFormatting)
	{
		return OFR_FUNCTION_FAILED;
	}
	
	OF_RV rv = validateObjectAttributeValue(attrType, pAttrValue, &ulAttrValueLen);
	if (rv != OFR_OK)
	{
		return rv;
	}
	
	OF_ULONG freeMem = getFreeMemory(OF_FALSE);
	if(freeMem < 5*ulAttrValueLen)
	{
		freeMem = getFreeMemory(OF_TRUE);
	}
	
	if(freeMem < ulAttrValueLen + sizeof(OF_ULONG))
	{
		return OFR_DEVICE_MEMORY;//can't allocate memory for storing this attribute
	}

	OF_BYTE oldSegmentFlag = 0, newSegmentFlag = 0, segLen, oldSegLen = 0;
	OF_ULONG oldMark, itr = 0, oldItr;
	if(*phObj != OF_INVALID_OBJECT_HANDLE)
	{//existing object
		OF_UINT attItr = (OF_UINT)-1;
#ifdef OF_STORE_METADATA_IN_FS
		if(*phObj != OF_METADATA_OBJECT_HANDLE)
#endif
		if(!iterateOnObjectAttributes(*phObj, &itr, &attItr))
		{//object handle invalid or object can't be seen because of it's privacy level
			return OFR_OBJECT_HANDLE_INVALID;
		}
		oldItr = 0;
		//check if attribute already exists & must invalidate its segments or not
		oldSegLen = iterateOnObjectAttributeSegments(*phObj, attrType, &oldItr);
		if(oldSegLen)
		{
			di->markp(&oldMark);
			OF_BYTE cBuf[SIZEOF_SF + SIZEOF_SN];
			di->seekg(-1 * (int)sizeof(cBuf));//seeking before segmentFlags
			di->read(cBuf, sizeof(cBuf));//reading segmentFlags & segmentNumber
			oldSegmentFlag = cBuf[0];
			newSegmentFlag = ((oldSegmentFlag & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER)+1) % 4;
		}
	}
	else
	{//new object
		if(isObjectPrivate && !di->isAuthenticated())
		{//user not logged in & so can't create private object
			return OFR_NOT_AUTHENTICATED;
		}

		if(!allocateNewObjectHandle(phObj))
		{//could not allocate new handle for this object
			return OFR_DEVICE_ERROR;
		}
	}

	ObjectSegment os;
	os.attrType = attrType;
	os.objectHandler = (OF_UINT) (*phObj & ~OF_NORMAL_OBJECT_HANDLE_MASK);
	os.segmentNumber = 0;
	os.segmentFlags = newSegmentFlag | (isAttrSensitive ? SEGMENT_FLAG_MASK_ENCRYPTED : 0);
	if(isObjectPrivate)
	{
		os.segmentFlags |= SEGMENT_FLAG_MASK_PRIVATE;
	}
	else
	{
		os.segmentFlags &= ~SEGMENT_FLAG_MASK_PRIVATE;
	}
	OF_ULONG readedLen = 0, remainedLen = ulAttrValueLen, len2Read;
	OF_BYTE offset = 0, segContentLen, maxAllowedSegmentSize = OF_OBJECT_SEGMENT_MAX_SIZE;
	OF_BOOL doGC = OF_TRUE;
	if(ulAttrValueLen > 0)
	{
		OF_ULONG ulAttrEncryptedValueLen = ulAttrValueLen;
		OF_BYTE_PTR pAttrEncryptedValue = (OF_BYTE_PTR) pAttrValue;
		OF_ULONG ulCryptBlockLen = 0;
		OF_VOID_PTR cryptCtx = isAttrSensitive ? di->getFSCryptContext(&ulCryptBlockLen, OF_TRUE) : NULL_PTR;
		if (isAttrSensitive && cryptCtx != NULL_PTR && ulCryptBlockLen)
		{//encryption sensitive attribute content
			ulAttrEncryptedValueLen += ulCryptBlockLen - ulAttrValueLen%ulCryptBlockLen;
			remainedLen = ulAttrEncryptedValueLen;
			pAttrEncryptedValue = (OF_BYTE_PTR)di->malloc(ulAttrEncryptedValueLen);
			
			for (OF_ULONG i = 0; i<ulAttrEncryptedValueLen; i += ulCryptBlockLen)
			{
				di->memcpy(pAttrEncryptedValue + i, ((OF_BYTE_PTR)pAttrValue) + i, min(ulCryptBlockLen, ulAttrValueLen - i));
				di->doFSCrypt(cryptCtx, pAttrEncryptedValue + i, pAttrEncryptedValue + i);
			}
			di->freeFSCryptContext(cryptCtx);
		}

		while(readedLen < ulAttrEncryptedValueLen)
		{//segmentation object attribute & write the segments to flash
			os.segmentFlags &= ~(/*SEGMENT_FLAG_MASK_TRANSMIT_COUNTER |*/ SEGMENT_FLAG_MASK_LAST_ATTR);
	
			if(maxAllowedSegmentSize == OF_OBJECT_SEGMENT_MIN_SIZE || remainedLen <= OF_OBJECT_SEGMENT_CONTENT_MIN_SIZE - (os.segmentNumber ? 0 : sizeof(OF_BYTE)))
			{
				//os.segmentFlags |= SEGMENT_FLAG_MASK_LEN_MIN;//sf[2-3] = 00b
				segContentLen = OF_OBJECT_SEGMENT_CONTENT_MIN_SIZE;
			}
			else if(maxAllowedSegmentSize == OF_OBJECT_SEGMENT_AVG_SIZE || remainedLen <= OF_OBJECT_SEGMENT_CONTENT_AVG_SIZE - (os.segmentNumber ? 0 : sizeof(OF_BYTE)))
			{
				os.segmentFlags |= SEGMENT_FLAG_MASK_LEN_AVG;//sf[2-3] = 01b
				segContentLen = OF_OBJECT_SEGMENT_CONTENT_AVG_SIZE;
			}
			else// if(remainedLen >= OF_OBJECT_SEGMENT_CONTENT_AVG_SIZE)
			{
				os.segmentFlags |= SEGMENT_FLAG_MASK_LEN_MAX;//sf[2-3] = 10b
				segContentLen = OF_OBJECT_SEGMENT_CONTENT_MAX_SIZE;
			}

			if(!os.segmentNumber)
			{//first segment
				offset = (os.segmentFlags & SEGMENT_FLAG_MASK_LEN_MAX) ? sizeof(OF_UINT) : sizeof(OF_BYTE);
				di->memcpy(os.content, &ulAttrValueLen, offset);
			}
			len2Read = min(remainedLen, (OF_BYTE)(segContentLen - offset));
			di->memcpy(os.content + offset, pAttrEncryptedValue + readedLen, len2Read);
			
			if(remainedLen == len2Read)
			{
				os.segmentFlags |= SEGMENT_FLAG_MASK_LAST_ATTR;
			}

			if(writeSegment(&os, (OF_BYTE)(len2Read+offset), doGC))
			{
				readedLen += len2Read;
				remainedLen -= len2Read;
				os.segmentNumber++;
				offset = 0;
			}
			else
			{
				if(maxAllowedSegmentSize == OF_OBJECT_SEGMENT_MIN_SIZE)
				{
					if(os.segmentNumber)
					{//invalidate written object attribute segments
						OF_UINT buf = 0;
						itr = 0;
						while(OF_TRUE)
						{
							segLen = iterateOnObjectAttributeSegments(*phObj, attrType, &itr);
							if(!segLen)
							{
								break;
							}
							di->seekp(-1 * OF_OBJECT_SEGMENT_HEADER_SIZE);//seeking before validatorIndicator
							di->write((OF_BYTE_PTR)&buf, SIZEOF_VI);//invalidate segment
#if OF_CACHE_L1
							setSegmentStatusItr((itr>>16) & 0x0000FFFF, SEGMENT_STATUS_INVALID, segLen);
#endif
						}
					}
					//Some error occured while writing to device memory
					rv = OFR_DEVICE_MEMORY;
					break;
				}
				doGC = OF_FALSE;
				maxAllowedSegmentSize -= OF_OBJECT_SEGMENT_MIN_SIZE;
			}
		}

		if(pAttrEncryptedValue != (OF_BYTE_PTR) pAttrValue)
		{
			di->free(pAttrEncryptedValue);
		}
		if(rv != OFR_OK)
		{
			return rv;
		}
	}
	else//if(!ulAttrValueLen)
	{
		os.segmentFlags |= SEGMENT_FLAG_MASK_LAST_ATTR;
		di->memcpy(os.content, &ulAttrValueLen, sizeof(OF_ULONG));
		if(!writeSegment(&os, sizeof(OF_ULONG)))
		{
			//Some error occured while writing to device memory
			return OFR_DEVICE_MEMORY;
		}
	}

	OF_BOOL beginDeleting = OF_FALSE;
	if((oldSegmentFlag & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER) != (newSegmentFlag & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER))
	{//invalidate old attribute segments
#ifdef OF_METADATA_OBJECT_HANDLE
		if(*phObj != OF_METADATA_OBJECT_HANDLE)
#endif
		beginDeleting = beginDeletingObjectAttribute(*phObj, attrType, oldSegmentFlag);
		
		OF_BYTE cBuf[SIZEOF_SF + SIZEOF_SN];
		di->memset(cBuf, 0, sizeof(cBuf));

		oldMark -= OF_OBJECT_SEGMENT_HEADER_SIZE;
		di->restorep(oldMark);//seeking before old segment validatorIndicator
		di->write(cBuf, SIZEOF_VI);//invalidate old segment
#if OF_CACHE_L1
		setSegmentStatusItr((oldItr>>16) & 0x0000FFFF, SEGMENT_STATUS_INVALID, oldSegLen);
#endif
		
		if(!(oldSegmentFlag & SEGMENT_FLAG_MASK_LAST_ATTR))
		{//not last segment
			OF_UINT itr = 0, cntItr = 0, segNum = 0, buf;
			OF_UINT tmp = (OF_UINT) (*phObj & ~OF_NORMAL_OBJECT_HANDLE_MASK);
			OF_ATTRIBUTE_TYPE aBuf;

			while(OF_TRUE)
			{
				segLen = iterateOnSegments(&itr, &cntItr, segNum, (OF_UINT)((*phObj) & ~OF_NORMAL_OBJECT_HANDLE_MASK), attrType, OF_TRUE, OF_TRUE);
				if(!segLen)
				{
					break;
				}
				di->read((OF_BYTE_PTR)&buf, SIZEOF_OH);//reading objectHandler
				if(buf == tmp)
				{
					di->read((OF_BYTE_PTR)&aBuf, SIZEOF_AT);
					if(aBuf == attrType)
					{
						di->read(cBuf, SIZEOF_SF + SIZEOF_SN);//reading segmentFlags & segmentNumber
						if((cBuf[0] & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER) != (newSegmentFlag & SEGMENT_FLAG_MASK_TRANSMIT_COUNTER))//== oldSegmentFlag)
						{
							di->seekp(-1 * (int)SIZEOF_VI);//seeging before validatorIndicator
							OF_UINT buf = 0;
							di->write((OF_BYTE_PTR)&buf, SIZEOF_VI);//writing validatorIndicator to invalidate segment
#if OF_CACHE_L1
							setSegmentStatusItr(itr, SEGMENT_STATUS_INVALID, segLen);
#endif
							if(cBuf[0] & SEGMENT_FLAG_MASK_LAST_ATTR)
							{
								break;
							}
							segNum++;
						}
					}
				}
			}
		}
#ifdef OF_METADATA_OBJECT_HANDLE
		if(*phObj != OF_METADATA_OBJECT_HANDLE)
#endif
		if(beginDeleting)
		{
			endDeletingObjectAttribute();
		}
	}
	di->flush();
	return OFR_OK;
}

OF_RV OFFSET::getObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType, OF_ULONG_PTR pulAttrValueLen, OF_VOID_PTR pAttrValue)
{
	if (isTotalFormatting)
	{
		return OFR_DEVICE_BUSY;
	}

	OF_ULONG itr = 0;
	OF_BYTE segLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr);
	if (!segLen)
	{
		return OFR_OBJECT_HANDLE_INVALID;//object or object attribute not found
	}

	if (!di->isAuthenticated() && isPrivateObject(hObj))
	{
		return OFR_OBJECT_HANDLE_INVALID;//object is private & could not be readed before login
	}

	OF_BYTE cBuf[SIZEOF_SF + SIZEOF_SN];
	di->read(cBuf, sizeof(OF_UINT));//reading attribute len
	OF_ULONG valueLen = cBuf[0];//setting attribute len
	if (segLen >= OF_OBJECT_SEGMENT_MAX_SIZE)
	{
		valueLen |= ((OF_UINT)cBuf[1]) << 8;
	}

	if (pulAttrValueLen == NULL_PTR)
	{
		return OFR_FUNCTION_INVALID_PARAM;
	}

	if (pAttrValue != NULL_PTR && *pulAttrValueLen < valueLen)
	{//small buffer
		return OFR_SMALL_BUFFER;
	}
	*pulAttrValueLen = valueLen;

	if (pAttrValue != NULL_PTR)
	{
		if (!valueLen)
		{
			return OFR_OK;
		}
		OF_BYTE_PTR pEncryptedAttrValue = (OF_BYTE_PTR)pAttrValue;
		OF_BYTE offset = segLen < OF_OBJECT_SEGMENT_MAX_SIZE ? sizeof(OF_BYTE) : sizeof(OF_UINT);
		OF_ULONG readedLen = offset % 2, len2Read, remainedLen = valueLen;
		if (readedLen && remainedLen)
		{
			di->memcpy(pAttrValue, cBuf + 1, 1);
			remainedLen--;
		}
		di->seekg(-1 * (int)(SIZEOF_SF + SIZEOF_SN + sizeof(OF_UINT)));
		di->read(cBuf, SIZEOF_SF + SIZEOF_SN);//reading attribute len
		OF_BOOL isAttrSensitive = cBuf[0] & SEGMENT_FLAG_MASK_ENCRYPTED;
		di->seekg(sizeof(OF_UINT));//seeking attribute len

		OF_ULONG ulCryptBlockLen = 0, encryptedValueLen = valueLen;
		OF_VOID_PTR cryptCtx = isAttrSensitive ? di->getFSCryptContext(&ulCryptBlockLen, OF_FALSE) : NULL_PTR;
		if (isAttrSensitive && cryptCtx != NULL_PTR && ulCryptBlockLen)
		{
			encryptedValueLen += ulCryptBlockLen - valueLen%ulCryptBlockLen;
			pEncryptedAttrValue = (OF_BYTE_PTR)di->malloc(encryptedValueLen);
			remainedLen = encryptedValueLen;
			if (readedLen)
			{
				di->memcpy(pEncryptedAttrValue, pAttrValue, readedLen);
				remainedLen -= readedLen;
			}
		}

		while (remainedLen)
		{
			len2Read = min(remainedLen, (OF_BYTE)(segLen - OF_OBJECT_SEGMENT_HEADER_SIZE - offset));
			di->read(pEncryptedAttrValue + readedLen, len2Read);
			readedLen += len2Read;
			remainedLen -= len2Read;
			if (remainedLen)
			{
				segLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr);
				if (!segLen)
				{
					return OFR_FUNCTION_FAILED;//could not read object value completely
				}
			}
			offset = 0;
		}

		if (pEncryptedAttrValue != (OF_BYTE_PTR)pAttrValue)
		{
			for (OF_ULONG i = 0; i<encryptedValueLen; i += ulCryptBlockLen)
			{
				di->doFSCrypt(cryptCtx, pEncryptedAttrValue + i, pEncryptedAttrValue + i);
				di->memcpy(((OF_BYTE_PTR)pAttrValue) + i, pEncryptedAttrValue + i, min(ulCryptBlockLen, valueLen - i));
			}
		}
		if (cryptCtx != NULL_PTR)
		{
			di->freeFSCryptContext(cryptCtx);
		}
		if (pEncryptedAttrValue != (OF_BYTE_PTR)pAttrValue)
		{
			di->free(pEncryptedAttrValue);
		}
	}
	return OFR_OK;
}

OF_RV OFFSET::deleteObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType)
{
	if (isTotalFormatting)
	{
		return OFR_DEVICE_BUSY;
	}

	OF_ULONG itr = 0;
	OF_BYTE segLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr);
	if (!segLen)
	{
		return OFR_OBJECT_HANDLE_INVALID;//object or object attribute not found
	}

	if (!di->isAuthenticated() && isPrivateObject(hObj))
	{
		return OFR_OBJECT_HANDLE_INVALID;//object is private & could not be readed before login
	}

	OF_ULONG buf = 0;
	do
	{
		di->seekp(-1 * OF_OBJECT_SEGMENT_HEADER_SIZE);//seek before validationIndicator
		di->write((OF_BYTE_PTR)&buf, SIZEOF_VI);//invalidate segment
#if OF_CACHE_L1
		setSegmentStatusItr((itr >> 16) & 0x0000FFFF, SEGMENT_STATUS_INVALID, segLen);
#endif
	} while (segLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr));

	di->flush();
	return OFR_OK;
}

OF_RV OFFSET::duplicateObject(OF_OBJECT_HANDLE hObj, OF_OBJECT_HANDLE_PTR phNewObj, OF_BYTE_PTR pNewTemplateStream)
{
	OF_BOOL ok = OF_TRUE, isObjectPrivate = isPrivateObject(hObj);
	OF_BYTE srcSegLen;
	OF_UINT copiedObjectSegmentCount = 0;
	OF_ULONG itr = 0, itr2 = 0, iItr, mark;
	OF_UINT attItr = (OF_UINT)-1, hNewObj = OF_INVALID_OBJECT_HANDLE/*(OF_UINT) (*phNewObj & ~OF_NORMAL_OBJECT_HANDLE_MASK)*/, itr3 = 0, cntItr;
	OF_ATTRIBUTE_TYPE attrType;
	OF_ATTRIBUTE attr;
	OF_BYTE cBuf[OF_OBJECT_SEGMENT_CONTENT_MAX_SIZE + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN];
	
	while(ok && iterateOnObjectAttributes(hObj, &itr, &attItr, &attrType))
	{
		//ckecking for new template attributes
		if(pNewTemplateStream != NULL_PTR)
		{
			iItr = 0;
			OF_BOOL ignoreOldAttr = OF_FALSE;
			while(iterateOnObjectTemplateStreamAtrributes(&attr, pNewTemplateStream, &iItr))
			{
				if(attr.type == attrType)
				{
					OF_RV rv = setObjectAttribute(phNewObj, attr.type, attr.pValue, attr.ulValueLen, isObjectPrivate);
					if(rv != OFR_OK)
					{
						if(copiedObjectSegmentCount)
						{
							destroyObject(*phNewObj);
							*phNewObj = NULL_PTR;
						}
						return rv;//Error copying new object attributes
					}
					copiedObjectSegmentCount++;
					ignoreOldAttr = OF_TRUE;
					break;
				}
			}
			if(ignoreOldAttr)
			{
				continue;
			}
		}

		if(!copiedObjectSegmentCount && !allocateNewObjectHandle(phNewObj))
		{
			return OFR_DEVICE_ERROR;//could not allocate new object handle
		}
		
		itr2 &= 0xFFFF0000;
		while(ok)
		{
			srcSegLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr2);
			if(!srcSegLen)
			{
				break;
			}
			cntItr = 0;
			di->markg(&mark);
			if (iterateOnSegments(&itr3, &cntItr, (OF_UINT)-1, (OF_UINT)-1, (OF_ATTRIBUTE_TYPE)-1, OF_TRUE, OF_FALSE, OF_TRUE, srcSegLen))
			{
				mark -= SIZEOF_AT+SIZEOF_SF+SIZEOF_SN;
				di->restoreg(mark);//seek after objectHandle
				di->read(cBuf, srcSegLen - SIZEOF_VI - SIZEOF_OH);//reading source attributeType, segmentFlags, segmentNumber & content
				
				if(hNewObj == 0xFFFF)
				{
					hNewObj = (OF_UINT) (*phNewObj & ~OF_NORMAL_OBJECT_HANDLE_MASK);
				}
				if(isObjectPrivate)
				{
					cBuf[SIZEOF_AT] |= SEGMENT_FLAG_MASK_PRIVATE;
				}
				else
				{
					cBuf[SIZEOF_AT] &= ~SEGMENT_FLAG_MASK_PRIVATE;
				}
				di->write((OF_BYTE_PTR)&hNewObj, SIZEOF_OH);//replacing new objectHandler
				di->write(cBuf, srcSegLen - SIZEOF_VI - SIZEOF_OH);//duplicating attributeType, segmentFlags, segmentNumber & content
#if OF_CACHE_L1
				setSegmentStatusItr(itr3, _segNum(cBuf+SIZEOF_AT+SIZEOF_SF) ? SEGMENT_STATUS_VALID : SEGMENT_STATUS_VALID_FAS, srcSegLen
#if OF_CACHE_L3			
					, _segNum(cBuf+SIZEOF_AT+SIZEOF_SF)
#if OF_CACHE_L4
					, hNewObj, attrType
#endif
#endif
					);
#endif
				copiedObjectSegmentCount++;
			}
			else
			{
				ok = OF_FALSE;
				break;
			}
		}
	}

	if(ok)
	{
		if(copiedObjectSegmentCount > 0)
		{
			return OFR_OK;
		}
		deleteOHandle(*phNewObj);
		*phNewObj = OF_INVALID_OBJECT_HANDLE;
		return  OFR_OBJECT_HANDLE_INVALID;
	}

	if(copiedObjectSegmentCount > 0)
	{
		destroyObject(*phNewObj);
		*phNewObj = OF_INVALID_OBJECT_HANDLE;
	}
	return OFR_DEVICE_ERROR;
}

OF_RV OFFSET::destroyObject(OF_OBJECT_HANDLE hObj, OF_BOOL bypassLogin)
{
	OF_BOOL success = OF_FALSE;
	OF_ULONG itr = 0, itr2 = 0;
	OF_UINT attItr = (OF_UINT) -1;
	OF_ATTRIBUTE_TYPE attrType;
	OF_BYTE segLen;
	OF_BOOL beginDeleting = beginDeletingObjectAttribute(hObj);
	
	while(iterateOnObjectAttributes(hObj, &itr, &attItr, &attrType, bypassLogin))
	{
		itr2 &= 0xFFFF0000;
		while(OF_TRUE)
		{
			segLen = iterateOnObjectAttributeSegments(hObj, attrType, &itr2);
			if(!segLen)
			{
				break;
			}
			di->seekp(-1 * OF_OBJECT_SEGMENT_HEADER_SIZE);//seek before validationIndicator
			OF_UINT buf = 0;
			di->write((OF_BYTE_PTR)&buf, SIZEOF_VI);//invalidate segment
#if OF_CACHE_L1
			setSegmentStatusItr((itr2>>16) & 0x0000FFFF, SEGMENT_STATUS_INVALID, segLen);
#endif
		}
		success = OF_TRUE;
	}
	if(success)
	{
		deleteOHandle(hObj);
		di->flush();
	}
	if(beginDeleting)
	{
		endDeletingObjectAttribute();
	}
	return success ? OFR_OK : OFR_OBJECT_HANDLE_INVALID;
}

OF_BOOL OFFSET::freeObjectSegment(ObjectSegment* os)
{
	OF_UINT itr = 0;
	OF_BYTE cBuf[SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN], segLen;

	while(OF_TRUE)
	{
		segLen = iterateOnSegments(&itr, NULL_PTR, os->segmentNumber, os->objectHandler, os->attrType, OF_TRUE, OF_TRUE);
		if(!segLen)
		{
			break;
		}
		di->read(cBuf, SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN);//reading objectHandle, attributeType, segmentFlags & segmentNumber
		if(*((OF_UINT_PTR)cBuf) == os->objectHandler && *((OF_ATTRIBUTE_TYPE*)(cBuf+SIZEOF_OH)) == os->attrType && _segNum(cBuf+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF) == os->segmentNumber)
		{//matched object info
			di->seekp(-1 * (int)SIZEOF_VI);//seeking before validationIndicator
			di->memset(cBuf, 0, SIZEOF_VI);
			di->write(cBuf, SIZEOF_VI);//invalidate validationIndicator
#if OF_CACHE_L1
			setSegmentStatusItr(itr, SEGMENT_STATUS_INVALID, segLen);
#endif
			return OF_TRUE;
		}
	}
	return OF_FALSE;
}

OF_BYTE OFFSET::iterateOnSegments(OF_UINT_PTR itr, OF_UINT_PTR cntItr, OF_OBJECT_HANDLE segmentNumber, OF_UINT oHandle, OF_ATTRIBUTE_TYPE attrType, OF_BOOL onlyValidSegments, OF_BOOL onlyNonFreeSegments, OF_BOOL onlyFreeSegments, OF_BYTE requiredSize, OF_BOOL onlyPrivateSegments, OF_BOOL onlyPublicSegments)
{
	OF_UINT i = _getItrPageNum(*itr);//pageNum
	OF_UINT j = _getItrSegNumInPage(*itr);//segmentNumInPage
	OF_BYTE msCount, tmp;

	while(cntItr == NULL_PTR || *cntItr < OF_SEGMENT_MAX_COUNT)
	{//while iteration not completed

		if(j >= OF_SEGMENT_MAX_COUNT_PER_PAGE)
		{//goto next page
			j = 0;
			i++;
		}

		if(i >= OF_HW_DATAPAGE_COUNT)
		{//invalid iterator
			if(cntItr == NULL_PTR)
			{//iterate only to the end of memory
				return 0;
			}
			i = 0;
		}
		OF_ULONG iBase = OF_CONST_DATABASE_ADDRESS + i*OF_HW_PAGE_SIZE;

		//seeking at the begining of segment
		di->seekg(iBase + j*OF_SEGMENT_MIN_SIZE, OF_TRUE);
		di->seekp(iBase + j*OF_SEGMENT_MIN_SIZE, OF_TRUE);
		
		msCount = 1;
		_setItrPageNum(*itr, i);
		_setItrSegNumInPage(*itr, ++j);
		(*cntItr)++;

#if OF_CACHE_L1

		OF_BOOL cnt = OF_TRUE, nextSeg = OF_TRUE;
		OF_UINT sgIndex = i*OF_SEGMENT_MAX_COUNT_PER_PAGE+j-1;
		OF_BYTE status = getSegmentStatus(sgIndex);
		switch(status)
		{
		case SEGMENT_STATUS_INVALID:
			cnt = !onlyValidSegments;		
			break;
		case SEGMENT_STATUS_VALID:
		case SEGMENT_STATUS_VALID_FAS:
			cnt = !onlyFreeSegments;
			break;
		case SEGMENT_STATUS_FREE:
			cnt = !onlyNonFreeSegments;
			nextSeg = OF_FALSE;
			break;
		}

		if(nextSeg)
		{
			tmp = getMinimizedSegmentCount(sgIndex)-1;
			if(tmp)
			{
				j += tmp;
				_setItrSegNumInPage(*itr, j);
				(*cntItr) += tmp;
				if(status == SEGMENT_STATUS_VALID || status == SEGMENT_STATUS_VALID_FAS)
				{
					msCount += tmp;
				}
			}
		}
#if OF_CACHE_L2
#if OF_CACHE_L3
		if(cnt && onlyNonFreeSegments && onlyValidSegments)
		{
			if(segmentNumber != 0xFFFF && getSegmentNumModulus(sgIndex) != (segmentNumber%16))
			{
				continue;
			}
#if OF_CACHE_L4
			if ((oHandle != OF_INVALID_OBJECT_HANDLE && getObjHandleModulus(sgIndex) != (oHandle % 16)) || (attrType != OF_INVALID_ATTRIBUTE_TYPE && getObjAttrTypeModulus(sgIndex) != (attrType % 16)))
			{
				continue;
			}
#endif
		}
#endif
#endif
		if(!cnt)
		{
			continue;
		}

		if(onlyPublicSegments || onlyPrivateSegments)
		{
			OF_BYTE cBuf[SIZEOF_SF + SIZEOF_SN];
			di->seekp(SIZEOF_VI);//seeking validationIndicator
			di->seekg(SIZEOF_VI + SIZEOF_OH + SIZEOF_AT);
			di->read(cBuf, SIZEOF_SF + SIZEOF_SN);//reading segmentFlag & segmentNumber
			if (!onlyFreeSegments && segmentNumber != OF_INVALID_SEGMENT_NUMBDER && ((_segNum(cBuf + SIZEOF_SF)) % 16) != (segmentNumber % 16))
			{
				continue;
			}
			if((onlyPrivateSegments && (cBuf[0] & SEGMENT_FLAG_MASK_PRIVATE)) || (onlyPublicSegments && !(cBuf[0] & SEGMENT_FLAG_MASK_PRIVATE)))
			{
				di->seekg(-1 * (int)(SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN));//seeking before objectHandle
				return msCount * OF_SEGMENT_MIN_SIZE;
			}
		}
		else
		{
			di->seekp(SIZEOF_VI);//seeking validationIndicator
			di->seekg(SIZEOF_VI);//seeking validationIndicator

#else

		OF_UINT buf;
		if(onlyValidSegments)
		{
			di->read((char*)&buf, SIZEOF_VI);//reading validationIndicator
			if((unsigned char)buf != OF_CONST_FREE_QWORD_VALUE)
			{//invalid segment
				tmp = getMinimizedSegmentCount(i*OF_SEGMENT_MAX_COUNT_PER_PAGE+j-1)-1;
				if(tmp)
				{
					j += tmp;
					_setItrSegNumInPage(*itr, j);
					(*cntItr) += tmp;
				}
				continue;
			}
		}
		else
		{
			di->seekg(SIZEOF_VI);//seeking validationIndicator
		}

		di->read((char*)&buf, SIZEOF_OH);//reading objectHandle
		if(buf != OF_CONST_FREE_HWORD_VALUE)
		{//non-free segment
			tmp = getMinimizedSegmentCount(i*OF_SEGMENT_MAX_COUNT_PER_PAGE+j-1)-1;
			if(tmp)
			{
				j += tmp;
				_setItrSegNumInPage(*itr, j);
				(*cntItr) += tmp;
				msCount += tmp;
			}
		}
		if((onlyFreeSegments && buf != OF_CONST_FREE_HWORD_VALUE) || (onlyNonFreeSegments && buf == OF_CONST_FREE_HWORD_VALUE))
		{//free or non-free??? this is the question!
			continue;
		}

		if(onlyPublicSegments || onlyPrivateSegments)
		{
			unsigned char cBuf[SIZEOF_SF+SIZEOF_SN];
			di->seekg(SIZEOF_AT);//seeking attributeType
			di->read((char*)cBuf, SIZEOF_SF + SIZEOF_SN);//reading segmentFlag & segmentNumber
			if((onlyPrivateSegments && (cBuf[0] & SEGMENT_FLAG_MASK_PRIVATE)) || (onlyPublicSegments && !(cBuf[0] & SEGMENT_FLAG_MASK_PRIVATE)))
			{
				di->seekg(-1 * (int)(SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN));//seeking before objectHandle
				di->seekp(SIZEOF_VI);//seeking validationIndicator
				return msCount * OF_SEGMENT_MIN_SIZE;
			}
		}
		else
		{
			di->seekg(-1 * (int)SIZEOF_OH);//seeking before objectHandle
			di->seekp(SIZEOF_VI);//seeking validationIndicator

#endif
			
			if(!onlyNonFreeSegments)
			{
				OF_BYTE n = (requiredSize/OF_SEGMENT_MIN_SIZE);
				if(onlyFreeSegments && msCount < n)
				{//calculate number of sequential free segments
#if !OF_CACHE_L1 
					unsigned char cBuf[SIZEOF_VI+SIZEOF_OH];
#endif
					while(*cntItr < OF_SEGMENT_MAX_COUNT)
					{
						OF_UINT iPos = i*OF_SEGMENT_MAX_COUNT_PER_PAGE;
						while(msCount < n && j <= OF_SEGMENT_MAX_COUNT_PER_PAGE-n+msCount && *cntItr < OF_SEGMENT_MAX_COUNT)
						{
#if OF_CACHE_L1
							if(getSegmentStatus(iPos+j) != SEGMENT_STATUS_FREE)
							{
								msCount = getMinimizedSegmentCount(iPos+j);
								(*cntItr) += msCount;
								j += msCount;
								msCount = 0;
								continue;
							}
#else
							di->seekg(iBase + j*OF_SEGMENT_MIN_SIZE, OF_TRUE);
							di->read((char*)cBuf, SIZEOF_VI + SIZEOF_OH);
							OF_ULONG _freeW = OF_CONST_FREE_WORD_VALUE;
							if(memcmp(cBuf, &_freeW, SIZEOF_VI+SIZEOF_OH))
							{//invalid or non-free segment
								msCount = getMinimizedSegmentCount(iPos+j);
								(*cntItr) += msCount;
								j += msCount;
								msCount = 0;
								continue;
							}
#endif
							(*cntItr)++;
							j++;
							msCount++;
						}

						if(msCount == n)
						{
							di->seekg(iBase + (j - n)*OF_SEGMENT_MIN_SIZE + SIZEOF_VI, OF_TRUE);
							di->seekp(iBase + (j - n)*OF_SEGMENT_MIN_SIZE + SIZEOF_VI, OF_TRUE);
							break;
						}
						
						msCount = 0;
						(*cntItr) += (OF_SEGMENT_MAX_COUNT_PER_PAGE-j);
						j = 0;
						if(++i == OF_HW_DATAPAGE_COUNT)
						{
							i = 0;
						}
						iBase = OF_CONST_DATABASE_ADDRESS + i*OF_HW_PAGE_SIZE;
					}
					_setItrPageNum(*itr, i);
					_setItrSegNumInPage(*itr, j);
				}
				return msCount < n ? 0 : msCount * OF_SEGMENT_MIN_SIZE;
			}
			return msCount * OF_SEGMENT_MIN_SIZE;
		}
	}
	return 0;
}

OF_BYTE OFFSET::iterateOnObjectAttributeSegments(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType, OF_ULONG_PTR itr)
{
	OF_BYTE segmentFlags = (OF_BYTE) (*itr & 0x000000FF);
	if(SIZEOF_SN == 1 && (segmentFlags & SEGMENT_FLAG_MASK_LAST_ATTR))
	{
		return 0;
	}
	OF_BYTE segLen;	
	OF_UINT segmentNumber = (OF_UINT) (((*itr)>>(8*(2-SIZEOF_SN))) & (SIZEOF_SN==1?0x000000FF:0x0000FFFF));
	OF_UINT itr2 = (OF_UINT) ((*itr)>>16 & 0x0000FFFF), cntItr = 0;
	OF_UINT oHandle = (OF_UINT) (hObj & ~OF_NORMAL_OBJECT_HANDLE_MASK);
	OF_BOOL found = OF_FALSE;
	OF_BYTE cBuf[SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN];

	while(OF_TRUE)
	{
		segLen = iterateOnSegments(&itr2, &cntItr, segmentNumber, (OF_UINT)(hObj & ~OF_NORMAL_OBJECT_HANDLE_MASK), attrType, OF_TRUE, OF_TRUE);
		if(!segLen)
		{
			break;
		}
#if OF_CACHE_L1
#if OF_CACHE_L2
		if(!segmentNumber && getSegmentStatusItr(itr2, segLen) != SEGMENT_STATUS_VALID_FAS)
		{
			continue;
		}
#endif
#endif
		
		di->read(cBuf, SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN);//reading objectHandle, attributeType, segmentFlag & segmentNumber
		if(oHandle == *(OF_UINT_PTR)cBuf && *(OF_ATTRIBUTE_TYPE*)(cBuf+SIZEOF_OH) == attrType && _segNum(cBuf+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF) == segmentNumber)
		{//matched valid object handle & segment number
			found = OF_TRUE;
			segmentFlags = cBuf[SIZEOF_OH+SIZEOF_AT];
			segmentNumber++;
			di->seekp(SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN);//seeking to content
			break;
		}
	}
		
	*itr = itr2;
	*itr <<= 8*SIZEOF_SN;
	*itr |= segmentNumber;
	if(SIZEOF_SN == 1)
	{
		*itr <<= 8;
		*itr |= segmentFlags;
	}

	return found ? segLen : 0;
}

OF_BOOL OFFSET::iterateOnObjectTemplateStreamAtrributes(OF_ATTRIBUTE_PTR pAttr, const OF_BYTE_PTR pStreamValue, OF_ULONG_PTR itr)
{
	if (pStreamValue == NULL_PTR || itr == NULL_PTR || ((*itr) & 0x0000FFFF) == *(OF_ULONG_PTR)pStreamValue /*|| ((*itr) & 0x0000FFFF) == 0xFFFF*/)
	{
		return OF_FALSE;
	}

	OF_ULONG cursor = ((*itr) >> 16) & 0x0000FFFF;
	if (!cursor)
	{
		cursor = 2 * sizeof(OF_ULONG);
	}
	OF_BOOL incValues = *(OF_BOOL*)(pStreamValue + sizeof(OF_ULONG));

	pAttr->type = *(OF_ATTRIBUTE_TYPE*)(pStreamValue + cursor);
	cursor += sizeof(OF_ATTRIBUTE_TYPE);
	pAttr->ulValueLen = *(OF_ULONG_PTR)(pStreamValue + cursor);
	cursor += sizeof(OF_ULONG);

	if ((incValues /*|| ((pAttr->type & CKF_ARRAY_ATTRIBUTE) && pAttr->type != CKA_ALLOWED_MECHANISMS)*/) && pAttr->ulValueLen && pAttr->ulValueLen != (OF_ULONG)-1)
	{
		pAttr->pValue = pStreamValue + cursor;
		cursor += pAttr->ulValueLen;
	}
	else
	{
		pAttr->pValue = NULL_PTR;
	}

	*itr = (cursor << 16) | (++(*itr) & 0x0000FFFF);

	return OF_TRUE;
}

OF_BOOL OFFSET::iterateOnObjectAttributes(OF_OBJECT_HANDLE hObj, OF_ULONG_PTR itr, OF_UINT_PTR attItr, OF_ATTRIBUTE_TYPE* pAttrType, OF_BOOL bypassLogin)
{
	if(!bypassLogin && !di->isAuthenticated() && isPrivateObject(hObj))
	{
		return OF_FALSE;//could not return attributes of private object before login
	}

	OF_BYTE cBuf[SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN];
	//OF_BYTE segmentNumber = (OF_BYTE) (*itr & 0x000000FF);
	//OF_BYTE segmentFlags = (OF_BYTE) ((*itr)>>8 & 0x000000FF);
	OF_UINT itr2 = (OF_UINT) ((*itr)>>16 & 0x0000FFFF), cntItr = 0;
	OF_UINT oHandle = (OF_UINT) (hObj & ~OF_NORMAL_OBJECT_HANDLE_MASK);
	OF_BOOL found = OF_FALSE;
	OF_BYTE segLen;
	
	while(OF_TRUE)
	{
		segLen = iterateOnSegments(&itr2, &cntItr, 0, (OF_UINT)(hObj & ~OF_NORMAL_OBJECT_HANDLE_MASK), (OF_ATTRIBUTE_TYPE)-1, OF_TRUE, OF_TRUE);
		if(!segLen)
		{
			break;
		}
#if OF_CACHE_L1
#if OF_CACHE_L2
		if(getSegmentStatusItr(itr2, segLen) != SEGMENT_STATUS_VALID_FAS)
		{
			continue;
		}
#endif
#endif
		if(*attItr == itr2)
		{
			break;
		}
		di->read(cBuf, SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN);//reading objectHandle, attributeType, segmentFlag & segmentNumber
		if(oHandle == *(OF_UINT_PTR)cBuf && _segNum(cBuf+SIZEOF_OH+SIZEOF_AT+SIZEOF_SF) == 0)
		{//matched valid object handle & this is first segment of an object attribute
			found = OF_TRUE;
			//segmentNumber++;
			if(pAttrType != NULL_PTR)
			{
				*pAttrType = *(OF_ATTRIBUTE_TYPE*)(cBuf+SIZEOF_OH);
			}
			di->seekp(SIZEOF_OH + SIZEOF_AT + SIZEOF_SF + SIZEOF_SN);//seeking to content
			if(*attItr == 0xFFFF)//*attItr == -1
			{
				*attItr = itr2;
			}
			break;
		}
	}
		
	*itr = ((OF_ULONG)itr2)<<16;
	//*itr |= segmentNumber;

	return found;
}


OF_BOOL OFFSET::beginDeletingObjectAttribute(OF_OBJECT_HANDLE hObj, OF_ATTRIBUTE_TYPE attrType, OF_BYTE flags)
{
	OF_BYTE buf[2*sizeof(OF_UINT)+sizeof(OF_ATTRIBUTE_TYPE)];
	hObj &= ~OF_NORMAL_OBJECT_HANDLE_MASK;
	di->memcpy(buf, &hObj, sizeof(OF_UINT));
	di->memcpy(buf + sizeof(OF_UINT), &attrType, sizeof(OF_ATTRIBUTE_TYPE));
	di->memcpy(buf + sizeof(OF_UINT)+sizeof(OF_ATTRIBUTE_TYPE), &flags, sizeof(OF_BYTE));
	return di->writeMetaData(OFMID_DELETING_OBJECT_ATTR_0, buf, sizeof(buf)) == sizeof(buf);
}

OF_VOID OFFSET::endDeletingObjectAttribute()
{
	OF_UINT buf = 0xFFFF;
	di->writeMetaData(OFMID_DELETING_OBJECT_ATTR_0, &buf, sizeof(buf));
}

OF_BOOL OFFSET::beginFormattingPage(OF_UINT pageIndex, OF_UINT isDataPage)
{
	if(!isTotalFormatting)
	{
		OF_UINT buf = (((uint16_t)pageIndex)<<8) | isDataPage;
		return di->writeMetaData(OFMID_FORMATTING_PAGE, &buf, sizeof(buf)) == sizeof(buf);
	}
	return OF_TRUE;
}

OF_VOID OFFSET::endFormattingPage()
{
	if(!isTotalFormatting)
	{
		OF_UINT buf = (OF_UINT)-1;
		di->writeMetaData(OFMID_FORMATTING_PAGE, &buf, sizeof(buf));
	}
}

OF_BOOL OFFSET::allocateNewObjectHandle(OF_OBJECT_HANDLE_PTR phObj)
{
	//begin critical section
	for(OF_ULONG i=1; i < 0xFFFE; i++)
	{
		if(lastGeneratedObjectHandle == 0xFFFE)
		{
			lastGeneratedObjectHandle = 0;
		}
		if(!this->objectExists(OF_NORMAL_OBJECT_HANDLE_MASK+1+lastGeneratedObjectHandle, OF_TRUE))
		{
			addOHandle(*phObj = OF_NORMAL_OBJECT_HANDLE_MASK+1+lastGeneratedObjectHandle++);
			return OF_TRUE;
		}
		lastGeneratedObjectHandle++;
	}
	//end critical section
	*phObj = OF_INVALID_OBJECT_HANDLE;
	return OF_FALSE;
}

OF_BOOL OFFSET::isPrivateObject(OF_OBJECT_HANDLE hObj)
{
	OF_BYTE flags = 0;
	OF_ULONG itr = 0, _markp, _markg;
	OF_UINT attItr = (OF_UINT)-1;
	di->markp(&_markp);
	di->markg(&_markg);
	if (iterateOnObjectAttributes(hObj, &itr, &attItr, NULL_PTR, OF_TRUE))
	{
		di->seekg(-1 * (int)(SIZEOF_SF + SIZEOF_SN));
		di->read(&flags, SIZEOF_SF);//reading segment flags
	}
	di->restorep(_markp);
	di->restoreg(_markg);
	return (flags & SEGMENT_FLAG_MASK_PRIVATE) ? OF_TRUE : OF_FALSE;
}

OF_BYTE OFFSET::getMinimizedSegmentCount(OF_UINT segmentGlobalIndex, OF_BOOL useCache)
{
#if OF_CACHE_L1
	if(useCache)
	{
#if OF_CACHE_L2
		return getSegmentCount(segmentGlobalIndex);
#else
		if(getSegmentStatus(segmentGlobalIndex) == SEGMENT_STATUS_FREE)
		{
			return 1;
		}
#endif
	}
#endif
	OF_UINT buf;
	OF_ULONG mark;
	di->markg(&mark);
	di->seekg(OF_CONST_DATABASE_ADDRESS + segmentGlobalIndex*OF_SEGMENT_MIN_SIZE + SIZEOF_VI, OF_TRUE);
	di->read((OF_BYTE_PTR)&buf, SIZEOF_OH);
	if(buf == OF_CONST_FREE_HWORD_VALUE)
	{//free segment
		di->restoreg(mark);
		return 1;
	}
	di->seekg(SIZEOF_AT);
	di->read((OF_BYTE_PTR)&buf, SIZEOF_SF);//+SIZEOF_SN);
	di->restoreg(mark);
	return _getSegmentCount(buf);
}

#if OF_CACHE_L1

OF_BYTE OFFSET::getSegmentStatus(OF_UINT segmentGlobalIndex)
{
#if OF_CACHE_L2
#if OF_CACHE_L3
#if OF_CACHE_L4
	return (segStatusCache[2*segmentGlobalIndex] & 0x03);
#else
	return (segStatusCache[segmentGlobalIndex] & 0x03);
#endif
#else
	return (segStatusCache[segmentGlobalIndex/2] & (0x03 << ((segmentGlobalIndex%2)*4))) >> ((segmentGlobalIndex%2)*4);
#endif
#else
	return (segStatusCache[segmentGlobalIndex/4] & (0x03 << ((segmentGlobalIndex%4)*2))) >> ((segmentGlobalIndex%4)*2);
#endif
}

OF_VOID OFFSET::setSegmentStatus(OF_UINT segmentGlobalIndex, OF_BYTE status, OF_BYTE segCount
#if OF_CACHE_L3
	, OF_UINT segNum
#if OF_CACHE_L4
	, OF_UINT oHandle, OF_ATTRIBUTE_TYPE attrType
#endif
#endif
	)
{
#if OF_CACHE_L2
	OF_BYTE newStatus = status;
	//specifying segment length type
	switch(segCount)
	{
	case 1://1x16-byte segment
		break;
	case 2://2x16-byte segments
		newStatus |= SEGMENT_FLAG_MASK_LEN_AVG;
		break;
	//case 4:
	default://4x16-byte segments
		newStatus |= SEGMENT_FLAG_MASK_LEN_MAX;
	}
#else
	if(status == SEGMENT_STATUS_VALID_FAS)
	{
		status = SEGMENT_STATUS_VALID;
	}
#endif
	while(segCount--)
	{
#if OF_CACHE_L2
#if OF_CACHE_L3
#if OF_CACHE_L4
		segStatusCache[2*segmentGlobalIndex] = (newStatus | ((segNum%16)<<4));
		segStatusCache[2*segmentGlobalIndex+1] = ((attrType%16) | ((oHandle%16)<<4));
#else
		segStatusCache[segmentGlobalIndex] = (newStatus | ((segNum%16)<<4));
#endif
#else
		segStatusCache[segmentGlobalIndex/2] = (segStatusCache[segmentGlobalIndex/2] & ~(0x0F<<((segmentGlobalIndex%2)*4))) | (newStatus<<((segmentGlobalIndex%2)*4));
#endif
		/*if(segCount)
		{
			newStatus &= 0x03;
		}*/
#else
		segStatusCache[segmentGlobalIndex/4] = (segStatusCache[segmentGlobalIndex/4] & ~(0x03<<((segmentGlobalIndex%4)*2))) | (status<<((segmentGlobalIndex%4)*2));
#endif
		segmentGlobalIndex++;
	}
}

OF_BYTE OFFSET::getSegmentStatusItr(OF_UINT itr, OF_BYTE segLen)
{
	OF_BYTE pageIndex = _getItrPageNum(itr);
	OF_UINT segmentIndex = _getItrSegNumInPage(itr);
	segLen /= OF_SEGMENT_MIN_SIZE;
	//if(segLen <= segmentIndex)
	//assume all continues segments are in a single page
	return getSegmentStatus(pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE+segmentIndex-segLen);
}

OF_VOID OFFSET::setSegmentStatusItr(OF_UINT itr, OF_BYTE status, OF_BYTE segLen
#if OF_CACHE_L3
	, OF_UINT segNum
#if OF_CACHE_L4
	, OF_UINT oHandle, OF_ATTRIBUTE_TYPE attrType
#endif
#endif
	)
{
	OF_BYTE pageIndex = _getItrPageNum(itr);
	OF_UINT segmentIndex = _getItrSegNumInPage(itr);
	segLen /= OF_SEGMENT_MIN_SIZE;
	if(segLen <= segmentIndex)
	{
		setSegmentStatus(pageIndex*OF_SEGMENT_MAX_COUNT_PER_PAGE+segmentIndex-segLen, status, segLen
#if OF_CACHE_L3
	, segNum
#if OF_CACHE_L4
	, oHandle, attrType
#endif
#endif
			);
	}
}

#if OF_CACHE_L2

OF_BYTE OFFSET::getSegmentCount(OF_UINT segmentGlobalIndex)
{
#if OF_CACHE_L3
#if OF_CACHE_L4
	return _getSegmentCount(segStatusCache[segmentGlobalIndex*2]/* & 0x0F*/);
#else
	return _getSegmentCount(segStatusCache[segmentGlobalIndex]/* & 0x0F*/);
#endif
#else
	return _getSegmentCount((segStatusCache[segmentGlobalIndex/2]) >> ((segmentGlobalIndex%2)*4));
#endif
}

#if OF_CACHE_L3

OF_BYTE OFFSET::getSegmentNumModulus(OF_UINT segmentGlobalIndex)
{
#if OF_CACHE_L4
	return (segStatusCache[segmentGlobalIndex*2]>>4)%16;
#else
	return (segStatusCache[segmentGlobalIndex]>>4)%16;
#endif
}

#if OF_CACHE_L4
OF_BYTE OFFSET::getObjHandleModulus(OF_UINT segmentGlobalIndex)
{
	return (segStatusCache[segmentGlobalIndex*2+1]>>4)%16;
}

OF_BYTE OFFSET::getObjAttrTypeModulus(OF_UINT segmentGlobalIndex)
{
	return (segStatusCache[segmentGlobalIndex*2+1] & 0x0F)%16;
}
#endif

#endif

#endif

#endif

OF_BOOL OFFSET::addOHandle(OF_OBJECT_HANDLE hObject)
{
	OF_UINT oHandle = (OF_UINT)(hObject & ~OF_NORMAL_OBJECT_HANDLE_MASK);
	for(OF_UINT i=0;i<oHandlesCount;i++)
	{
		if(oHandles[i] == oHandle)
		{//object handle already exists
			return OF_FALSE;
		}
	}
	for(OF_UINT i=0;i<oHandlesCount;i++)
	{
		if(oHandles[i] == (OF_UINT)-1)
		{
			oHandles[i] = oHandle;
			return OF_TRUE;
		}
	}
	OF_UINT_PTR temp = (OF_UINT_PTR)di->realloc(oHandles, (oHandlesCount + OBJECT_HANDLE_LIST_GROW_SIZE)*sizeof(OF_UINT));
	if(!temp)
	{
		return OF_FALSE;
	}
	oHandles = temp;
	oHandles[oHandlesCount++] = oHandle;
	for(OF_UINT i=1;i<OBJECT_HANDLE_LIST_GROW_SIZE;i++)
	{
		oHandles[oHandlesCount++] = (OF_UINT)-1;
	}
	return OF_TRUE;
}

OF_BOOL OFFSET::deleteOHandle(OF_OBJECT_HANDLE hObject)
{
	OF_UINT oHandle = (OF_UINT)(hObject & ~OF_NORMAL_OBJECT_HANDLE_MASK), freeOHandleCount = 0;
	OF_BOOL res = OF_FALSE;
	for(OF_UINT i=0;i<oHandlesCount;i++)
	{
		if(oHandles[i] == oHandle)
		{
			oHandles[i] = (OF_UINT)-1;
			res = OF_TRUE;
		}
		if(oHandles[i] == (OF_UINT)-1)
		{
			freeOHandleCount++;
		}
	}
	if(freeOHandleCount > 10*OBJECT_HANDLE_LIST_GROW_SIZE)
	{
		OF_UINT newOhandleCount = oHandlesCount - (freeOHandleCount-(freeOHandleCount%OBJECT_HANDLE_LIST_GROW_SIZE)) + OBJECT_HANDLE_LIST_GROW_SIZE;
		OF_UINT_PTR newOHandles = (OF_UINT_PTR) di->malloc(newOhandleCount*sizeof(OF_UINT)/*, OF_FALSE*/);
		if(newOHandles)
		{
			OF_UINT j=0;
			for(OF_UINT i=0;i<oHandlesCount;i++)
			{
				if(oHandles[i] != (OF_UINT)-1)
				{
					newOHandles[j++] = oHandles[i];
				}
			}
			for(;j<newOhandleCount;j++)
			{
				newOHandles[j] = (OF_UINT)-1;
			}
			di->free(oHandles);
			oHandles = newOHandles;
			oHandlesCount = newOhandleCount;
		}
	}
	return res;
}

OF_RV OFFSET::validateObjectAttributeValue(OF_ATTRIBUTE_TYPE attrType, OF_VOID_PTR pAttrValue, OF_ULONG_PTR pulAttrValueLen)
{
	if (attrType == OF_INVALID_ATTRIBUTE_TYPE || pulAttrValueLen == NULL_PTR || (pAttrValue == NULL_PTR && *pulAttrValueLen > 0) || *pulAttrValueLen > OF_OBJECT_MAX_ALLOWED_ATTRIBUTE_SIZE)
	{
		return OFR_FUNCTION_INVALID_PARAM;
	}
	return OFR_OK;
}
