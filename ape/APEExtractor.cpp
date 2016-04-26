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

//#define LOG_NDEBUG 0
#define LOG_TAG "APEExtractor"
#include <utils/Log.h>

#include "APEExtractor.h"

#include "include/avc_utils.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/foundation/AMessage.h>
#include <media/stagefright/DataSource.h>
#include <media/stagefright/MediaBuffer.h>
#include <media/stagefright/MediaBufferGroup.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/Utils.h>
#include <utils/String8.h>

namespace android {

#define APE_MIN_VERSION 3800
#define APE_MAX_VERSION 3990
#define APE_FLAG_8_BIT                 1
#define APE_FLAG_CRC                   2
#define APE_FLAG_HAS_PEAK_LEVEL        4
#define APE_FLAG_24_BIT                8
#define APE_FLAG_HAS_SEEK_ELEMENTS    16
#define APE_FLAG_CREATE_WAV_HEADER    32

#define APE_MAX_TAGS_SIZE 200

class APESource : public MediaSource {
public:
    APESource(
            const sp<MetaData> &meta, const sp<DataSource> &source, sp<APEFrameData> apeframedata);

    virtual status_t start(MetaData *params = NULL);
    virtual status_t stop();

    virtual sp<MetaData> getFormat();

    virtual status_t read(
            MediaBuffer **buffer, const ReadOptions *options = NULL);

protected:
    virtual ~APESource();

private:
    sp<MetaData> mMeta;
    sp<DataSource> mDataSource;
    sp<APEFrameData> mAPEFrameData;

    MediaBufferGroup *mGroup;

    uint32_t mCurrentFrameNum;

    APESource(const APESource &);
    APESource &operator=(const APESource &);
};

APEFrameData::APEFrameData(const sp<DataSource> &source)
        :mDataSource(source),
        mApeHeaderData(NULL),
        mSeekTable(NULL),
        mAPEFrames(NULL) {
    uint64_t data_offset = 0;
    uint8_t buff[2];

    mApeHeaderData = (ApeHeaderData *)malloc(sizeof(ApeHeaderData));
    if (!mApeHeaderData) {
        LOGE("%s: Out of memory:%d", __FUNCTION__, __LINE__);
        return;
    }

    size_t n = source->readAt(data_offset + 4, buff, sizeof(buff));
    mApeHeaderData->version = U16LE_AT(buff);
    if (mApeHeaderData->version < APE_MIN_VERSION || mApeHeaderData->version > APE_MAX_VERSION) {
        LOGE("Unsupported file version - %d.%02d\n",
               mApeHeaderData->version / 1000, (mApeHeaderData->version % 1000) / 10);
        return;
    }
    if (mApeHeaderData->version >= 3980) {
        uint8_t descriptor[46];

        //Get descriptor data.
        if (source->readAt(data_offset + 6, descriptor, sizeof(descriptor)) < sizeof(descriptor)) {
            return;
        }

        mApeHeaderData->descriptorlength = U32LE_AT(&descriptor[2]);
        mApeHeaderData->headerlength     = U32LE_AT(&descriptor[6]);
        mApeHeaderData->seektablelength  = U32LE_AT(&descriptor[10]);
        mApeHeaderData->wavheaderlength  = U32LE_AT(&descriptor[14]);

        uint8_t header[mApeHeaderData->headerlength];
        data_offset += mApeHeaderData->descriptorlength;

        //Get header data.
        if (source->readAt(data_offset, header, sizeof(header)) < sizeof(header)) {
            return;
        }

        mApeHeaderData->compressiontype  = U16LE_AT(&header[0]);
        mApeHeaderData->formatflags      = U16LE_AT(&header[2]);
        mApeHeaderData->blocksperframe   = U32LE_AT(&header[4]);
        mApeHeaderData->finalframeblocks = U32LE_AT(&header[8]);
        mApeHeaderData->totalframes      = U32LE_AT(&header[12]);
        mApeHeaderData->bitspersample    = U16LE_AT(&header[16]);
        mApeHeaderData->channels         = U16LE_AT(&header[18]);
        mApeHeaderData->samplerate       = U32LE_AT(&header[20]);

        data_offset += mApeHeaderData->headerlength;
    } else {
        uint8_t header[34];

        mApeHeaderData->descriptorlength = 0;
        mApeHeaderData->headerlength = 32;

        data_offset += 6;
        if (source->readAt(data_offset, header, sizeof(header)) < sizeof(header)) {
            return;
        }

        mApeHeaderData->compressiontype  = U16LE_AT(&header[0]);
        mApeHeaderData->formatflags      = U16LE_AT(&header[2]);
        mApeHeaderData->channels         = U16LE_AT(&header[4]);
        mApeHeaderData->samplerate       = U32LE_AT(&header[6]);
        mApeHeaderData->wavheaderlength  = U32LE_AT(&header[10]);
        mApeHeaderData->wavtaillength  = U32LE_AT(&header[14]);
        mApeHeaderData->totalframes      = U32LE_AT(&header[18]);
        mApeHeaderData->finalframeblocks = U32LE_AT(&header[22]);

        if (mApeHeaderData->formatflags & APE_FLAG_HAS_PEAK_LEVEL) {
            mApeHeaderData->headerlength += 4;
        }

        if (mApeHeaderData->formatflags & APE_FLAG_HAS_SEEK_ELEMENTS) {
            if (mApeHeaderData->formatflags & APE_FLAG_HAS_PEAK_LEVEL) {
                mApeHeaderData->seektablelength = U32LE_AT(&header[30]);
            } else {
                mApeHeaderData->seektablelength = U32LE_AT(&header[26]);
            }
            mApeHeaderData->headerlength += 4;
            mApeHeaderData->seektablelength *= sizeof(int32_t);
        } else {
            mApeHeaderData->seektablelength = mApeHeaderData->totalframes * sizeof(int32_t);
        }

        if (mApeHeaderData->formatflags & APE_FLAG_8_BIT) {
            mApeHeaderData->bitspersample = 8;
        } else if (mApeHeaderData->formatflags & APE_FLAG_24_BIT) {
            mApeHeaderData->bitspersample = 24;
        } else {
            mApeHeaderData->bitspersample = 16;
        }

        if (mApeHeaderData->version >= 3950) {
            mApeHeaderData->blocksperframe = 73728 * 4;
        } else if (mApeHeaderData->version >= 3900 || (mApeHeaderData->version >= 3800
                        && mApeHeaderData->compressiontype >= 4000)) {
            mApeHeaderData->blocksperframe = 73728;
        } else {
            mApeHeaderData->blocksperframe = 9216;
        }

        data_offset += mApeHeaderData->headerlength;

        if (mApeHeaderData->formatflags & APE_FLAG_CREATE_WAV_HEADER) {
            data_offset += mApeHeaderData->wavheaderlength;
        }
    }

    mAPEFrames = (ApeFrame *)malloc(mApeHeaderData->totalframes * sizeof(ApeFrame));
    if (!mAPEFrames) {
        LOGE("%s: Out of memory:%d", __FUNCTION__, __LINE__);
        return;
    }

    //Seek table, from which can get every frame start position,
    //and can calculate the frame size and skip values.
    if (mApeHeaderData->seektablelength > 0) {
        mSeekTable = (uint32_t *)malloc(mApeHeaderData->seektablelength);
       if (!mSeekTable) {
            LOGE("%s: Out of memory:%d", __FUNCTION__, __LINE__);
            return;
       }
        for (uint32_t i = 0; i < mApeHeaderData->seektablelength/sizeof(uint32_t); i++) {
            if (source->readAt(data_offset + i * 4, &mSeekTable[i],
                                            sizeof(uint32_t)) < sizeof(uint32_t)) {
                return;
            }
        }
    }
    mAPEFrames[0].pos = mApeHeaderData->descriptorlength
                        + mApeHeaderData->headerlength
                        + mApeHeaderData->seektablelength
                        + mApeHeaderData->wavheaderlength;
    mAPEFrames[0].nblocks = mApeHeaderData->blocksperframe;
    mAPEFrames[0].skip = 0;
    mAPEFrames[0].pts = 0;

    for (uint32_t i = 1; i < mApeHeaderData->totalframes; i++) {
        mAPEFrames[i].pos = mSeekTable[i];
        mAPEFrames[i].nblocks = mApeHeaderData->blocksperframe;
        mAPEFrames[i-1].size = mAPEFrames[i].pos - mAPEFrames[i-1].pos;
        mAPEFrames[i].skip = (mAPEFrames[i].pos - mAPEFrames[0].pos) & 3;
        mAPEFrames[i].pts = mAPEFrames[i-1].pts + mApeHeaderData->blocksperframe/4608;
    }

    //The frame size and block num of the last frame.
    off64_t file_size = 0;
    int  final_size = 0;
    source->getSize(&file_size);
    if (file_size > 0) {
        final_size = file_size - mAPEFrames[mApeHeaderData->totalframes - 1].pos -
                     mApeHeaderData->wavtaillength;
        final_size -= final_size & 3;
    }
    if (file_size <= 0 || final_size <= 0) {
        final_size = mApeHeaderData->finalframeblocks * 8;
    }
    mAPEFrames[mApeHeaderData->totalframes - 1].size = final_size;
    mAPEFrames[mApeHeaderData->totalframes - 1].size = mApeHeaderData->finalframeblocks * 8;
    mAPEFrames[mApeHeaderData->totalframes - 1].nblocks = mApeHeaderData->finalframeblocks;

    //If the skip value is not zero, need to adjust the frame position and frame size.
    for (uint32_t i = 0; i < mApeHeaderData->totalframes; i++) {
        if (mAPEFrames[i].skip) {
            mAPEFrames[i].pos -= mAPEFrames[i].skip;
            mAPEFrames[i].size += mAPEFrames[i].skip;
        }
        mAPEFrames[i].size = (mAPEFrames[i].size + 3) & ~3;
    }
}

APEFrameData::~APEFrameData() {
    free(mApeHeaderData);
    mApeHeaderData = NULL;

    free(mSeekTable);
    mSeekTable = NULL;

    free(mAPEFrames);
    mAPEFrames = NULL;
}

status_t APEFrameData::getRequiredFrameNum(int64_t seekTimeUs,int32_t *frameNum){
    for (uint32_t i = 0; i < mApeHeaderData->totalframes; i++) {
        if(mAPEFrames[i].pts * 1000 * 100 >= seekTimeUs) {
            *frameNum = i;
            return OK;
        }
    }
    return OK;
}

size_t APEFrameData::getMaxFrameSize(){
    size_t maxframesize;

    maxframesize = mAPEFrames[0].size;
    for (uint32_t i = 1; i < mApeHeaderData->totalframes; i++) {
        if (mAPEFrames[i].size > maxframesize) {
            maxframesize = mAPEFrames[i].size;
        }
    }

    return maxframesize;
}

ApeFrame *APEFrameData::getCurrentFrame(uint32_t framenum){
    if (framenum >= mApeHeaderData->totalframes) {
        return NULL;
    }
    return &mAPEFrames[framenum];
}

ApeHeaderData *APEFrameData::getApeHeaderData(){
    return mApeHeaderData;
}

APEExtractor::APEExtractor(
        const sp<DataSource> &source)
        :mDataSource(source),
         mMeta(new MetaData),
         mFileMeta(new MetaData),
         mAPEFrameData(NULL),
         mInitCheck(NO_INIT) {

    uint16_t channels, bitspersample;
    uint32_t samplerate;
    uint64_t durationUs;
    size_t maxframesize;
    //Extra data size is 6.
    uint16_t extradata[3];

    ApeHeaderData *apeheaderdata = NULL;

    mAPEFrameData = new APEFrameData(mDataSource);

    apeheaderdata = mAPEFrameData->getApeHeaderData();

    if (apeheaderdata != NULL) {
        channels = apeheaderdata->channels;
        bitspersample = apeheaderdata->bitspersample;
        samplerate = apeheaderdata->samplerate;

        int64_t tatalblocks = (apeheaderdata->totalframes - 1)
                        * apeheaderdata->blocksperframe + apeheaderdata->finalframeblocks;
        //4608 sub frame size.
        durationUs = tatalblocks/4608;

        // Extra data content is consist of version, compression type and format flags.
        extradata[0] = apeheaderdata->version;
        extradata[1] = apeheaderdata->compressiontype;
        extradata[2] = apeheaderdata->formatflags;

        maxframesize = mAPEFrameData->getMaxFrameSize();

        mMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_APE);

        mMeta->setInt32(kKeyChannelCount, channels);
        mMeta->setInt32(kKeySampleRate, samplerate);
        mMeta->setInt32(kKeyBitsPerSample,bitspersample);
        mMeta->setInt64(kKeyDuration, durationUs * 1000 * 100);
        mMeta->setInt32(kKeyMaxInputSize, maxframesize);

        mMeta->setData(kFfmpegCodecSpecificData, 0, (uint8_t *)extradata, 6);

        mInitCheck = OK;
    }
}

size_t APEExtractor::countTracks() {
    return mInitCheck != OK ? 0 : 1;
}

sp<MediaSource> APEExtractor::getTrack(size_t index) {
    if (mInitCheck != OK || index != 0) {
        return NULL;
    }

    return new APESource(mMeta, mDataSource, mAPEFrameData);
}

sp<MetaData> APEExtractor::getTrackMetaData(size_t index, uint32_t flags) {
    if (mInitCheck != OK || index != 0) {
        return NULL;
    }

    return mMeta;
}

sp<MetaData> APEExtractor::getMetaData() {
    if (parseAPETag() != OK) {
        return NULL;
    }

    mFileMeta->setCString(kKeyMIMEType, MEDIA_MIMETYPE_AUDIO_APE);
    return mFileMeta;
}

static IdTag TagArray[] {{kKeyTitle, "Title", 5},
                        {kKeyArtist, "Artist", 6},
                        {kKeyAlbum, "Album", 5},
                        {kKeyYear, "Year", 4}};

status_t APEExtractor::parseAPETag() {
    off64_t filesize = 0;
    mDataSource->getSize(&filesize);
    uint64_t position = filesize - 32; //skip APE Tag Footer

    char *tagid = "APETAGEX";

    uint8_t buff[8];
    for(uint64_t i = position - 1; i > 0; i--) {
        mDataSource->readAt(i, buff, 8);
        if (!memcmp(buff, (uint8_t *)tagid, 8)) {
            position = i;
            break;
        }
    }

    size_t tagsize = filesize - position;
    uint8_t *tagdata = (uint8_t *)malloc(tagsize);
    mDataSource->readAt(position, tagdata, tagsize);

    uint32_t version = U32LE_AT(tagdata + 8);
    uint32_t itemnum = U32LE_AT(tagdata + 16);

    position = 32; //start from tagdata[32] to parse.
    for (int i = 0; i < itemnum; i++) {
        String8 key;
        String8 value;

        uint32_t len = U32LE_AT(tagdata + position);
        position += 8; //skip len and flag

        uint64_t tmppos = position;
        while (1) {
            if (tagdata[tmppos] == 0x00) {
                break;
            }
            tmppos++;
        }

        char string[40];
        memcpy(string, tagdata + position, tmppos - position);
        key.setTo(string, tmppos - position);

        memcpy(string, tagdata + tmppos + 1, len);
        value.setTo(string, len);

        ALOGV("key %s, vlaue %s\n", key.string(), value.string());

        for (int i = 0; i < sizeof(TagArray)/sizeof(TagArray[0]); i++) {
            if (!strncmp(key.string(), TagArray[i].type, TagArray[i].size)) {
                mFileMeta->setCString(TagArray[i].key, value.string());
                break;
            }
        }

        position = tmppos + 1;
        position += len;
    }

    return OK;
}

APESource::APESource(
        const sp<MetaData> &meta, const sp<DataSource> &source, sp<APEFrameData> apeframedata)
        :mMeta(meta),
         mDataSource(source),
         mAPEFrameData(apeframedata),
         mGroup(NULL),
         mCurrentFrameNum(0) {
}

APESource::~APESource() {
    stop();
}

status_t APESource::start(MetaData *) {
    mGroup = new MediaBufferGroup;
    const size_t kMaxFrameSize = mAPEFrameData->getMaxFrameSize();
    mGroup->add_buffer(new MediaBuffer(kMaxFrameSize));

    return OK;
}

status_t APESource::stop() {
    delete mGroup;
    mGroup = NULL;

    return OK;
}

sp<MetaData> APESource::getFormat() {
    return mMeta;
}

status_t APESource::read(
        MediaBuffer **out, const ReadOptions *options) {
    *out = NULL;
    int64_t seekTimeUs;
    ReadOptions::SeekMode mode;
    bool seekCBR = false;

    if (options != NULL && options->getSeekTo(&seekTimeUs, &mode)) {
        int32_t framenum;
        mAPEFrameData->getRequiredFrameNum(seekTimeUs, &framenum);
        mCurrentFrameNum = framenum;
    }

    ApeHeaderData *apeheaderdata = mAPEFrameData->getApeHeaderData();

    if (mCurrentFrameNum >= apeheaderdata->totalframes){
        return ERROR_END_OF_STREAM;
    }

    MediaBuffer *buffer;
    status_t err = mGroup->acquire_buffer(&buffer);
    if (err != OK) {
        return err;
    }

    ApeFrame *apeframe = mAPEFrameData->getCurrentFrame(mCurrentFrameNum);

    //Add 8 bytes header for every frame, and the content is consist of block num and skip value.
    uint32_t *tmp = (uint32_t *)buffer->data();
    tmp[0] = apeframe->nblocks;
    tmp[1] = apeframe->skip;

    size_t n = mDataSource->readAt(apeframe->pos,
                (uint8_t *)buffer->data() + 8, apeframe->size);

    if (n < apeframe->size) {
        buffer->release();
        buffer = NULL;
        return ERROR_END_OF_STREAM;
    }

    buffer->set_range(0, apeframe->size + 8);
    buffer->meta_data()->setInt64(kKeyTime, apeframe->pts * 1000 * 100);
    mCurrentFrameNum++;

    *out = buffer;
    return OK;
}

bool SniffAPE(
        const sp<DataSource> &source, String8 *mimeType,
        float *confidence, sp<AMessage> *meta) {
    uint8_t header[4];

    ssize_t n = source->readAt(0, header, sizeof(header));
    if (n < (ssize_t)sizeof(header)) {
        return false;
    }

    if (header[0] == 'M' && header[1] == 'A' && header[2] == 'C' && header[3] == ' ') {
        *mimeType = MEDIA_MIMETYPE_AUDIO_APE;
        *confidence = 0.2f;
        return true;
    }

    return false;
}

}  // namespace android

