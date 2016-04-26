/*
* (C) Copyright 2013 Marvell International Ltd.
* All Rights Reserved
*
* MARVELL CONFIDENTIAL
* Copyright 2008 ~ 2013 Marvell International Ltd All Rights Reserved.
* The source code contained or described herein and all documents related to
* the source code ("Material") are owned by Marvell International Ltd or its
* suppliers or licensors. Title to the Material remains with Marvell International Ltd
* or its suppliers and licensors. The Material contains trade secrets and
* proprietary and confidential information of Marvell or its suppliers and
* licensors. The Material is protected by worldwide copyright and trade secret
* laws and treaty provisions. No part of the Material may be used, copied,
* reproduced, modified, published, uploaded, posted, transmitted, distributed,
* or disclosed in any way without Marvell's prior express written permission.
*
* No license under any patent, copyright, trade secret or other intellectual
* property right is granted to or conferred upon you by disclosure or delivery
* of the Materials, either expressly, by implication, inducement, estoppel or
* otherwise. Any license under such intellectual property rights must be
* express and approved by Marvell in writing.
*
*/

#ifndef APE_EXTRACTOR_H_

#define APE_EXTRACTOR_H_

#include <utils/Errors.h>
#include <media/stagefright/MediaExtractor.h>
#include <media/stagefright/MetaData.h>

namespace android {

struct AMessage;
class DataSource;
class String8;
class APEFrameData;

typedef struct {
    uint32_t key;
    char *type;
    size_t size;
}IdTag;

typedef struct {
    int64_t pos;
    int32_t nblocks;
    size_t size;
    int32_t skip;
    int64_t pts;
} ApeFrame;

typedef struct {
    //Descriptor data.
    uint16_t version;
    uint32_t descriptorlength;
    uint32_t headerlength;
    uint32_t seektablelength;
    uint32_t wavheaderlength;
    uint32_t wavtaillength;
    //Header data.
    uint32_t compressiontype;
    uint16_t formatflags;
    uint32_t blocksperframe;
    uint32_t finalframeblocks;
    uint32_t totalframes;
    uint16_t bitspersample;
    uint16_t channels;
    uint32_t samplerate;
    uint64_t durationUS;
} ApeHeaderData;

class APEFrameData : public RefBase{
public:
    APEFrameData(const sp<DataSource> &source);

    status_t getRequiredFrameNum(int64_t seekTimeUs,int32_t *frameNum);

    size_t getMaxFrameSize();

    ApeFrame *getCurrentFrame(uint32_t framenum);

    ApeHeaderData *getApeHeaderData();
protected:
    virtual ~APEFrameData();

private:

    sp<DataSource> mDataSource;

    ApeHeaderData *mApeHeaderData;

    uint32_t *mSeekTable;
    ApeFrame *mAPEFrames;
};

class APEExtractor : public MediaExtractor {
public:
    // Extractor assumes ownership of "source".
    APEExtractor(const sp<DataSource> &source);

    virtual size_t countTracks();
    virtual sp<MediaSource> getTrack(size_t index);
    virtual sp<MetaData> getTrackMetaData(size_t index, uint32_t flags);
    virtual sp<MetaData> getMetaData();

    status_t parseAPETag();
private:
    sp<DataSource> mDataSource;
    sp<MetaData> mMeta;
    sp<MetaData> mFileMeta;
    sp<APEFrameData> mAPEFrameData;
    status_t mInitCheck;

    APEExtractor(const APEExtractor &);
    APEExtractor &operator=(const APEExtractor &);
};

bool SniffAPE(
        const sp<DataSource> &source, String8 *mimeType, float *confidence,
        sp<AMessage> *meta);

}  // namespace android

#endif  // APE_EXTRACTOR_H_
