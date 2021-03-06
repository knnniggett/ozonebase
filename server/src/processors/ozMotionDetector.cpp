#include "../base/oz.h"
#include "ozMotionDetector.h"

#include "../base/ozAlarmFrame.h"
#include "../base/ozZone.h"

#include <sys/time.h>

bool writeImages = false;
std::string writeLocation = "/tmp/debug";

/**
* @brief 
*/
void MotionDetector::construct()
{
    mRefBlend = 128;
    mVarBlend = 512;

    mColourDeltas = false;

    mAlarmCount = 0;
    mFastStart = true;
    mReadyCount = mFastStart?min(mRefBlend,mVarBlend)/2:max(mRefBlend,mVarBlend);
    Info( "Ready Count: %d", mReadyCount );
    mFirstAlarmCount = 0;
    mLastAlarmCount = 0;
    mAlarmed = false;

    //if ( mAlarmFrameCount < 1 )
        mAlarmFrameCount = 1;

    //mAnalysisScale = config.analysis_scale;
    mAnalysisScale = 2;
    mNoiseLevel = 8;
    mNoiseLevelSq = (mNoiseLevel*mNoiseLevel) << 16;

    mStartTime = time( 0 );
}

/**
* @brief 
*
* @param name
*/
MotionDetector::MotionDetector( const std::string &name ) :
    VideoProvider( cClass(), name ),
    Thread( identity() ),
    mVarImage( NULL ),
    mCompImageSlave( NULL ),
    mRefImageSlave( NULL ),
    mDeltaImageSlave( NULL ),
    mVarImageSlave( NULL )
{
    construct();

    mCompImageSlave = new SlaveVideo( name+"-compImage" );
    mRefImageSlave = new SlaveVideo( name+"-refImage" );
    mDeltaImageSlave = new SlaveVideo( name+"-deltaImage" );
    mVarImageSlave = new SlaveVideo( name+"-varImage" );
}

/**
* @brief 
*/
MotionDetector::~MotionDetector()
{
    for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
        delete *zoneIter;
    mZones.clear();
    delete mVarImage;

    mAlarmed = false;
}

/**
* @brief 
*
* @param zone
*
* @return 
*/
bool MotionDetector::addZone( Zone *zone )
{
    std::pair<ZoneSet::iterator,bool> result = mZones.insert( zone );
    return( result.second );
}

/**
* @brief 
*
* @return 
*/
int MotionDetector::run()
{
    config.dir_events = "/transfer";
    config.record_diag_images = true;

    // Wait for encoder to be ready
    if ( waitForProviders() )
    {
        PixelFormat pixelFormat = videoProvider()->pixelFormat();
        int16_t width = videoProvider()->width();
        int16_t height = videoProvider()->height();
        Info( "pf:%d, %dx%d", pixelFormat, width, height );

        if ( !mZones.size() )
        {
            Coord coords[4] = { Coord( 0, 0 ), Coord( width-1, 0 ), Coord( width-1, height-1 ), Coord( 0, height-1 ) };
            addZone( new Zone( 0, "All", Zone::ACTIVE, Polygon( sizeof(coords)/sizeof(*coords), coords ), RGB_RED, true, 2.0, 1.41, 64, 1200, 0, 50, 0 ) );
        }

        mCompImageSlave->prepare( videoProvider()->frameRate() );
        mRefImageSlave->prepare( videoProvider()->frameRate() );
        mDeltaImageSlave->prepare( videoProvider()->frameRate() );
        mVarImageSlave->prepare( videoProvider()->frameRate() );
      
        while ( !mStop )
        {
            if ( mStop )
               break;
            mQueueMutex.lock();
            if ( !mFrameQueue.empty() )
            {
                Debug( 3, "Got %zd frames on queue", mFrameQueue.size() );
                for ( FrameQueue::iterator iter = mFrameQueue.begin(); iter != mFrameQueue.end(); iter++ )
                {
                    const VideoFrame *frame = dynamic_cast<const VideoFrame *>(iter->get());

                    Image image( pixelFormat, width, height, frame->buffer().data() );
                    //Image *image = new Image( Image::FMT_YUVP, width, height, packet->data() );
                    if ( mRefImage.empty() )
                    {
                        if ( writeImages )
                            image.writeJpeg( stringtf( "%s/image-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), frame->id() ), 100 );
                        mRefImage.assign( Image( Image::FMT_RGB48, image ) );
                        if ( writeImages )
                            mRefImage.writeJpeg( stringtf( "%s/ref-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), frame->id() ), 100 );
                    }

                    //struct timeval *timestamp = new struct timeval;
                    //gettimeofday( timestamp, NULL );

                    MotionData *motionData = new MotionData;
                    //Image motionImage( Image::FMT_RGB, image );
                    Image motionImage( image );
                    //motionImage.erase();
                    //motionData.reset();
                    analyse( &image, motionData, &motionImage );
                    if ( mAlarmed )
                    {
                        Info( "ALARM" );
                    }
                    MotionFrame *motionFrame = new MotionFrame( this, *iter, mFrameCount, frame->timestamp(), motionImage.buffer(), mAlarmed, motionData );

                    distributeFrame( FramePtr( motionFrame ) );

                    if ( config.blend_alarmed_images || !mAlarmed )
                    {
                        int refBlend = mRefBlend;
                        if ( refBlend > (mFrameCount+1) )
                        {
                            while ( refBlend > (mFrameCount+1) )
                                refBlend >>= 1;
                            //printf( "Frame: %d, adjusting refBlend = %d\n", frameCount, refBlend );
                        }
                        mRefImage.blend( image, refBlend );
                    }
                    //mRefImage.blend( image, mRefBlend );
                    mFrameCount++;

                    //delete *iter;
                }
                mFrameQueue.clear();
            }
            mQueueMutex.unlock();
            checkProviders();
            // Quite short so we can always keep up with the required packet rate for 25/30 fps
            usleep( INTERFRAME_TIMEOUT );
        }
    }
    FeedProvider::cleanup();
    FeedConsumer::cleanup();
    delete mCompImageSlave;
    delete mRefImageSlave;
    delete mDeltaImageSlave;
    delete mVarImageSlave;
    return( !ended() );
}

/**
* @brief 
*
* @param compImage
* @param motionData
* @param motionImage
*
* @return 
*/
bool MotionDetector::analyse( const Image *compImage, MotionData *motionData, Image *motionImage )
{
    double score = 0;
    if ( mFrameCount > mReadyCount )
    {
        std::string cause;
        StringSetMap noteSetMap;

        ZoneSet detectedZones;
        uint64_t motionScore = detectMotion( *compImage, detectedZones );
        if ( motionScore )
        {
            Info( "Score: %ld", motionScore );
            if ( !mAlarmed )
            {
                score += motionScore;
                if ( cause.length() )
                    cause += ", ";
                cause += MOTION_CAUSE;
            }
            else
            {
                score += motionScore;
            }
            StringSet zoneLabels;
            for ( ZoneSet::iterator zoneIter = detectedZones.begin(); zoneIter!= detectedZones.end(); zoneIter++ )
            {
                Zone *zone = *zoneIter;
                if ( motionData && zone->motionData() )
                    *motionData += *(zone->motionData());
                if ( motionImage )
                    motionImage->overlay( *(zone->motionImage()) );
                zoneLabels.insert( zone->label() );
            }
            noteSetMap[MOTION_CAUSE] = zoneLabels;
        }
        if ( score )
        {
            if ( !mAlarmed )
            {
                Info( "%03ju - Gone into alarm state", mFrameCount );
                mAlarmed = true;
            }
        }
        else
        {
            if ( mAlarmed )
            {
                Info( "%03ju - Left alarm state", mFrameCount );
                mAlarmed = false;
            }
        }
#if 0
        if ( mAlarmed )
        {
            if ( motionData && config.create_analysis_images )
            {
                bool gotAnalImage = false;
                Image alarmImage( *compImage );
                alarmImage.overlay( *(motionData->image()) );
                /*
                for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
                {
                    Zone *zone = *zoneIter;
                    if ( zone->isAlarmed() )
                    {
                        const MotionData *motionData = zone->motionData();
                        alarmImage.overlay( *(motionData->image()) );
                        gotAnalImage = true;
                        if ( config.record_event_stats && mAlarmed )
                        {
                            //zone->recordStats( mEvent );
                        }
                    }
                }
                */
            }
            else
            {
                for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
                {
                    Zone *zone = *zoneIter;
                    if ( zone->isAlarmed() )
                    {
                        if ( config.record_event_stats && mAlarmed )
                        {
                            //mZones[i]->recordStats( mEvent );
                        }
                    }
                }
            }
        }
#endif
    }
    return( mAlarmed );
}

/**
* @brief 
*
* @param compImage
* @param motionZones
*
* @return 
*/
uint32_t MotionDetector::detectMotion( const Image &compImage, ZoneSet &motionZones )
{
    bool alarm = false;
    uint64_t score = 0;

    if ( mZones.size() <= 0 ) return( alarm );

    if ( config.record_diag_images )
    {
        mCompImageSlave->relayImage( compImage );
        //compImage.writeJpeg( stringtf( "%s/diag-c.jpg", config.dir_events ) );
        if ( writeImages )
            compImage.writeJpeg( stringtf( "%s/comp-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), mFrameCount ), 100 );
        mRefImageSlave->relayImage( mRefImage );
        //mRefImage.writeJpeg( stringtf( "%s/diag-r.jpg", config.dir_events ) );
        if ( writeImages )
            mRefImage.writeJpeg( stringtf( "%s/ref2-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), mFrameCount ), 100 );
    }

    // Get the difference between the captured image and the reference image
    mDeltaCorrection = false;
    Image *deltaImage = 0;
    if ( mColourDeltas )
        deltaImage = mRefImage.delta2( compImage, mDeltaCorrection );
    else
        deltaImage = mRefImage.yDelta2( compImage, mDeltaCorrection );
    //Info( "DI: %dx%d - %d", deltaImage->width(), deltaImage->height(), deltaImage->format() );

    // Reduce the size of the diff image, also helps to despeckle
    if ( mAnalysisScale > 1 )
        deltaImage->shrink( mAnalysisScale );
    if ( config.record_diag_images )
    {
        mDeltaImageSlave->relayImage( *deltaImage );
        //deltaImage->writeJpeg( stringtf( "%s/diag-d.jpg", config.dir_events ), 100 );
        if ( writeImages )
            deltaImage->writeJpeg( stringtf( "%s/delta-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), mFrameCount ), 100 );
    }
    // Pre-populate the variance buffer with noise
    if ( mVarBuffer.empty() )
        mVarBuffer.fill( mNoiseLevelSq, deltaImage->pixels() );

    // Blank out all exclusion zones
    for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
    {
        Zone *zone = *zoneIter;
        zone->clearAlarmed();
        if ( !zone->isInactive() )
            continue;
        Debug( 3, "Blanking inactive zone %s", zone->label().c_str() );
        deltaImage->fill( RGB_BLACK, zone->getPolygon() );
    }

    // Check preclusive zones first
    for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
    {
        Zone *zone = *zoneIter;
        if ( !zone->isPreclusive() )
            continue;
        Debug( 3, "Checking preclusive zone %s", zone->label().c_str() );
        if ( zone->checkMotion( deltaImage, mVarBuffer ) )
        {
            Debug( 3, "Preclusive zone %s is alarmed, zone score = %d", zone->label().c_str(), zone->score() );
            return( false );
        }
    }

    double topScore = -1.0;
    Coord alarmCentre;

    // Find all alarm pixels in active zones
    for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
    {
        Zone *zone = *zoneIter;
        if ( !zone->isActive() )
            continue;
        Debug( 3, "Checking active zone %s", zone->label().c_str() );
        if ( zone->checkMotion( deltaImage, mVarBuffer ) )
        {
            alarm = true;
            score += zone->score();
            zone->setAlarmed();
            Debug( 3, "Active zone %s is alarmed, zone score = %d", zone->label().c_str(), zone->score() );
            motionZones.insert( zone );
        }
    }

    if ( alarm )
    {
        for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
        {
            Zone *zone = *zoneIter;
            if ( !zone->isInclusive() )
                continue;
            Debug( 3, "Checking inclusive zone %s", zone->label().c_str() );
            if ( zone->checkMotion( deltaImage, mVarBuffer ) )
            {
                alarm = true;
                score += zone->score();
                zone->setAlarmed();
                Debug( 3, "Inclusive zone %s is alarmed, zone score = %d", zone->label().c_str(), zone->score() );
                motionZones.insert( zone );
            }
        }
    }
    else
    {
        // Find all alarm pixels in exclusive zones
        for ( ZoneSet::iterator zoneIter = mZones.begin(); zoneIter!= mZones.end(); zoneIter++ )
        {
            Zone *zone = *zoneIter;
            if ( !zone->isExclusive() )
                continue;
            Debug( 3, "Checking exclusive zone %s", zone->label().c_str() );
            if ( zone->checkMotion( deltaImage, mVarBuffer ) )
            {
                alarm = true;
                score += zone->score();
                zone->setAlarmed();
                Debug( 3, "Exclusive zone %s is alarmed, zone score = %d", zone->label().c_str(), zone->score() );
                motionZones.insert( zone );
            }
        }
    }

    if ( topScore > 0 )
    {
        Info( "Got alarm centre at %d,%d, at count %ju", alarmCentre.x(), alarmCentre.y(), mFrameCount );
    }

    // Update the variance buffer based on the delta image
    uint8_t *pDelta = deltaImage->buffer().head();
    uint32_t *pBuf = mVarBuffer.head();
    int deltaStep = deltaImage->pixelStep();
    int varBufShift = getShift( mVarBlend, mFastStart?mFrameCount+1:0 );
    int varDeltaShift = 16 - varBufShift;

    if ( deltaImage->format() == Image::FMT_GREY16 )
    {
        while( pDelta != deltaImage->buffer().tail() )
        {
            uint16_t delta = (((uint16_t)*pDelta)<<8)+*(pDelta+1);
            //uint32_t long deltaSq = delta*delta*2;
            uint64_t deltaSq = delta*delta;
            *pBuf = *pBuf - (*pBuf >> varBufShift) + (deltaSq >> varDeltaShift);
            // Add a noise threshold
            if ( *pBuf < mNoiseLevelSq )
                *pBuf = mNoiseLevelSq;
            pDelta += deltaStep;
            pBuf++;
        }
    }
    else
    {
        while( pDelta != deltaImage->buffer().tail() )
        {
            uint8_t delta = *pDelta;
            //uint16_t deltaSq = delta*delta*2;
            uint16_t deltaSq = delta*delta;
            *pBuf = *pBuf - (*pBuf >> varBufShift) + (deltaSq << varDeltaShift);
            // Add a noise threshold
            if ( *pBuf < mNoiseLevelSq )
                *pBuf = mNoiseLevelSq;
            pDelta += deltaStep;
            pBuf++;
        }
    }

    if ( config.record_diag_images )
    {
        if ( !mVarImage )
            mVarImage = new Image( Image::FMT_GREY, deltaImage->width(), deltaImage->height() );
        else
            mVarImage->erase();

        uint8_t *pVar = mVarImage->buffer().head();
        pBuf = mVarBuffer.head();
        int varStep = mVarImage->pixelStep();
        while( pBuf != mVarBuffer.tail() )
        {
            *pVar = (uint8_t)sqrt(*pBuf>>16);
            pVar += varStep;
            pBuf++;
        }
        mVarImageSlave->relayImage( *mVarImage );
        //mVarImage->writeJpeg( stringtf( "%s/diag-v.jpg", config.dir_events ), 100 );
        if ( writeImages )
            mVarImage->writeJpeg( stringtf( "%s/var-%s-%ju.jpg", writeLocation.c_str(), mName.c_str(), mFrameCount ), 100 );
    }
    delete deltaImage;
    //mRefImage.blend( compImage, mRefBlend );

    // This is a small and innocent hack to prevent scores of 0 being returned in alarm state
    return( score?score:alarm );
} 

/**
* @brief 
*
* @param frame
* @param 
*
* @return 
*/
bool MotionDetector::inAlarm( FramePtr frame, const FeedConsumer * )
{
    const AlarmFrame *alarmFrame = dynamic_cast<const AlarmFrame *>(frame.get());
    //const MotionDetector *motionDetector = dynamic_cast<const MotionDetector *>( videoFrame->provider() );
    return( alarmFrame->alarmed() );
}
