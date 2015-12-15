#ifndef ___DEVICE_INTERFACE_H_INC___
#define ___DEVICE_INTERFACE_H_INC___

#include "definitions.h"

class DeviceInterface
{
public:
	/**
	 * return true if user access is granted, false otherwise.
	 **/
	virtual OF_BOOL isAuthenticated() = 0;


	/**
	 * return encryption/decryption context or NULL_PTR if no encryption/decryption allowed
	 * this method should also set algorithm block length in bytes
	 **/
	virtual OF_VOID_PTR getFSCryptContext(OF_ULONG_PTR blockLenBytes, OF_BOOL isEncryption) = 0;
	
	/**
	 * encrypt/decrypt input data block into output data block
	 * return true if succeed or false in the case of failure
	 **/
	virtual OF_BOOL doFSCrypt(OF_VOID_PTR ctx, OF_BYTE_PTR inputBlock, OF_BYTE_PTR outputBlock) = 0;

	/**
	 * cleanup and free allocated memory for encryption/decryption context
	 **/
	virtual OF_VOID freeFSCryptContext(OF_VOID_PTR ctx) = 0;


	/**
	 * write metadata into anywhere outside of filesystem
	 **/
	virtual OF_UINT writeMetaData(OF_METADATA_HANDLE hMetaData, const OF_VOID_PTR data, OF_UINT len) = 0;

	/**
	 * read metadata from anywhere outside of filesystem
	 **/
	virtual OF_UINT readMetaData(OF_METADATA_HANDLE hMetaData, OF_VOID_PTR data, OF_UINT pLen) = 0;
	

	/**
	 * flush any possible unsaved changes into physical memory
	 **/
	virtual OF_VOID flush() = 0;

	/**
	 * read len bytes from physical memory into data buffer
	 * return number of readed bytes
	 **/
	virtual OF_INT read(OF_BYTE_PTR data, OF_INT len) = 0;

	/**
	 * write len bytes of data buffer into physical memory
	 **/
	virtual OF_VOID write(const OF_BYTE_PTR data, OF_INT len) = 0;

	/**
	 * mark current read position
	 **/
	virtual OF_VOID markg(OF_ULONG_PTR _rPos) = 0;

	/**
	 * mark current write position
	 **/
	virtual OF_VOID markp(OF_ULONG_PTR _wPos) = 0;

	/**
	 * restore to given read position
	 **/
	virtual OF_VOID restoreg(OF_ULONG _rPos) = 0;

	/**
	 * restore to given write position
	 **/
	virtual OF_VOID restorep(OF_ULONG _wPos) = 0;

	/**
	 * jump len bytes from current read position or from memory base address
	 **/
	virtual OF_VOID seekg(OF_INT len, OF_BOOL fromBegin = OF_FALSE) = 0;
	
	/**
	 * jump len bytes from current write position or from memory base address
	 **/
	virtual OF_VOID seekp(OF_INT len, OF_BOOL fromBegin = OF_FALSE) = 0;


	/**
	 * return true if sector which specified by index is formattable, false otherwise
	 * allowFormatAllSectorVirtualPages indicates that if specified sector contains more than one virtual page,
	 * then formatting those pages is allowed or not, so for single page sectors it can be ignored
	 **/
	virtual OF_BOOL canFormatSector(OF_UINT sectorIndex, OF_BOOL allowFormatAllSectorVirtualPages) = 0;

	/**
	 * format hardware sector which specified by index
	 **/
	virtual OF_BOOL formatSector(OF_UINT sectorIndex) = 0;

	/**
	 * return size of sector which specified by index
	 **/
	virtual OF_UINT getSectorSize(OF_UINT sectorIndex) = 0;


	/**
	 * enable Interrrupt Requests for device
	 **/
	virtual OF_VOID enableIRQ() = 0;

	/**
	 * disable Interrrupt Requests for device
	 **/
	virtual OF_VOID disableIRQ() = 0;


	/**
	 * allocate a size byte block of memory and return pointer to begining of allocated block
	 **/
	virtual OF_VOID_PTR malloc(OF_ULONG size) = 0;

	/**
	 * resize a block of memory to newSize bytes and return pointer to begining of new block
	 **/
	virtual OF_VOID_PTR realloc(OF_VOID_PTR mem, OF_ULONG newSize) = 0;

	/**
	 * free given block of memory
	 **/
	virtual OF_VOID free(OF_VOID_PTR mem) = 0;

	/**
	 * fill size bytes of dst buffer with given value
	 **/
	virtual OF_VOID memset(OF_VOID_PTR dst, OF_BYTE value, OF_ULONG size) = 0;
	
	/**
	 * copy size bytes of src buffer into dst
	 **/
	virtual OF_VOID memcpy(OF_VOID_PTR dst, const OF_VOID_PTR src, OF_ULONG size) = 0;

	/**
	 * compare size bytes of two given blocks of memory and return the result
	 **/
	virtual OF_INT memcmp(const OF_VOID_PTR dst, const OF_VOID_PTR src, OF_ULONG size) = 0;
};

#endif /*  ___DEVICE_INTERFACE_H_INC___  */