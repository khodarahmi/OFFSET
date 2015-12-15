#include "OFFSET.h"
#include "aes.h"

#include <fstream>
#include <time.h>

using namespace std;

class FileDeviceInterface : public DeviceInterface
{
private:
	OF_BOOL isVolatile, bufferedIO;
	fstream* fs;
	OF_BYTE_PTR as;
	OF_ULONG rPos, wPos;

public:
	OF_VOID markg(OF_ULONG_PTR _rPos){ *_rPos = rPos; }
	OF_VOID markp(OF_ULONG_PTR _wPos){ *_wPos = wPos; }
	OF_VOID restoreg(OF_ULONG _rPos){ rPos = _rPos; }
	OF_VOID restorep(OF_ULONG _wPos){ wPos = _wPos; }
	OF_VOID seekg(OF_INT len, OF_BOOL fromBegin = OF_FALSE){ rPos = fromBegin ? len : rPos + len; }
	OF_VOID seekp(OF_INT len, OF_BOOL fromBegin = OF_FALSE){ wPos = fromBegin ? len : wPos + len; }

	FileDeviceInterface(string filename)
	{
		bufferedIO = OF_TRUE;
		isVolatile = OF_FALSE;
		rPos = wPos = 0;

		if (isVolatile || bufferedIO)
		{
			as = (OF_BYTE_PTR)this->malloc(OF_CONST_TOTAL_SIZE);
			if (isVolatile)
			{
				format();
				return;
			}
		}

		fs = new fstream(filename, ios::in | ios::out | ios::binary);
		if (!fs->good())
		{
			fstream(filename, ios::out | ios::binary | ios::trunc).close();
			fs = new fstream(filename, ios::in | ios::out | ios::binary);
			format();
		}

		if (bufferedIO)
		{
			//read from file to buffer
			fs->seekg(0, ios::beg);
			fs->read((char*)as, OF_CONST_TOTAL_SIZE);
		}
	}

	~FileDeviceInterface()
	{
		flush();
		fs->close();
		delete fs;
		free(as);
	}

	/**
	 * this is just a utility method that formats all assigned flash sectors
	 **/
	OF_BOOL format()
	{
		for (int i = 0; i < OF_HW_METAPAGE_COUNT + OF_HW_DATAPAGE_COUNT; i++)
		{
			if (!formatSector(i))
			{
				return OF_FALSE;
			}
		}
		seekp(0, true);
		seekg(0, true);
		flush();
		return OF_TRUE;
	}

	OF_INT read(OF_BYTE_PTR data, OF_INT len)
	{
		bool isVolatile = this->isVolatile || this->bufferedIO;

		int readedLen = 0, remainedLen;

		while (readedLen < len)
		{
			remainedLen = len - readedLen;
			if (remainedLen >= OF_HW_MAX_RW_SIZE)
			{
				if (isVolatile)
				{
					memcpy(data + readedLen, as + rPos, remainedLen = min(remainedLen, OF_HW_MAX_RW_SIZE));
				}
				else
				{
					fs->seekg(rPos, ios::beg);
					fs->read((char*) (data + readedLen), remainedLen = min(remainedLen, OF_HW_MAX_RW_SIZE));
				}
				rPos += OF_HW_MAX_RW_SIZE;
				readedLen += remainedLen;
			}
			else
			{
				if (isVolatile)
				{
					memcpy(data + readedLen, as + rPos, remainedLen);
				}
				else
				{
					fs->seekg(rPos, ios::beg);
					fs->read((char*)(data + readedLen), remainedLen);
				}
#if OF_HW_MIN_RW_SIZE > 1
				rPos += remainedLen > OF_HW_MIN_RW_SIZE ? OF_HW_MAX_RW_SIZE : OF_HW_MIN_RW_SIZE;
#else
				rPos += remainedLen;
#endif
				break;
			}
		}
		return readedLen;
	}

	OF_VOID write(const OF_BYTE_PTR data, OF_INT len)
	{
		bool isVolatile = this->isVolatile || this->bufferedIO;

		int writtenLen = 0, remainedLen;

		while (writtenLen < len)
		{
			remainedLen = len - writtenLen;
			if (remainedLen >= OF_HW_MAX_RW_SIZE)
			{
				if (isVolatile)
				{
					memcpy(as + wPos, (const OF_VOID_PTR)(data + writtenLen), OF_HW_MAX_RW_SIZE);
				}
				else
				{
					fs->seekp(wPos, ios::beg);
					fs->write((char*)(data + writtenLen), OF_HW_MAX_RW_SIZE);
				}
				wPos += OF_HW_MAX_RW_SIZE;
				writtenLen += OF_HW_MAX_RW_SIZE;
			}
			else
			{
				if (isVolatile)
				{
					memcpy(as + wPos, (const OF_VOID_PTR)(data + writtenLen), remainedLen);
				}
				else
				{
					fs->seekp(wPos, ios::beg);
					fs->write((char*)(data + writtenLen), remainedLen);
				}
#if OF_HW_MIN_RW_SIZE > 1
				wPos += remainedLen > OF_HW_MIN_RW_SIZE ? OF_HW_MAX_RW_SIZE : OF_HW_MIN_RW_SIZE;
#else
				wPos += remainedLen;
#endif
				break;
			}
		}
	}

	OF_VOID flush()
	{
		if (isVolatile)
		{
			return;
		}
		if (bufferedIO)
		{
			fs->seekp(0, ios::beg);
			fs->write((char*)as, OF_CONST_TOTAL_SIZE);
		}
		fs->flush();
	}

	OF_BOOL isAuthenticated()
	{
		return OF_TRUE;
	}

	OF_VOID_PTR getFSCryptContext(OF_ULONG_PTR blockLenBytes, OF_BOOL isEncryption)
	{
		aes_context* aes = new aes_context;
		const OF_BYTE AES_SECRET_KEY[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf };

		if (isEncryption)
		{
			aes_setkey_enc(aes, AES_SECRET_KEY, sizeof(AES_SECRET_KEY)* 8);
		}
		else
		{
			aes_setkey_dec(aes, AES_SECRET_KEY, sizeof(AES_SECRET_KEY)* 8);
		}

		*blockLenBytes = 16;//set to algorithm's block length

		return aes;
	}

	OF_BOOL doFSCrypt(OF_VOID_PTR ctx, OF_BYTE_PTR inputBlock, OF_BYTE_PTR outputBlock)
	{
		if (ctx)
		{
			aes_context* aes = (aes_context*)ctx;
			aes_crypt_ecb(aes, aes->mode, inputBlock, outputBlock);
			return OF_TRUE;
		}
		return OF_FALSE;
	}

	OF_VOID freeFSCryptContext(OF_VOID_PTR ctx)
	{
		aes_context* aes = (aes_context*)ctx;
		delete aes;
	}

	OF_UINT writeMetaData(OF_METADATA_HANDLE hMetaData, const OF_VOID_PTR data, OF_UINT len)
	{
		return 0;//not needed for this test class
	}

	OF_UINT readMetaData(OF_METADATA_HANDLE hMetaData, OF_VOID_PTR data, OF_UINT pLen)
	{
		return 0;//not needed for this test class
	}

	OF_BOOL canFormatSector(OF_UINT sectorIndex, OF_BOOL allowFormatAllSectorVirtualPages)
	{
		return OF_TRUE;//we assume all sectors contains only one page
	}

	OF_BOOL formatSector(OF_UINT sectorIndex)
	{
		wPos = OF_HW_BASE_ADDRESS;
		for (OF_ULONG i = 0; i < sectorIndex; i++)
		{
			wPos += getSectorSize(i);
		}
		if (isVolatile || bufferedIO)
		{
			memset(as + wPos, OF_CONST_FREE_QWORD_VALUE, getSectorSize(sectorIndex));
		}
		else
		{
			fs->seekp(wPos, ios::beg);
			for (OF_ULONG i = getSectorSize(sectorIndex); i > 0; i--)
			{
				fs->put((char) OF_CONST_FREE_QWORD_VALUE);
			}
		}
		return OF_TRUE;
	}

	OF_UINT getSectorSize(OF_UINT sectorIndex)
	{
		return 2048;//all of our sectors have same length
	}

	OF_VOID enableIRQ()
	{
		//nothing to do
	}

	OF_VOID disableIRQ()
	{
		//nothing to do
	}

	OF_VOID_PTR malloc(OF_ULONG size)
	{
		return ::malloc(size);
	}

	OF_VOID_PTR realloc(OF_VOID_PTR mem, OF_ULONG newSize)
	{
		return ::realloc(mem, newSize);
	}

	OF_VOID free(OF_VOID_PTR mem)
	{
		return ::free(mem);
	}

	OF_VOID memset(OF_VOID_PTR dst, OF_BYTE value, OF_ULONG size)
	{
		::memset(dst, value, size);
	}

	OF_VOID memcpy(OF_VOID_PTR dst, const OF_VOID_PTR src, OF_ULONG size)
	{
		::memcpy(dst, src, size);
	}

	OF_INT memcmp(const OF_VOID_PTR buf1, const OF_VOID_PTR buf2, OF_ULONG size)
	{
		return ::memcmp(buf1, buf2, size);
	}
};

#define ASSERT(bool_cond, format, ...)\
	if (!bool_cond){ printf("[%s line %d] " format "\n", __FILE__, __LINE__, __VA_ARGS__); getchar(); return 0; }

int main(int argc, char** argv)
{
	//creating filesystem object using new FileDeviceInterface instance
	OFFSET fs(new FileDeviceInterface("SIMULATION.OFFSET"));

	OF_OBJECT_HANDLE hObj, hObj2;
	OF_BYTE buf1[256], buf2[256];
	OF_ULONG len1, len2, len3, itr;
	OF_UINT attrItr;
	OF_ATTRIBUTE_TYPE attrType;
	OF_RV rv;
	
	//filling buf1 with random data
	srand((int)time(0));
	for (len1 = 0; len1 < sizeof(buf1); len1++)
	{
		buf1[len1] = rand() % 256;
	}

	//setting hObj to OF_INVALID_OBJECT_HANDLE in order to create new FS object
	hObj = OF_INVALID_OBJECT_HANDLE;
	attrType = 1;

	//create a new FS object with attribute type 1 (non-private and non-sensitive)
	rv = fs.setObjectAttribute(&hObj, attrType, buf1, len1, OF_FALSE, OF_FALSE);
	ASSERT(rv == OFR_OK, "Failed to create object with error code %04X", rv);

	ASSERT(fs.getObjectAttributeCount(hObj) == 1, "Invalid attribute count");

	rv = fs.getObjectAttribute(hObj, attrType, &len2, buf2);
	ASSERT(rv == OFR_OK, "Failed to get object attribute with error code %04X", rv);

	ASSERT(len1 == len2, "Invalid length for newly created attribute (should be %d but got %d)", len1, len2);

	ASSERT(memcmp(buf1, buf2, len2) == 0, "Invalid value fetched for newly created attribute");
	
	//adding new attribute type 2 (private & sensitive) to created object
	attrType = 2;
	rv = fs.setObjectAttribute(&hObj, attrType, buf1, len1, OF_TRUE, OF_TRUE);
	ASSERT(rv == OFR_OK, "Failed to add object attribute with error code 0x%04X", rv);

	len3 = 2;
	ASSERT(fs.getObjectAttributeCount(hObj) == len3, "Invalid attribute count");

	rv = fs.getObjectAttribute(hObj, attrType, &len2, buf2);
	ASSERT(rv == OFR_OK, "Failed to get object attribute with error code 0x%04X", rv);

	ASSERT(len1 == len2, "Invalid length for newly created (private sensitive) attribute (should be %d but got %d)", len1, len2);

	ASSERT(memcmp(buf1, buf2, len2) == 0, "Invalid value fetched for newly created (private sensitive) attribute");

	len3 = fs.getObjectSize(hObj);
	ASSERT(len3 - (len1 + len2) < 0, "Unexpected length for newly create object (expected greater than %d, got %d)", len1 + len2, len3);
	
	itr = 0;
	attrItr = (OF_UINT)-1;//iterate on all object attributes
	ASSERT(fs.iterateOnObjectAttributes(hObj, &itr, &attrItr, &attrType, OF_TRUE), "Error while iterating on object attributes");
	ASSERT((attrType == 1 || attrType == 2), "Invalid attribute type, expected 0x0001, got 0x%04X", attrType);

	ASSERT(fs.iterateOnObjectAttributes(hObj, &itr, &attrItr, &attrType, OF_TRUE), "Error while iterating on object attributes");
	ASSERT((attrType == 1 || attrType == 2), "Invalid attribute type, expected 0x0001, got 0x%04X", attrType);
	
	ASSERT(fs.iterateOnObjectAttributes(hObj, &itr, &attrItr, &attrType) == OF_FALSE, "Invalid iteration on object attributes");
	
	attrType = 2;
	rv = fs.deleteObjectAttribute(hObj, attrType);
	ASSERT(rv == OFR_OK, "Failed to delete object attribute with error code 0x%04X", rv);

	rv = fs.deleteObjectAttribute(hObj, attrType);
	ASSERT(rv == OFR_OBJECT_HANDLE_INVALID, "Failed to duplicate object with error code 0x%04X", rv);

	rv = fs.duplicateObject(hObj, &hObj2);
	ASSERT(rv == OFR_OK, "Failed to duplicate object with error code 0x%04X", rv);

	rv = fs.destroyObject(hObj);
	ASSERT(rv == OFR_OK, "Failed to delete object with error code 0x%04X", rv);

	len3 = 1;
	ASSERT(fs.getObjectAttributeCount(hObj2) == len3, "Invalid attribute count");

	rv = fs.deleteObjectAttribute(hObj2, attrType);
	ASSERT(rv == OFR_OBJECT_HANDLE_INVALID, "Failed to duplicate object with error code 0x%04X", rv);

	attrType = 1;
	rv = fs.deleteObjectAttribute(hObj2, attrType);
	ASSERT(rv == OFR_OK, "Failed to delete object attribute with error code 0x%04X", rv);

	printf( "Test completed successfully!" "\n" );

	return 0;
}
