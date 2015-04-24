/*
 *  kernelcache.c
 *  kext_tools
 *
 *  Created by Nik Gervae on 2010 10 04.
 *  Copyright 2010, 2012 Apple Computer, Inc. All rights reserved.
 *
 */

#include "kernelcache.h"
#include "compression.h"

#include <mach-o/arch.h>
#include <mach-o/fat.h>
#include <mach-o/swap.h>
#include <sys/mman.h>

#include <IOKit/kext/OSKext.h>
#include <IOKit/kext/OSKextPrivate.h>

#undef  TIMESPEC_TO_TIMEVAL
#define	TIMESPEC_TO_TIMEVAL(tv, ts) { \
(tv)->tv_sec = (ts)->tv_sec; \
(tv)->tv_usec = (int)((ts)->tv_nsec / 1000); \
} while (/*CONSTCOND*/0)

/*******************************************************************************
*******************************************************************************/
ExitStatus 
writeFatFile(
    const char                * filePath,
    CFArrayRef                  fileSlices,
    CFArrayRef                  fileArchs,
    mode_t                      fileMode,
    const struct timeval        fileTimes[2])
{
    ExitStatus        result               = EX_SOFTWARE;
    char              tmpPath[PATH_MAX];
    struct fat_header fatHeader;
    struct fat_arch   fatArch;
    CFDataRef         sliceData            = NULL;    // do not release
    const uint8_t   * sliceDataPtr         = NULL;    // do not free
    const char *      tmpPathPtr           = tmpPath; // must unlink
    const NXArchInfo * targetArch          = NULL;    // do not free
    mode_t             procMode            = 0;
    uint32_t          fatOffset            = 0;
    uint32_t          sliceLength          = 0;
    int               fileDescriptor       = -1;      // must close
    int                numArchs            = 0;
    int               i                    = 0;

    /* Make the temporary file */

    strlcpy(tmpPath, filePath, sizeof(tmpPath));
    if (strlcat(tmpPath, ".XXXX", sizeof(tmpPath)) >= sizeof(tmpPath)) {
        OSKextLogStringError(/* kext */ NULL);
        goto finish;
    }

    fileDescriptor = mkstemp(tmpPath);
    if (-1 == fileDescriptor) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                "Can't create %s - %s.",
                tmpPath, strerror(errno));
        goto finish;
    }

    /* Set the file's permissions */

    /* Set the umask to get it, then set it back to iself. Wish there were a
     * better way to query it.
     */
    procMode = umask(0);
    umask(procMode);

    if (-1 == fchmod(fileDescriptor, fileMode & ~procMode)) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                "Can't set permissions on %s - %s.",
                tmpPathPtr, strerror(errno));
    }

    /* Write out the fat headers even if there's only one arch so we know what
     * arch a compressed prelinked kernel belongs to.
     */

    numArchs = (int)CFArrayGetCount(fileArchs);
    fatHeader.magic = OSSwapHostToBigInt32(FAT_MAGIC);
    fatHeader.nfat_arch = OSSwapHostToBigInt32(numArchs);

    result = writeToFile(fileDescriptor, (const UInt8 *)&fatHeader,
        sizeof(fatHeader));
    if (result != EX_OK) {
        goto finish;
    }

    fatOffset = sizeof(struct fat_header) +
        (sizeof(struct fat_arch) * numArchs);

    for (i = 0; i < numArchs; i++) {
        targetArch = CFArrayGetValueAtIndex(fileArchs, i);
        sliceData = CFArrayGetValueAtIndex(fileSlices, i);
        sliceLength = (uint32_t)CFDataGetLength(sliceData);

        fatArch.cputype = OSSwapHostToBigInt32(targetArch->cputype);
        fatArch.cpusubtype = OSSwapHostToBigInt32(targetArch->cpusubtype);
        fatArch.offset = OSSwapHostToBigInt32(fatOffset);
        fatArch.size = OSSwapHostToBigInt32(sliceLength);
        fatArch.align = OSSwapHostToBigInt32(0);

        result = writeToFile(fileDescriptor, 
            (UInt8 *)&fatArch, sizeof(fatArch));
        if (result != EX_OK) {
            goto finish;
        }

        fatOffset += sliceLength;
    }

    /* Write out the file slices */

    for (i = 0; i < numArchs; i++) {
        sliceData = CFArrayGetValueAtIndex(fileSlices, i);
        sliceDataPtr = CFDataGetBytePtr(sliceData);
        sliceLength = (uint32_t)CFDataGetLength(sliceData);

        result = writeToFile(fileDescriptor, sliceDataPtr, sliceLength);
        if (result != EX_OK) {
            goto finish;
        }
    }

    OSKextLog(/* kext */ NULL,
        kOSKextLogDebugLevel | kOSKextLogFileAccessFlag,
        "Renaming temp file to %s.",
        filePath);

    /* Move the file to its final path */

    if (rename(tmpPathPtr, filePath) != 0) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                "Can't rename temporary file %s to %s - %s.",
                tmpPathPtr, filePath, strerror(errno));
        result = EX_OSERR;
        goto finish;
    }
    tmpPathPtr = NULL;
    
    /* Update the file's mod time if necessary */
    
    if (utimes(filePath, fileTimes)) {
        OSKextLog(/* kext */ NULL,
            kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
            "Can't update mod time of %s - %s.", filePath, strerror(errno));
    }

finish:

    if (fileDescriptor >= 0) (void)close(fileDescriptor);
    if (tmpPathPtr) unlink(tmpPathPtr);

    return result;
}

/*******************************************************************************
*******************************************************************************/
void *
mapAndSwapFatHeaderPage(
    int fileDescriptor)
{
    void              * result          = NULL; 
    void              * headerPage      = NULL;  // must unmapFatHeaderPage()
    struct fat_header * fatHeader       = NULL;  // do not free
    struct fat_arch   * fatArch         = NULL;  // do not free

    /* Map the first page to read the fat headers. */

    headerPage = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE, 
        MAP_FILE | MAP_PRIVATE, fileDescriptor, 0);
    if (MAP_FAILED == headerPage) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                "Failed to map file header page.");
        goto finish;
    }

    /* Make sure that the fat header, if any, is swapped to the host's byte
     * order.
     */

    fatHeader = (struct fat_header *) headerPage;
    fatArch = (struct fat_arch *) (&fatHeader[1]);

    if (fatHeader->magic == FAT_CIGAM) {
        swap_fat_header(fatHeader, NXHostByteOrder());
        swap_fat_arch(fatArch, fatHeader->nfat_arch, NXHostByteOrder());
    }

    result = headerPage;
    headerPage = NULL;

finish:
    if (headerPage) unmapFatHeaderPage(headerPage);

    return result;
}

/*******************************************************************************
*******************************************************************************/
void
unmapFatHeaderPage(
    void *headerPage)
{
    munmap(headerPage, PAGE_SIZE);
}

/*******************************************************************************
*******************************************************************************/
struct fat_arch *
getFirstFatArch(
    u_char *headerPage)
{
    struct fat_header * fatHeader       = NULL;
    struct fat_arch   * fatArch         = NULL;

    fatHeader = (struct fat_header *) headerPage;
    if (fatHeader->magic != FAT_MAGIC || !fatHeader->nfat_arch) {
            goto finish;
        }

    fatArch = (struct fat_arch *) (&fatHeader[1]);

finish:
    return fatArch;
}

/*******************************************************************************
*******************************************************************************/
struct fat_arch *
getNextFatArch(
    u_char *headerPage, 
    struct fat_arch *prevArch)
{
    struct fat_header * fatHeader       = NULL;
    struct fat_arch   * firstArch       = NULL;
    struct fat_arch   * nextArch        = NULL;
    unsigned int numArchs;

    fatHeader = (struct fat_header *) headerPage;
    if (fatHeader->magic != FAT_MAGIC) {
        goto finish;
    }

    firstArch = (struct fat_arch *) (&fatHeader[1]);
    nextArch = &prevArch[1];
    numArchs = (unsigned int)(nextArch - firstArch);

    if (numArchs >= fatHeader->nfat_arch) {
        nextArch = NULL;
        goto finish;
    }

finish:
    return nextArch;
}

/*******************************************************************************
*******************************************************************************/
struct fat_arch *
getFatArchForArchInfo(
    u_char *headerPage, 
    const NXArchInfo *archInfo)
{
    struct fat_header * fatHeader       = NULL;
    struct fat_arch   * fatArch         = NULL;

    fatHeader = (struct fat_header *)headerPage;

    if (fatHeader->magic != FAT_MAGIC) {
        goto finish;
    }

    fatArch = (struct fat_arch *)(&fatHeader[1]);
    fatArch = NXFindBestFatArch(archInfo->cputype, archInfo->cpusubtype, 
        fatArch, fatHeader->nfat_arch);

finish:
    return fatArch;
}

/*******************************************************************************
*******************************************************************************/
const NXArchInfo * 
getThinHeaderPageArch(
    const void *headerPage)
{
    const NXArchInfo          * result          = NULL;
    struct mach_header        * machHdr         = NULL;
    struct mach_header_64     * machHdr64       = NULL;
    Boolean                     is32Bit         = true;

    machHdr = (struct mach_header *) headerPage;
    machHdr64 = (struct mach_header_64 *) headerPage;

    switch (machHdr->magic) {
    case MH_MAGIC:
        break;
    case MH_MAGIC_64:
        is32Bit = false;
        break;
    case MH_CIGAM:
        swap_mach_header(machHdr, NXHostByteOrder());
        break;
    case MH_CIGAM_64:
        swap_mach_header_64(machHdr64, NXHostByteOrder());
        is32Bit = false;
        break;
    default:
        goto finish;
    }

    if (is32Bit) {
        machHdr64 = NULL;
        result = NXGetArchInfoFromCpuType(machHdr->cputype, 
            machHdr->cpusubtype);
    } else {
        machHdr = NULL;
        result = NXGetArchInfoFromCpuType(machHdr64->cputype,
            machHdr64->cpusubtype);
    }

finish:
    return result;
}

/*******************************************************************************
*******************************************************************************/
ExitStatus
readFatFileArchsWithPath(
    const char        * filePath,
    CFMutableArrayRef * archsOut)
{
    ExitStatus          result          = EX_SOFTWARE;
    void              * headerPage      = NULL;         // must unmapFatHeaderPage()
    int                 fileDescriptor  = 0;            // must close()

    /* Open the file. */

    fileDescriptor = open(filePath, O_RDONLY);
    if (fileDescriptor < 0) {
        goto finish;
    }

    /* Map the fat headers in */

    headerPage = mapAndSwapFatHeaderPage(fileDescriptor);
    if (!headerPage) {
        goto finish;
    }

    result = readFatFileArchsWithHeader(headerPage, archsOut);
finish:
    if (fileDescriptor >= 0) close(fileDescriptor);
    if (headerPage) unmapFatHeaderPage(headerPage);

    return result;
}

/*******************************************************************************
*******************************************************************************/
Boolean archInfoEqualityCallback(const void *v1, const void *v2)
{
    const NXArchInfo *a1 = (const NXArchInfo *)v1;
    const NXArchInfo *a2 = (const NXArchInfo *)v2;

    return ((a1->cputype == a2->cputype) && (a1->cpusubtype == a2->cpusubtype));
}

/*******************************************************************************
*******************************************************************************/
ExitStatus
readFatFileArchsWithHeader(
    u_char            * headerPage,
    CFMutableArrayRef * archsOut)
{
    ExitStatus          result          = EX_SOFTWARE;
    CFMutableArrayRef   fileArchs       = NULL;         // must release
    struct fat_arch   * fatArch         = NULL;         // do not free
    const NXArchInfo  * archInfo        = NULL;         // do not free
    CFArrayCallBacks    callbacks       = { 0, NULL, NULL, NULL, archInfoEqualityCallback };

    /* Create an array to hold the fat archs */

    if (!createCFMutableArray(&fileArchs, &callbacks))
    {
        OSKextLogMemError();
        result = EX_OSERR;
        goto finish;
    }

    /* Read the archs */

    fatArch = getFirstFatArch(headerPage);
    if (fatArch) {
        while (fatArch) {
            archInfo = NXGetArchInfoFromCpuType(fatArch->cputype,
                fatArch->cpusubtype);
            CFArrayAppendValue(fileArchs, archInfo);

            fatArch = getNextFatArch(headerPage, fatArch);
        }
    } else {
        archInfo = getThinHeaderPageArch(headerPage);
        if (archInfo) {
            CFArrayAppendValue(fileArchs, archInfo);
        } else {
            // We can't determine the arch information, so don't return any
            archsOut = NULL;
        }
    }

    if (archsOut) *archsOut = (CFMutableArrayRef) CFRetain(fileArchs);
    result = EX_OK;

finish:
    SAFE_RELEASE(fileArchs);

    return result;
}

/*******************************************************************************
*******************************************************************************/
ExitStatus
readMachOSlices(
    const char        * filePath,
    CFMutableArrayRef * slicesOut,
    CFMutableArrayRef * archsOut,
    mode_t            * modeOut,
    struct timeval      machOTimesOut[2])
{
    struct stat         statBuf;
    ExitStatus          result          = EX_SOFTWARE;
    CFMutableArrayRef   fileSlices      = NULL;         // release
    CFMutableArrayRef   fileArchs       = NULL;         // release
    CFDataRef           sliceData       = NULL;         // release
    u_char            * fileBuf         = NULL;         // must free
    void              * headerPage      = NULL;         // must unmapFatHeaderPage()
    struct fat_arch   * fatArch         = NULL;         // do not free
    int                 fileDescriptor  = 0;            // must close()

    /* Create an array to hold the fat slices */

    if (!createCFMutableArray(&fileSlices, &kCFTypeArrayCallBacks))
    {
        OSKextLogMemError();
        result = EX_OSERR;
        goto finish;
    }

    /* Open the file. */

    fileDescriptor = open(filePath, O_RDONLY);
    if (fileDescriptor < 0) {
        goto finish;
    }

    if (fstat(fileDescriptor, &statBuf)) {
        goto finish;
    }

    /* Map the fat headers in */

    headerPage = mapAndSwapFatHeaderPage(fileDescriptor);
    if (!headerPage) {
        goto finish;
    }

    /* If the file is fat, read the slices into separate objects.  If not,
     * read the whole file into one large slice.
     */

    fatArch = getFirstFatArch(headerPage);
    if (fatArch) {
        while (fatArch) {
            sliceData = readMachOSlice(fileDescriptor,
                fatArch->offset, fatArch->size);
            if (!sliceData) goto finish;

            CFArrayAppendValue(fileSlices, sliceData);
            fatArch = getNextFatArch(headerPage, fatArch);
            CFRelease(sliceData); // drop ref from readMachOSlice()
            sliceData = NULL;
        }
    } else {
        sliceData = readMachOSlice(fileDescriptor, 0, (size_t)statBuf.st_size);
        if (!sliceData) goto finish;

        CFArrayAppendValue(fileSlices, sliceData);
    }

    if (archsOut) {
        result = readFatFileArchsWithHeader(headerPage, &fileArchs);
        if (result != EX_OK) {
            goto finish;
        }

        if (!fileArchs) archsOut = NULL;
    }

    result = EX_OK;
    if (slicesOut) *slicesOut = (CFMutableArrayRef) CFRetain(fileSlices);
    if (archsOut) *archsOut = (CFMutableArrayRef) CFRetain(fileArchs);
    if (modeOut) *modeOut = statBuf.st_mode;
    if (machOTimesOut) {
        TIMESPEC_TO_TIMEVAL(&machOTimesOut[0], &statBuf.st_atimespec);
        TIMESPEC_TO_TIMEVAL(&machOTimesOut[1], &statBuf.st_mtimespec);
    }

finish:
    SAFE_RELEASE(fileSlices);
    SAFE_RELEASE(fileArchs);
    SAFE_RELEASE(sliceData);
    SAFE_FREE(fileBuf);
    if (fileDescriptor >= 0) close(fileDescriptor);
    if (headerPage) unmapFatHeaderPage(headerPage);

    return result;
}

/*******************************************************************************
*******************************************************************************/
CFDataRef 
readMachOSliceForArch(
    const char        * filePath,
    const NXArchInfo  * archInfo,
    Boolean             checkArch)
{   
    CFDataRef           result          = NULL; // must release
    CFDataRef           fileData        = NULL; // must release
    void              * headerPage      = NULL; // must unmapFatHeaderPage()
    struct fat_header * fatHeader       = NULL; // do not free
    struct fat_arch   * fatArch         = NULL; // do not free
    int                 fileDescriptor  = 0;    // must close()
    off_t               fileSliceOffset = 0;
    size_t              fileSliceSize   = 0;
    struct stat statBuf;

    /* Open the file */

    fileDescriptor = open(filePath, O_RDONLY);
    if (fileDescriptor < 0) {
        goto finish;
    }

    /* Map the fat headers in */

    headerPage = mapAndSwapFatHeaderPage(fileDescriptor);
    if (!headerPage) {
        goto finish;
    }

    /* Find the slice for the target architecture */

    fatHeader = (struct fat_header *)headerPage;

    if (archInfo && fatHeader->magic == FAT_MAGIC) {
        fatArch = NXFindBestFatArch(archInfo->cputype, archInfo->cpusubtype, 
            (struct fat_arch *)(&fatHeader[1]), fatHeader->nfat_arch);
        if (!fatArch) {
            OSKextLog(/* kext */ NULL,
                    kOSKextLogErrorLevel | kOSKextLogGeneralFlag, 
                    "Fat file does not contain requested architecture %s.",
                    archInfo->name);
            goto finish;
        }

        fileSliceOffset = fatArch->offset;
        fileSliceSize = fatArch->size;
    } else {
        if (fstat(fileDescriptor, &statBuf)) {
            goto finish;
        }

        fileSliceOffset = 0;
        fileSliceSize = (size_t)statBuf.st_size;
    }

    /* Read the file */    

    fileData = readMachOSlice(fileDescriptor, fileSliceOffset, 
        fileSliceSize);
    if (!fileData) {
        goto finish;
    }

    /* Verify that the file is of the right architecture */

    if (checkArch) {
        if (verifyMachOIsArch(CFDataGetBytePtr(fileData), 
                fileSliceSize, archInfo)) 
        {
            goto finish;
        }
    }

    result = CFRetain(fileData);

finish:
    SAFE_RELEASE(fileData);
    if (fileDescriptor >= 0) close(fileDescriptor);
    if (headerPage) unmapFatHeaderPage(headerPage);

    return result;
}

/*******************************************************************************
*******************************************************************************/
CFDataRef 
readMachOSlice(
    int         fileDescriptor,
    off_t       fileOffset,
    size_t      fileSliceSize)
{
    CFDataRef   fileData        = NULL;  // do not release
    u_char    * fileBuf         = NULL;  // must free
    off_t       seekedBytes     = 0;
    ssize_t     readBytes       = 0;
    size_t      totalReadBytes  = 0;

    /* Allocate a buffer for the file */

    fileBuf = malloc(fileSliceSize);
    if (!fileBuf) {
        OSKextLogMemError();
        goto finish;
    }

    /* Seek to the specified file offset */

    seekedBytes = lseek(fileDescriptor, fileOffset, SEEK_SET);
    if (seekedBytes != fileOffset) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                "Failed to seek in file.");  // xxx - which file is that?
        goto finish;
    }

    /* Read the file's bytes into the buffer */

    while (totalReadBytes < fileSliceSize) {
        readBytes = read(fileDescriptor, fileBuf + totalReadBytes,
                fileSliceSize - totalReadBytes);
        if (readBytes < 0) {
            OSKextLog(/* kext */ NULL,
                    kOSKextLogErrorLevel | kOSKextLogFileAccessFlag,
                    "Failed to read file.");
            goto finish;
        }

        totalReadBytes += (size_t) readBytes;
    }

    /* Wrap the file slice in a CFData object */

    fileData = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, 
            (UInt8 *) fileBuf, fileSliceSize, kCFAllocatorMalloc);
    if (!fileData) {
        goto finish;
    }
    fileBuf = NULL;

finish:
    SAFE_FREE(fileBuf);

    return fileData;
}

/*******************************************************************************
*******************************************************************************/
int 
verifyMachOIsArch(
    const UInt8      * fileBuf, 
    size_t             size,
    const NXArchInfo * archInfo)
{
    int result = -1;
    cpu_type_t cputype = 0;
    struct mach_header *checkHeader = (struct mach_header *)fileBuf;

    if (!archInfo) {
        result = 0;
        goto finish;
    }

    /* Get the cputype from the mach header */
    if (size < sizeof(uint32_t)) {
        goto finish;
    }

    if (checkHeader->magic == MH_MAGIC_64 || checkHeader->magic == MH_CIGAM_64) {
        struct mach_header_64 *machHeader = 
            (struct mach_header_64 *) fileBuf;

        if (size < sizeof(*machHeader)) {
            goto finish;
        }

        cputype = machHeader->cputype;
        if (checkHeader->magic == MH_CIGAM_64) cputype = OSSwapInt32(cputype);
    } else if (checkHeader->magic == MH_MAGIC || checkHeader->magic == MH_CIGAM) {
        struct mach_header *machHeader = 
            (struct mach_header *) fileBuf;

        if (size < sizeof(*machHeader)) {
            goto finish;
        }

        cputype = machHeader->cputype;
        if (checkHeader->magic == MH_CIGAM) cputype = OSSwapInt32(cputype);
    }

    /* Make sure the file's cputype matches the host's.
     */
    if (result == 0 && cputype != archInfo->cputype) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogGeneralFlag,
                "File is not of expected architecture %s.", archInfo->name);
        goto finish;
    }

    result = 0;
finish:
    return result;
}

/*********************************************************************
 *********************************************************************/
CFDataRef 
uncompressPrelinkedSlice(
    CFDataRef      prelinkImage)
{
    CFDataRef                     result              = NULL;
    CFMutableDataRef              uncompressedImage   = NULL;  // must release
    const PrelinkedKernelHeader * prelinkHeader       = NULL;  // do not free
    unsigned char               * buf                 = NULL;  // do not free
    vm_size_t                     bufsize             = 0;
    vm_size_t                     uncompsize          = 0;
    uint32_t                      adler32             = 0;

    prelinkHeader = (PrelinkedKernelHeader *) CFDataGetBytePtr(prelinkImage);

    /* Verify the header information.
     */
    if (prelinkHeader->signature != OSSwapHostToBigInt32('comp')) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Compressed prelinked kernel has invalid signature: 0x%x.", 
                prelinkHeader->signature);
        goto finish;
    }

    if ((prelinkHeader->compressType != OSSwapHostToBigInt32('lzss')) &&
        (prelinkHeader->compressType != OSSwapHostToBigInt32('lzvn'))) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Compressed prelinked kernel has invalid compressType: 0x%x.", 
                prelinkHeader->compressType);
        goto finish;
    }

    /* Create a buffer to hold the uncompressed kernel.
     */
    bufsize = OSSwapBigToHostInt32(prelinkHeader->uncompressedSize);
    uncompressedImage = CFDataCreateMutable(kCFAllocatorDefault, bufsize);
    if (!uncompressedImage) {
        goto finish;
    }

    /* We have to call CFDataSetLength explicitly to get CFData to allocate
     * its internal buffer.
     */
    CFDataSetLength(uncompressedImage, bufsize);
    buf = CFDataGetMutableBytePtr(uncompressedImage);
    if (!buf) {
        OSKextLogMemError();
        goto finish;
    }

    /* Uncompress the kernel.
     */
    if (prelinkHeader->compressType == OSSwapHostToBigInt32('lzvn'))
    {
        uncompsize = lzvn_decode(buf, (u_int32_t)bufsize,
                                 ((u_int8_t *)(CFDataGetBytePtr(prelinkImage))) + sizeof(*prelinkHeader),
                                 (u_int32_t)(CFDataGetLength(prelinkImage) - sizeof(*prelinkHeader)));
    } else {
        uncompsize = decompress_lzss(buf, (u_int32_t)bufsize,
                                     ((u_int8_t *)(CFDataGetBytePtr(prelinkImage))) + sizeof(*prelinkHeader),
                                     (u_int32_t)(CFDataGetLength(prelinkImage) - sizeof(*prelinkHeader)));
    }

    if (uncompsize != bufsize) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Compressed prelinked kernel uncompressed to an unexpected size: %u.",
                (unsigned)uncompsize);
        goto finish;
    }

    /* Verify the adler32.
     */
    adler32 = local_adler32((u_int8_t *) buf, (int)bufsize);
    if (prelinkHeader->adler32 != OSSwapHostToBigInt32(adler32)) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Checksum error for compressed prelinked kernel.");
        goto finish;
    }

    result = CFRetain(uncompressedImage);

finish:
    SAFE_RELEASE(uncompressedImage);
    return result;
}

/*********************************************************************
 *********************************************************************/
CFDataRef 
compressPrelinkedSlice(
    CFDataRef      prelinkImage, 
    Boolean        hasRelocs)
{
    CFDataRef               result          = NULL;
    CFMutableDataRef        compressedImage = NULL;  // must release
    PrelinkedKernelHeader * kernelHeader    = NULL;  // do not free
    const PrelinkedKernelHeader * kernelHeaderIn = NULL; // do not free
    unsigned char         * buf             = NULL;  // do not free
    unsigned char         * bufend          = NULL;  // do not free
    u_long                  offset          = 0;
    vm_size_t               bufsize         = 0;
    vm_size_t               compsize        = 0;
    uint32_t                adler32         = 0;

    /* Check that the kernel is not already compressed */

    kernelHeaderIn = (const PrelinkedKernelHeader *) 
        CFDataGetBytePtr(prelinkImage);
    if (kernelHeaderIn->signature == OSSwapHostToBigInt('comp')) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Prelinked kernel is already compressed.");
        goto finish;
    }

    /* Create a buffer to hold the compressed kernel */

    offset = sizeof(*kernelHeader);
    bufsize = CFDataGetLength(prelinkImage) + offset;
    compressedImage = CFDataCreateMutable(kCFAllocatorDefault, bufsize);
    if (!compressedImage) {
        goto finish;
    }

    /* We have to call CFDataSetLength explicitly to get CFData to allocate
     * its internal buffer.
     */
    CFDataSetLength(compressedImage, bufsize);
    buf = CFDataGetMutableBytePtr(compressedImage);
    if (!buf) {
        OSKextLogMemError();
        goto finish;
    }

    kernelHeader = (PrelinkedKernelHeader *) buf;
    bzero(kernelHeader, sizeof(*kernelHeader));
    kernelHeader->prelinkVersion = OSSwapHostToBigInt32(hasRelocs ? 1 : 0);

    /* Fill in the compression information */

    kernelHeader->signature = OSSwapHostToBigInt32('comp');

#if 0
    kernelHeader->compressType = OSSwapHostToBigInt32('lzvn');
#else
    kernelHeader->compressType = OSSwapHostToBigInt32('lzss');
#endif
    
    adler32 = local_adler32((u_int8_t *)CFDataGetBytePtr(prelinkImage),
            (int)CFDataGetLength(prelinkImage));
    kernelHeader->adler32 = OSSwapHostToBigInt32(adler32);
    kernelHeader->uncompressedSize = 
        OSSwapHostToBigInt32((__uint32_t)CFDataGetLength(prelinkImage));

#if 0
    /* Compress the kernel (LZVN) */
    bufend = compress_lzvn(buf + offset, (u_int32_t)bufsize,
            (u_int8_t *)CFDataGetBytePtr(prelinkImage),
            (u_int32_t)CFDataGetLength(prelinkImage));
#else
    /* Compress the kernel (LZSS) */
    bufend = compress_lzss(buf + offset, (u_int32_t)bufsize,
                           (u_int8_t *)CFDataGetBytePtr(prelinkImage),
                           (u_int32_t)CFDataGetLength(prelinkImage));
#endif

    if (!bufend) {
        OSKextLog(/* kext */ NULL,
                kOSKextLogErrorLevel | kOSKextLogArchiveFlag,
                "Failed to compress prelinked kernel.");
        goto finish;
    }

    compsize = bufend - (buf + offset);
    kernelHeader->compressedSize = OSSwapHostToBigInt32((__int32_t)compsize);
    CFDataSetLength(compressedImage, bufend - buf);

    result = CFRetain(compressedImage);

finish:
    SAFE_RELEASE(compressedImage);
    return result;
}

/*******************************************************************************
*******************************************************************************/
ExitStatus
writePrelinkedSymbols(
    CFURLRef    symbolDirURL,
    CFArrayRef  prelinkSymbols,
    CFArrayRef  prelinkArchs)
{
    SaveFileContext saveFileContext;
    ExitStatus          result          = EX_SOFTWARE;
    CFDictionaryRef     sliceSymbols    = NULL; // do not release
    CFURLRef            saveDirURL      = NULL; // must release
    const NXArchInfo  * archInfo        = NULL; // do not free
    CFIndex             numArchs        = 0;
    CFIndex             i               = 0;

    saveFileContext.overwrite = true;
    saveFileContext.fatal = false;
    numArchs = CFArrayGetCount(prelinkArchs);

    for (i = 0; i < numArchs; ++i) {
        archInfo = CFArrayGetValueAtIndex(prelinkArchs, i);
        sliceSymbols = CFArrayGetValueAtIndex(prelinkSymbols, i);

        SAFE_RELEASE_NULL(saveDirURL);

        /* We don't need arch-specific directories if there's only one arch */
        if (numArchs == 1) {
            saveDirURL = CFRetain(symbolDirURL);
        } else {
            saveDirURL = CFURLCreateFromFileSystemRepresentationRelativeToBase(
                kCFAllocatorDefault, (const UInt8 *) archInfo->name, 
                strlen(archInfo->name), /* isDirectory */ true, symbolDirURL);
            if (!saveDirURL) {
                goto finish;
            }
        }

        result = makeDirectoryWithURL(saveDirURL);
        if (result != EX_OK) {
            goto finish;
        }

        saveFileContext.saveDirURL = saveDirURL;

        CFDictionaryApplyFunction(sliceSymbols, &saveFile, 
            &saveFileContext);
        if (saveFileContext.fatal) {
            goto finish;
        }
    }
    result = EX_OK;

finish:
    SAFE_RELEASE(saveDirURL);
        
    return result;
}

/*******************************************************************************
*******************************************************************************/
ExitStatus
makeDirectoryWithURL(
    CFURLRef dirURL)
{
    char dirPath[MAXPATHLEN];
    struct stat statBuf;
    ExitStatus result = EX_SOFTWARE;

    if (!CFURLHasDirectoryPath(dirURL)) {
        goto finish;
    }

    if (!CFURLGetFileSystemRepresentation(dirURL, /* resolveToBase */ true,
            (UInt8 *)dirPath, sizeof(dirPath))) 
    {
        goto finish;
    }

    result = stat(dirPath, &statBuf);

    /* If the directory exists, return success */
    if (result == 0 && statBuf.st_mode & S_IFDIR) {
        result = EX_OK;
        goto finish;
    }

    /* Die if the stat failed for any reason other than an invalid path */
    if (result == 0 || errno != ENOENT) {
        goto finish;
    }
    
    result = mkdir(dirPath, 0755);
    if (result != 0) {
        goto finish;
    }

    result = EX_OK;

finish:
    return result;
}
