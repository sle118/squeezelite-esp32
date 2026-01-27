/*
 Copyright © 2011 Apple  Inc. All rights reserved.
 
 IMPORTANT:  This Apple software is supplied to you by Apple Inc. (“Apple”) in consideration of your agreement to the following terms, and your use, installation, modification or redistribution of this Apple software constitutes acceptance of these terms.  If you do not agree with these terms, please do not use, install, modify or redistribute this Apple software.
 
 In consideration of your agreement to abide by the following terms, and subject to these terms, Apple grants you a personal, non-exclusive license, under Apple’s copyrights in this original Apple software (the “Apple Software”), to use, reproduce, modify and redistribute the Apple Software, with or without modifications, in source and/or binary forms; provided that if you redistribute the Apple Software in its entirety and without modifications, you must retain this notice and the following text and disclaimers in all such redistributions of the Apple Software.  Neither the name, trademarks, service marks or logos of Apple Inc. may be used to endorse or promote products derived from the Apple Software without specific prior written permission from Apple.  Except as expressly stated in this notice, no other rights or licenses, express or implied, are granted by Apple herein, including but not limited to any patent rights that may be infringed by your derivative works or by other works in which the Apple Software may be incorporated.
 
 The Apple Software is provided by Apple on an "AS IS" basis.  APPLE MAKES NO WARRANTIES, EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION THE IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE, REGARDING THE APPLE SOFTWARE OR ITS USE AND OPERATION ALONE OR IN COMBINATION WITH YOUR PRODUCTS. 
 
 IN NO EVENT SHALL APPLE BE LIABLE FOR ANY SPECIAL, INDIRECT, INCIDENTAL OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) ARISING IN ANY WAY OUT OF THE USE, REPRODUCTION, MODIFICATION AND/OR DISTRIBUTION OF THE APPLE SOFTWARE, HOWEVER CAUSED AND WHETHER UNDER THEORY OF CONTRACT, TORT (INCLUDING NEGLIGENCE), STRICT LIABILITY OR OTHERWISE, EVEN IF APPLE HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

//
//  CAFFileALAC.h
//  based CAFFile.h in the CoreAudio headers, ALAC specific
//
//  Copyright 2011 Apple Inc. All rights reserved.
//

#ifndef _CAFFilePortable_h
#define _CAFFilePortable_h

#if TARGET_OS_WIN32
#define ATTRIBUTE_PACKED
#pragma pack(push, 1)
#else
#define ATTRIBUTE_PACKED __attribute__((__packed__))
#endif

#include "ALACAudioTypes.h"

#define kMinCAFFPacketTableHeaderSize 24

typedef uint32_t CAFFChannelLayoutTag;
// These are subset of the channel layout tags listed in CoreAudioTypes.h
// ALAC and caff both use the same tag values
enum
{
    kCAFFChannelLayoutTag_Mono          = (100<<16) | 1,    // C
    kCAFFChannelLayoutTag_Stereo        = (101<<16) | 2,	// L R
    kCAFFChannelLayoutTag_MPEG_3_0_B    = (113<<16) | 3,	// C L R
    kCAFFChannelLayoutTag_MPEG_4_0_B    = (116<<16) | 4,	// C L R Cs
    kCAFFChannelLayoutTag_MPEG_5_0_D    = (120<<16) | 5,    // C L R Ls Rs
    kCAFFChannelLayoutTag_MPEG_5_1_D    = (124<<16) | 6,	// C L R Ls Rs LFE
    kCAFFChannelLayoutTag_AAC_6_1       = (142<<16) | 7,	// C L R Ls Rs Cs LFE
    kCAFFChannelLayoutTag_MPEG_7_1_B	= (127<<16) | 8     // C Lc Rc L R Ls Rs LFE
};

// ALAC currently only utilizes these channels layouts. CAFF supports all those listed in
// CoreAudioTypes.h. 
static const CAFFChannelLayoutTag	CAFFChannelLayoutTags[kALACMaxChannels] =
{
    kCAFFChannelLayoutTag_Mono,         // C
    kCAFFChannelLayoutTag_Stereo,		// L R
    kCAFFChannelLayoutTag_MPEG_3_0_B,	// C L R
    kCAFFChannelLayoutTag_MPEG_4_0_B,	// C L R Cs
    kCAFFChannelLayoutTag_MPEG_5_0_D,	// C L R Ls Rs
    kCAFFChannelLayoutTag_MPEG_5_1_D,	// C L R Ls Rs LFE
    kCAFFChannelLayoutTag_AAC_6_1,		// C L R Ls Rs Cs LFE
    kCAFFChannelLayoutTag_MPEG_7_1_B	// C Lc Rc L R Ls Rs LFE    (doc: IS-13818-7 MPEG2-AAC)
};


// In a CAF File all of these types' byte order is big endian.
// When reading or writing these values the program will need to flip byte order to native endian

// CAF File Header
enum {
	k_port__port_CAF_FileType				= 'caff',
	k_port_CAF_FileVersion_Initial	= 1
};

// CAF Chunk Types	
enum {
	k_port_CAF_StreamDescriptionChunkID = 'desc',
	k_port_CAF_AudioDataChunkID			= 'data',
	k_port_CAF_ChannelLayoutChunkID		= 'chan',
	k_port_CAF_MagicCookieID			= 'kuki',
	k_port_CAF_PacketTableChunkID		= 'pakt',
	k_port_CAF_FreeTableChunkID			= 'free'
};


struct port_CAFFileHeader
{
    uint32_t          mFileType;			// 'caff'
    uint16_t			mFileVersion;		//initial revision set to 1
    uint16_t			mFileFlags;			//initial revision set to 0
} ATTRIBUTE_PACKED;
typedef struct CAFFileHeader CAFFileHeader;


struct port_CAFChunkHeader
{
    uint32_t          mChunkType; // four char code
    int64_t          mChunkSize;  // size in bytes of the chunk data (not including this header).
    // mChunkSize is int64_t not uint64_t because negative values for 
    // the data size can have a special meaning
} ATTRIBUTE_PACKED;

typedef struct port_CAFChunkHeader port_CAFChunkHeader;

// Every file MUST have this chunk. It MUST be the first chunk in the file
struct port_CAFAudioDescription
{
    double mSampleRate;
    uint32_t  mFormatID;
    uint32_t  mFormatFlags;
    uint32_t  mBytesPerPacket;
    uint32_t  mFramesPerPacket;
    uint32_t  mChannelsPerFrame;
    uint32_t  mBitsPerChannel;
} ATTRIBUTE_PACKED;
typedef struct port_CAFAudioDescription  port_CAFAudioDescription;

// these are the flags if the format ID is 'lpcm'
// <CoreAudio/CoreAudioTypes.h> declares some of the format constants 
// that can be used as Data Formats in a CAF file
enum
{
    k_port_CAFLinearPCMFormatFlagIsFloat				= (1L << 0),
    k_port_CAFLinearPCMFormatFlagIsLittleEndian		= (1L << 1)
};


// 'chan'  Optional chunk.
// struct AudioChannelLayout  as defined in CoreAudioTypes.h.

// 'free'
// this is a padding chunk for reserving space in the file. content is meaningless.

// 'kuki'
// this is the magic cookie chunk. bag of bytes.

// 'data'    Every file MUST have this chunk.
// actual audio data can be any format as described by the 'asbd' chunk.

// if mChunkSize is < 0 then this is the last chunk in the file and the actual length 
// should be determined from the file size. 
// The motivation for this is to allow writing the files without seeking to update size fields after every 
// write in order to keep the file legal.
// The program can put a -1 in the mChunkSize field and 
// update it only once at the end of recording. 
// If the program were to crash during recording then the file is still well defined.

// 'pakt' Required if either/or mBytesPerPacket or mFramesPerPacket in the Format Description are zero
// For formats that are packetized and have variable sized packets. 
// The table is stored as an array of one or two variable length integers. 
// (a) size in bytes of the data of a given packet.
// (b) number of frames in a given packet.
// These sizes are encoded as variable length integers

// The packet description entries are either one or two values depending on the format.
// There are three possibilities
// (1)
// If the format has variable bytes per packets (desc.mBytesPerPacket == 0) and constant frames per packet
// (desc.mFramesPerPacket != 0) then the packet table contains single entries representing the bytes in a given packet
// (2)
// If the format is a constant bit rate (desc.mBytesPerPacket != 0) but variable frames per packet
// (desc.mFramesPerPacket == 0) then the packet table entries contains single entries 
// representing the number of frames in a given packet
// (3)
// If the format has variable frames per packet (asbd.mFramesPerPacket == 0) and variable bytes per packet
// (desc.mBytesPerPacket == 0) then the packet table entries are a duple of two values. The first value
// is the number of bytes in a given packet, the second value is the number of frames in a given packet

struct port_CAFPacketTableHeader
{
    int64_t  mNumberPackets;
    int64_t  mNumberValidFrames;
    int32_t  mPrimingFrames;
    int32_t  mRemainderFrames;
    
    uint8_t   mPacketDescriptions[1]; // this is a variable length array of mNumberPackets elements
} ATTRIBUTE_PACKED;
typedef struct port_CAFPacketTableHeader port_CAFPacketTableHeader;

struct port_CAFDataChunk
{
    uint32_t mEditCount;
    uint8_t mData[1]; // this is a variable length data field based off the size of the data chunk
} ATTRIBUTE_PACKED;
typedef struct port_CAFDataChunk port_CAFDataChunk;

// prototypes
int32_t FindCAFFPacketTableStart(FILE * inputFile, int32_t * paktPos, int32_t * paktSize);
void WriteCAFFcaffChunk(FILE * outputFile);
void WriteCAFFdescChunk(FILE * outputFile, AudioFormatDescription theOutputFormat);
void WriteCAFFdataChunk(FILE * outputFile);
void WriteCAFFkukiChunk(FILE * outputFile, void * inCookie, uint32_t inCookieSize);
void WriteCAFFChunkSize(FILE * outputFile, int64_t numDataBytes);
void WriteCAFFchanChunk(FILE * outputFile, uint32_t inChannelTag);
void WriteCAFFfreeChunk(FILE * outputFile, uint32_t theSize);
void WriteCAFFpaktChunkHeader(FILE * outputFile, port_CAFPacketTableHeader * thePacketTableHeader, uint32_t thePacketTableSize);
void GetBERInteger(int32_t theOriginalValue, uint8_t * theBuffer, int32_t * theBERSize);
uint32_t ReadBERInteger(uint8_t * theInputBuffer, int32_t * ioNumBytes);
int32_t BuildBasePacketTable(AudioFormatDescription theInputFormat, int32_t inputDataSize, int32_t * thePacketTableSize, port_CAFPacketTableHeader * thePacketTableHeader);
uint32_t GetMagicCookieSizeFromCAFFkuki(FILE * inputFile);
int32_t GetMagicCookieFromCAFFkuki(FILE * inputFile, uint8_t * outMagicCookie, uint32_t * ioMagicCookieSize);
bool FindCAFFDataStart(FILE * inputFile, int32_t * dataPos, int32_t * dataSize);
bool GetCAFFdescFormat(FILE * inputFile, AudioFormatDescription * theInputFormat);

#if TARGET_OS_WIN32
#pragma pack(pop)
#endif
////////////////////////////////////////////////////////////////////////////////////////////////


#endif
