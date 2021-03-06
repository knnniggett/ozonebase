#ifndef OZ_MEMORY_IO_H
#define OZ_MEMORY_IO_H

#include "ozFfmpeg.h"

class Image;

class MemoryIO
{
protected:
    std::string     mLocation;
    int             mMemoryKey;
    bool            mOwner;

    typedef struct
    {
        int         size;
        bool        valid;
        int         lastWriteIndex;
        time_t      lastWriteTime;
        PixelFormat imageFormat;
        uint16_t    imageWidth;
        uint16_t    imageHeight;
        FrameRate	frameRate;
        int         imageCount;
    } SharedData;

    struct Snapshot
    {
        uint64_t    *timestamp;
        Image       *image;
    };

#if OZ_MEM_MAPPED
    int             mMapFd;
    char            mMemFile[PATH_MAX];
#else // OZ_MEM_MAPPED
    int             mShmId;
#endif // OZ_MEM_MAPPED
    int             mMemSize;
    unsigned char   *mMemPtr;

    SharedData      *mSharedData;
    Snapshot        *mImageBuffer;

protected:
    bool queryMemory( SharedData *sharedData );
    void attachMemory( int imageCount, PixelFormat imageFormat, uint16_t imageWidth, uint16_t imageHeight );
    void detachMemory();

public:
    MemoryIO( const std::string &location, int id, bool owner );
    ~MemoryIO();
};

#endif // OZ_MEMORY_IO_H
