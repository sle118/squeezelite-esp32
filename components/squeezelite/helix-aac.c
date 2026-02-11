/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
 *		Philippe, philippe_44@outlook.com
 *  
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "squeezelite.h"

#include <aacdec.h>

// AAC_MAX_SAMPLES is the number of samples for one channel
#define FRAME_BUF (AAC_MAX_NSAMPS*2)

#if BYTES_PER_FRAME == 4		
#define ALIGN(n) 	(n)
#else
#define ALIGN(n) 	(n << 16)		
#endif

#define WRAPBUF_LEN 2048

static unsigned rates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };

struct chunk_table {
	u32_t sample, offset;
};

struct helixaac {
	HAACDecoder hAac;
	u8_t type;
	u8_t *write_buf;
	u8_t *wrap_buf;
	// following used for mp4 only
	u32_t consume;
	u32_t pos;
	u32_t sample;
	u32_t nextchunk;
	void *stsc;
	u32_t skip;
	u64_t samples;
	u64_t sttssamples;
	bool  empty;
	struct chunk_table *chunkinfo;
#if !LINKALL
#endif
};

static struct helixaac *a;

extern log_level loglevel;

extern struct buffer *streambuf;
extern struct buffer *outputbuf;
extern struct streamstate stream;
extern struct outputstate output;
extern struct decodestate decode;
extern struct processstate process;

#define LOCK_S   mutex_lock(streambuf->mutex)
#define UNLOCK_S mutex_unlock(streambuf->mutex)
#define LOCK_O   mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#if PROCESS
#define LOCK_O_direct   if (decode.direct) mutex_lock(outputbuf->mutex)
#define UNLOCK_O_direct if (decode.direct) mutex_unlock(outputbuf->mutex)
#define IF_DIRECT(x)    if (decode.direct) { x }
#define IF_PROCESS(x)   if (!decode.direct) { x }
#else
#define LOCK_O_direct   mutex_lock(outputbuf->mutex)
#define UNLOCK_O_direct mutex_unlock(outputbuf->mutex)
#define IF_DIRECT(x)    { x }
#define IF_PROCESS(x)
#endif

#if LINKALL
#define HAAC(h, fn, ...) (AAC ## fn)(__VA_ARGS__)
#else
#define HAAC(h, fn, ...) (h)->AAC##fn(__VA_ARGS__)
#endif

// minimal code for mp4 file parsing to extract audio config and find media data

// adapted from faad2/common/mp4ff
u32_t mp4_desc_length(u8_t **buf) {
	u8_t b;
	u8_t num_bytes = 0;
	u32_t length = 0;

	do {
		b = **buf;
		*buf += 1;
		num_bytes++;
		length = (length << 7) | (b & 0x7f);
	} while ((b & 0x80) && num_bytes < 4);

	return length;
}

// read mp4 header to extract config data
static int read_mp4_header(unsigned long *samplerate_p, unsigned char *channels_p) {
	size_t bytes = min(_buf_used(streambuf), _buf_cont_read(streambuf));
	char type[5];
	u32_t len;

	while (bytes >= 8) {
		// count trak to find the first playable one
		static unsigned trak, play;
		u32_t consume;

		len = unpackN((u32_t *)streambuf->readp);
		memcpy(type, streambuf->readp + 4, 4);
		type[4] = '\0';

		if (!strcmp(type, "moov")) {
			trak = 0;
			play = 0;
		}
		if (!strcmp(type, "trak")) {
			trak++;
		}

		// extract audio config from within esds and pass to DecInit2
		if (!strcmp(type, "esds") && bytes > len) {
			u8_t *ptr = streambuf->readp + 12;
			AACFrameInfo info;	
			if (*ptr++ == 0x03) {
				mp4_desc_length(&ptr);
				ptr += 4;
			} else {
				ptr += 3;
			}
			mp4_desc_length(&ptr);
			ptr += 13;
			if (*ptr++ != 0x05) {
				LOG_WARN("error parsing esds");
				return -1;
			}
			int desc_len = mp4_desc_length(&ptr);
			int AOT = *ptr >> 3;
			info.profile = AAC_PROFILE_LC;
			info.sampRateCore = (*ptr++ & 0x07) << 1;
			info.sampRateCore |= (*ptr >> 7) & 0x01;
			info.sampRateCore = rates[info.sampRateCore];								
			info.nChans = (*ptr & 0x7f) >> 3;
			*channels_p = info.nChans;				
			// Note that 24 bits frequencies are not handled	
#if AAC_ENABLE_SBR			
			if (AOT == 5 || AOT == 29) {
				*samplerate_p = rates[((ptr[0] & 0x03) << 1) | (ptr[1] >> 7)];
				LOG_WARN("AAC stream with SBR => high CPU required (use LMS proxied mode)");									
			} else if (desc_len > 2 && ((ptr[1] << 3) | (ptr[2] >> 5)) == 0x2b7 && (ptr[2] & 0x1f) == 0x05 && (ptr[3] & 0x80)) {
				*samplerate_p = rates[(ptr[3] & 0x78) >> 3];
				LOG_WARN("AAC stream with extended SBR => high CPU required (use LMS proxied mode)");									
			} else if (AOT == 2) {
				*samplerate_p = info.sampRateCore;
			} else {	
				*samplerate_p = 44100;
				LOG_ERROR("AAC audio object type %d not handled", AOT);									
			}	
#else			
			*samplerate_p = info.sampRateCore;
#endif			
			HAAC(a, SetRawBlockParams, a->hAac, 0, &info); 
			LOG_DEBUG("playable aac track: %u (p:%x, r:%d, c:%d, desc_len:%d)", trak, AOT, info.sampRateCore, info.nChans, desc_len);
			play = trak;
		}

		// extract the total number of samples from stts
		if (!strcmp(type, "stts") && bytes > len) {
			u32_t i;
			u8_t *ptr = streambuf->readp + 12;
			u32_t entries = unpackN((u32_t *)ptr);
			ptr += 4;
			for (i = 0; i < entries; ++i) {
				u32_t count = unpackN((u32_t *)ptr);
				u32_t size = unpackN((u32_t *)(ptr + 4));
				a->sttssamples += count * size;
				ptr += 8;
			}
			LOG_DEBUG("total number of samples contained in stts: " FMT_u64, a->sttssamples);
		}

		// stash sample to chunk info, assume it comes before stco
		if (!strcmp(type, "stsc") && bytes > len && !a->chunkinfo) {
			a->stsc = malloc(len - 12);
			if (a->stsc == NULL) {
				LOG_WARN("malloc fail");
				return -1;
			}
			memcpy(a->stsc, streambuf->readp + 12, len - 12);
		}

		// build offsets table from stco and stored stsc
		if (!strcmp(type, "stco") && bytes > len && play == trak) {
			u32_t i;
			// extract chunk offsets
			u8_t *ptr = streambuf->readp + 12;
			u32_t entries = unpackN((u32_t *)ptr);
			ptr += 4;
			a->chunkinfo = malloc(sizeof(struct chunk_table) * (entries + 1));
			if (a->chunkinfo == NULL) {
				LOG_WARN("malloc fail");
				return -1;
			}
			for (i = 0; i < entries; ++i) {
				a->chunkinfo[i].offset = unpackN((u32_t *)ptr);
				a->chunkinfo[i].sample = 0;
				ptr += 4;
			}
			a->chunkinfo[i].sample = 0;
			a->chunkinfo[i].offset = 0;
			// fill in first sample id for each chunk from stored stsc
			if (a->stsc) {
				u32_t stsc_entries = unpackN((u32_t *)a->stsc);
				u32_t sample = 0;
				u32_t last = 0, last_samples = 0;
				u8_t *ptr = (u8_t *)a->stsc + 4;
				while (stsc_entries--) {
					u32_t first = unpackN((u32_t *)ptr);
					u32_t samples = unpackN((u32_t *)(ptr + 4));
					if (last) {
						for (i = last - 1; i < first - 1; ++i) {
							a->chunkinfo[i].sample = sample;
							sample += last_samples;
						}
					}
					if (stsc_entries == 0) {
						for (i = first - 1; i < entries; ++i) {
							a->chunkinfo[i].sample = sample;
							sample += samples;
						}
					}
					last = first;
					last_samples = samples;
					ptr += 12;
				}
				free(a->stsc);
				a->stsc = NULL;
			}
		}

		// found media data, advance to start of first chunk and return
		if (!strcmp(type, "mdat")) {
			_buf_inc_readp(streambuf, 8);
			a->pos += 8;
			bytes  -= 8;
			if (play) {
				LOG_DEBUG("type: mdat len: %u pos: %u", len, a->pos);
				if (a->chunkinfo && a->chunkinfo[0].offset > a->pos) {
					u32_t skip = a->chunkinfo[0].offset - a->pos; 	
					LOG_DEBUG("skipping: %u", skip);
					if (skip <= bytes) {
						_buf_inc_readp(streambuf, skip);
						a->pos += skip;
					} else {
						a->consume = skip;
					}
				}
				a->sample = a->nextchunk = 1;
				return 1;
			} else {
				LOG_DEBUG("type: mdat len: %u, no playable track found", len);
				return -1;
			}
		}

		// parse key-value atoms within ilst ---- entries to get encoder padding within iTunSMPB entry for gapless
		if (!strcmp(type, "----") && bytes > len) {
			u8_t *ptr = streambuf->readp + 8;
			u32_t remain = len - 8, size;
			if (!memcmp(ptr + 4, "mean", 4) && (size = unpackN((u32_t *)ptr)) < remain) {
				ptr += size; remain -= size;
			}
			if (!memcmp(ptr + 4, "name", 4) && (size = unpackN((u32_t *)ptr)) < remain && !memcmp(ptr + 12, "iTunSMPB", 8)) {
				ptr += size; remain -= size;
			}
			if (!memcmp(ptr + 4, "data", 4) && remain > 16 + 48) {
				// data is stored as hex strings: 0 start end samples
				unsigned b_tmp, c_tmp;
				u64_t d;
				if (sscanf((const char *)(ptr + 16), "%x %x %x " FMT_x64, &b_tmp, &b_tmp, &c_tmp, &d) == 4) {
					u32_t b = b_tmp, c = c_tmp;
					LOG_DEBUG("iTunSMPB start: %u end: %u samples: " FMT_u64, b, c, d);
					if (a->sttssamples && a->sttssamples < b + c + d) {
						LOG_DEBUG("reducing samples as stts count is less");
						d = a->sttssamples - (b + c);
					}
					a->skip = b;
					a->samples = d;
				}
			}
		}

		// default to consuming entire box
		consume = len;

		// read into these boxes so reduce consume
		if (!strcmp(type, "moov") || !strcmp(type, "trak") || !strcmp(type, "mdia") || !strcmp(type, "minf") || !strcmp(type, "stbl") ||
			!strcmp(type, "udta") || !strcmp(type, "ilst")) {
			consume = 8;
		}
		// special cases which mix mix data in the enclosing box which we want to read into
		if (!strcmp(type, "stsd")) consume = 16;
		if (!strcmp(type, "mp4a")) consume = 36;
		if (!strcmp(type, "meta")) consume = 12;

		// consume rest of box if it has been parsed (all in the buffer) or is not one we want to parse
		if (bytes >= consume) {
			LOG_DEBUG("type: %s len: %u consume: %u", type, len, consume);
			_buf_inc_readp(streambuf, consume);
			a->pos += consume;
			bytes -= consume;
		} else if ( !(!strcmp(type, "esds") || !strcmp(type, "stts") || !strcmp(type, "stsc") || 
					 !strcmp(type, "stco") || !strcmp(type, "----")) ) {
			LOG_DEBUG("type: %s len: %u consume: %u - partial consume: %u", type, len, consume, bytes);
			_buf_inc_readp(streambuf, bytes);
			a->pos += bytes;
			a->consume = consume - bytes;
			break;
		} else if (len > streambuf->size) {
 			// can't process an atom larger than streambuf!
			LOG_ERROR("atom %s too large for buffer %u %u", type, len, streambuf->size);
			return -1;
		 } else {
			 // make sure there is 'len' contiguous space
			_buf_unwrap(streambuf, len); 
			break;
		 }
	}

	return 0;
}

static decode_state helixaac_decode(void) {
	size_t bytes_total, bytes_wrap;
	int res, bytes;
	static AACFrameInfo info;
	s16_t *iptr;
	u8_t *sptr;
	bool endstream;
	frames_t frames;
	
	LOCK_S;
	bytes_total = _buf_used(streambuf);
	bytes_wrap  = min(bytes_total, _buf_cont_read(streambuf));
	
	if (stream.state <= DISCONNECT && !bytes_total) {
		UNLOCK_S;
		return DECODE_COMPLETE;
	}

	if (a->consume) {
		u32_t consume = min(a->consume, bytes_wrap);
		LOG_DEBUG("consume: %u of %u", consume, a->consume);
		_buf_inc_readp(streambuf, consume);
		a->pos += consume;
		a->consume -= consume;
		UNLOCK_S;
		return DECODE_RUNNING;
	}

	if (decode.new_stream) {
		int found = 0;
		static unsigned char channels;
		static unsigned long samplerate;
		
		if (a->type == '2') {

			// adts stream - seek for header
			long n = HAAC(a, FindSyncWord, streambuf->readp, bytes_wrap);
			
			LOG_DEBUG("Sync search in %d bytes %d", bytes_wrap, n);
			
			if (n >= 0) {
				u8_t *p = streambuf->readp + n;
				int bytes = bytes_wrap - n;
				
				if (!HAAC(a, Decode, a->hAac, &p, &bytes, (s16_t*) a->write_buf)) {
					HAAC(a, GetLastFrameInfo, a->hAac, &info);
					channels = info.nChans;
					samplerate = info.sampRateOut;
					found = 1;
				} else if (n == 0) n++;
					
				HAAC(a, FlushCodec, a->hAac);
			
				bytes_total -= n;
				bytes_wrap -= n;
				_buf_inc_readp(streambuf, n);
			} else {
				found = -1;
			}	

		} else {

			// mp4 - read header
			found = read_mp4_header(&samplerate, &channels);
		}

		if (found == 1) {
			LOCK_O;
			output.next_sample_rate = decode_newstream(samplerate, output.supported_rates);
			IF_DSD( output.next_fmt = PCM; )
			output.track_start = outputbuf->writep;
			if (output.fade_mode) _checkfade(true);
			decode.new_stream = false;
			UNLOCK_O;
			
			LOG_INFO("setting track start, samplerate: %u channels: %u", samplerate, channels);
			
			bytes_total = _buf_used(streambuf);
			bytes_wrap  = min(bytes_total, _buf_cont_read(streambuf));

			// come back later if we don' thave enough data			
			if (bytes_total < WRAPBUF_LEN) {
				UNLOCK_S;
				LOG_INFO("need more audio data");
				return DECODE_RUNNING;
			}

		} else if (found == -1) {

			LOG_WARN("error reading stream header");
			UNLOCK_S;
			return DECODE_ERROR;

		} else {

			// not finished header parsing come back next time
			UNLOCK_S;
			LOG_DEBUG("header not found yet");
			return DECODE_RUNNING;
		}
	}

	// we always have at least WRAPBUF_LEN unless it's the end of a stream	
	if (bytes_wrap < WRAPBUF_LEN && bytes_wrap != bytes_total) {		
		// build a linear buffer if we are crossing the end of streambuf
		memcpy(a->wrap_buf, streambuf->readp, bytes_wrap);
		memcpy(a->wrap_buf + bytes_wrap, streambuf->buf, min(WRAPBUF_LEN, bytes_total) - bytes_wrap);		
		sptr = a->wrap_buf;
		bytes = bytes_wrap = min(WRAPBUF_LEN, bytes_total);
	} else {
		sptr = streambuf->readp;
		bytes = bytes_wrap;
	}
	
	// decode function changes iptr, so can't use streambuf->readp (same for bytes)
	res = HAAC(a, Decode, a->hAac, &sptr, &bytes, (s16_t*) a->write_buf);
	if (res  < 0) {
		LOG_WARN("AAC decode error %d", res);
	}

	HAAC(a, GetLastFrameInfo, a->hAac, &info);
	iptr = (s16_t*) a->write_buf;
	bytes = bytes_wrap - bytes;
	endstream = false;

	if (a->chunkinfo && a->chunkinfo[a->nextchunk].offset && a->sample++ == a->chunkinfo[a->nextchunk].sample) {
		// mp4 end of chunk - skip to next offset
		if (a->chunkinfo[a->nextchunk].offset > a->pos) {
			u32_t skip = a->chunkinfo[a->nextchunk].offset - a->pos;
			if (skip != bytes) {
				LOG_DEBUG("skipping to next chunk pos: %u consumed: %u != skip: %u", a->pos, bytes, skip);
			}
			if (bytes_total >= skip) {
				_buf_inc_readp(streambuf, skip);
				a->pos += skip;
			} else {
				a->consume = skip;
			}
			a->nextchunk++;
		} else {
			LOG_ERROR("error: need to skip backwards!");
			endstream = true;
		}
	} else if (bytes > 0) {
		// adts and mp4 when not at end of chunk 
		_buf_inc_readp(streambuf, bytes);
		a->pos += bytes;
	} else {
		// error which doesn't advance streambuf - end
		endstream = true;
	}

	UNLOCK_S;

	if (endstream) {
		LOG_WARN("unable to decode further");
		return DECODE_ERROR;
	}

	if (!info.outputSamps) {
		a->empty = true;
		return DECODE_RUNNING;
	}
	
	frames = info.outputSamps / info.nChans;

	if (a->skip) {
		u32_t skip;
		if (a->empty) {
			a->empty = false;
			a->skip -= frames;
			LOG_DEBUG("gapless: first frame empty, skipped %u frames at start", frames);
		}
		skip = min(frames, a->skip);
		LOG_DEBUG("gapless: skipping %u frames at start", skip);
		frames -= skip;
		a->skip -= skip;
		iptr += skip * info.nChans;
	}

	if (a->samples) {
		if (a->samples < frames) {
			LOG_DEBUG("gapless: trimming %u frames from end", frames - a->samples);
			frames = (frames_t)a->samples;
		}
		a->samples -= frames;
	}

	LOG_SDEBUG("write %u frames", frames);

	LOCK_O_direct;

	while (frames > 0) {
		frames_t f;
		frames_t count;
		ISAMPLE_T *optr;
		
		IF_DIRECT(
			f = _buf_cont_write(outputbuf) / BYTES_PER_FRAME;
			optr = (ISAMPLE_T *)outputbuf->writep;
		);
		IF_PROCESS(
			f = process.max_in_frames;
			optr = (ISAMPLE_T *)process.inbuf;
		);

		f = min(f, frames);
		count = f;
		
		if (info.nChans == 2) {
#if BYTES_PER_FRAME == 4			
			memcpy(optr, iptr, count * BYTES_PER_FRAME);
			iptr += count * 2;
#else 			
			while (count--) {
				*optr++ = ALIGN(*iptr++);
				*optr++ = ALIGN(*iptr++);
			}
#endif			
		} else if (info.nChans == 1) {
			while (count--) {
				*optr++ = ALIGN(*iptr);
				*optr++ = ALIGN(*iptr++);
			}
		} else {
			LOG_WARN("unsupported number of channels");
		}

		frames -= f;

		IF_DIRECT(
			_buf_inc_writep(outputbuf, f * BYTES_PER_FRAME);
		);
		IF_PROCESS(
			process.in_frames = f;
			if (frames) LOG_ERROR("unhandled case");
		);
	}
	
	UNLOCK_O_direct;

	return DECODE_RUNNING;
}

static void helixaac_open(u8_t size, u8_t rate, u8_t chan, u8_t endianness) {
	LOG_INFO("opening %s stream", size == '2' ? "adts" : "mp4");

	a->type = size;
	a->pos = a->consume = a->sample = a->nextchunk = 0;
	
	if (a->chunkinfo) {
		free(a->chunkinfo);
	}
	
	if (a->stsc) {
		free(a->stsc);
	}
	
	a->chunkinfo = NULL;
	a->stsc = NULL;
	a->skip = 0;
	a->samples = 0;
	a->sttssamples = 0;
	a->empty = false;

	if (a->hAac) {
		// always free decoder as flush only works when no parameter has changed
		HAAC(a, FreeDecoder, a->hAac);			
	} else {
		a->write_buf = malloc(FRAME_BUF * 4);
		a->wrap_buf = malloc(WRAPBUF_LEN);
	}
	
	a->hAac = HAAC(a, InitDecoder);	
}

static void helixaac_close(void) {
	HAAC(a, FreeDecoder, a->hAac);
	a->hAac = NULL;
	if (a->chunkinfo) {
		free(a->chunkinfo);
		a->chunkinfo = NULL;
	}
	if (a->stsc) {
		free(a->stsc);
		a->stsc = NULL;
	}
	free(a->write_buf);
	free(a->wrap_buf);
}

static bool load_helixaac() {
#if !LINKALL
	void *handle = dlopen(LIBHELIX-AAC, RTLD_NOW);
	char *err;

	if (!handle) {
		LOG_INFO("dlerror: %s", dlerror());
		return false;
	}

	// load symbols here

	if ((err = dlerror()) != NULL) {
		LOG_INFO("dlerror: %s", err);		
		return false;
	}

	LOG_INFO("loaded "LIBHELIX-AAC"");
#endif

	return true;
}

struct codec *register_helixaac(void) {
	static struct codec ret = { 
		'a',          // id
		"aac",        // types
		WRAPBUF_LEN,  // min read
		20480,        // min space
		helixaac_open,    // open
		helixaac_close,   // close
		helixaac_decode,  // decode
	};

	a = malloc(sizeof(struct helixaac));
	if (!a) {
		return NULL;
	}

	a->hAac = NULL;
	a->chunkinfo = NULL;
	a->stsc = NULL;

	if (!load_helixaac()) {
		return NULL;
	}

	LOG_INFO("using helix-aac to decode aac");
	return &ret;
}
