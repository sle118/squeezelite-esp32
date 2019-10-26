/* 
 *  Squeezelite - lightweight headless squeezebox emulator
 *
 *  (c) Adrian Smith 2012-2015, triode1@btinternet.com
 *      Ralph Irving 2015-2017, ralph_irving@hotmail.com
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

/* 
*  with some low-end CPU, the decode call takes a fair bit of time and if the outputbuf is locked during that
*  period, the output_thread (or equivalent) will be locked although there is plenty of samples available.
*  Normally, with PRIO_INHERIT, that thread should increase decoder priority and get the lock quickly but it
*  seems that when the streambuf has plenty of data, the decode thread grabs the CPU to much, even it the output
*  thread has a higher priority. Using an interim buffer where vorbis decoder writes the output is not great from
*  an efficiency (one extra memory copy) point of view, but it allows the lock to not be kept for too long
*/
#define FRAME_BUF 2048

#if BYTES_PER_FRAME == 4		
#define ALIGN(n) 	(n)
#else
#define ALIGN(n) 	(n << 16)		
#endif

// automatically select between floating point (preferred) and fixed point libraries:
// NOTE: works with Tremor version here: http://svn.xiph.org/trunk/Tremor, not vorbisidec.1.0.2 currently in ubuntu

// we take common definations from <vorbis/vorbisfile.h> even though we can use tremor at run time
// tremor's OggVorbis_File struct is normally smaller so this is ok, but padding added to malloc in case it is bigger
#define OV_EXCLUDE_STATIC_CALLBACKS

#ifdef TREMOR_ONLY
#include <ivorbisfile.h>
#else
#include <vorbis/vorbisfile.h>
#endif

struct vorbis {
	OggVorbis_File *vf;
	bool opened;
#if FRAME_BUF	
	u8_t *write_buf;
#endif	
#if !LINKALL
	// vorbis symbols to be dynamically loaded - from either vorbisfile or vorbisidec (tremor) version of library
	vorbis_info *(* ov_info)(OggVorbis_File *vf, int link);
	int (* ov_clear)(OggVorbis_File *vf);
	long (* ov_read)(OggVorbis_File *vf, char *buffer, int length, int bigendianp, int word, int sgned, int *bitstream);
	long (* ov_read_tremor)(OggVorbis_File *vf, char *buffer, int length, int *bitstream);
	int (* ov_open_callbacks)(void *datasource, OggVorbis_File *vf, const char *initial, long ibytes, ov_callbacks callbacks);
#endif
};

static struct vorbis *v;

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
#define LOCK_O_not_direct   if (!decode.direct) mutex_lock(outputbuf->mutex)
#define UNLOCK_O_not_direct if (!decode.direct) mutex_unlock(outputbuf->mutex)
#define IF_DIRECT(x)    if (decode.direct) { x }
#define IF_PROCESS(x)   if (!decode.direct) { x }
#else
#define LOCK_O_direct   mutex_lock(outputbuf->mutex)
#define UNLOCK_O_direct mutex_unlock(outputbuf->mutex)
#define LOCK_O_not_direct
#define UNLOCK_O_not_direct
#define IF_DIRECT(x)    { x }
#define IF_PROCESS(x)
#endif

#if LINKALL
#define OV(h, fn, ...) (ov_ ## fn)(__VA_ARGS__)
#define TREMOR(h)      0
#if !WIN
extern int ov_read_tremor(); // needed to enable compilation, not linked
#endif
#else
#define OV(h, fn, ...) (h)->ov_##fn(__VA_ARGS__)
#define TREMOR(h)      (h)->ov_read_tremor
#endif

// called with mutex locked within vorbis_decode to avoid locking O before S
static size_t _read_cb(void *ptr, size_t size, size_t nmemb, void *datasource) {
	size_t bytes;

	LOCK_S;

	bytes = min(_buf_used(streambuf), _buf_cont_read(streambuf));
	bytes = min(bytes, size * nmemb);

	memcpy(ptr, streambuf->readp, bytes);
	_buf_inc_readp(streambuf, bytes);

	UNLOCK_S;

	return bytes / size;
}

// these are needed for older versions of tremor, later versions and libvorbis allow NULL to be used
static int _seek_cb(void *datasource, ogg_int64_t offset, int whence) {  return -1; }
static int _close_cb(void *datasource) { return 0; }
static long _tell_cb(void *datasource) { return 0; }

static decode_state vorbis_decode(void) {
	static int channels;
	frames_t frames;
	int bytes, s, n;
	u8_t *write_buf;

	LOCK_S;

	if (stream.state <= DISCONNECT && !_buf_used(streambuf)) {
		UNLOCK_S;
		return DECODE_COMPLETE;
	}
	
	UNLOCK_S;
	
	if (decode.new_stream) {
		ov_callbacks cbs;
		int err;
		struct vorbis_info *info;

		cbs.read_func = _read_cb;

		if (TREMOR(v)) {
			cbs.seek_func = _seek_cb; cbs.close_func = _close_cb; cbs.tell_func = _tell_cb;
		} else {
			cbs.seek_func = NULL; cbs.close_func = NULL; cbs.tell_func = NULL;
		}

		if ((err = OV(v, open_callbacks, streambuf, v->vf, NULL, 0, cbs)) < 0) {
			LOG_WARN("open_callbacks error: %d", err);
			return DECODE_COMPLETE;
		}

		v->opened = true;
		info = OV(v, info, v->vf, -1);

		LOG_INFO("setting track_start");
		LOCK_O;
		output.next_sample_rate = decode_newstream(info->rate, output.supported_rates);
		IF_DSD(	output.next_fmt = PCM; )
		output.track_start = outputbuf->writep;
		if (output.fade_mode) _checkfade(true);
		decode.new_stream = false;
		UNLOCK_O;

		channels = info->channels;

		if (channels > 2) {
			LOG_WARN("too many channels: %d", channels);
			return DECODE_ERROR;
		}
	}
	
#if !FRAME_BUF		
	LOCK_O_direct;
#endif	

#if FRAME_BUF
	IF_DIRECT(
		frames = min(_buf_space(outputbuf), _buf_cont_write(outputbuf)) / BYTES_PER_FRAME;
		write_buf = v->write_buf;
	);
#else
	IF_DIRECT(
		frames = min(_buf_space(outputbuf), _buf_cont_write(outputbuf)) / BYTES_PER_FRAME;
		write_buf = outputbuf->writep;
	);
#endif
	IF_PROCESS(
		frames = process.max_in_frames;
		write_buf = process.inbuf;
	);
	
#if FRAME_BUF	
	frames = min(frames, FRAME_BUF);
#endif	
	bytes = frames * 2 * channels; // samples returned are 16 bits

	// write the decoded frames into outputbuf even though they are 16 bits per sample, then unpack them
#ifdef TREMOR_ONLY	
	n = OV(v, read, v->vf, (char *)write_buf, bytes, &s);
#else
	if (!TREMOR(v)) {
#if SL_LITTLE_ENDIAN
		n = OV(v, read, v->vf, (char *)write_buf, bytes, 0, 2, 1, &s);
#else
		n = OV(v, read, v->vf, (char *)write_buf, bytes, 1, 2, 1, &s);
#endif
#if !WIN
	} else {
		n = OV(v, read_tremor, v->vf, (char *)write_buf, bytes, &s);
#endif
	}
#endif	

#if FRAME_BUF
	LOCK_O_direct;
#endif	

	if (n > 0) {
		frames_t count;
		s16_t *iptr;
		ISAMPLE_T *optr;

		frames = n / 2 / channels;
		count = frames * channels;

		iptr = (s16_t *)write_buf + count;
		optr = (ISAMPLE_T *) outputbuf->writep + frames * 2;

		if (channels == 2) {
#if BYTES_PER_FRAME == 4
			memcpy(outputbuf->writep, write_buf, frames * BYTES_PER_FRAME);
#else
			while (count--) {
				*--optr = *--iptr << 16;
			}
#endif
		} else if (channels == 1) {
			while (count--) {
				*--optr = ALIGN(*--iptr);
				*--optr = ALIGN(*iptr);
			}
		}
		
		IF_DIRECT(
			_buf_inc_writep(outputbuf, frames * BYTES_PER_FRAME);
		);
		IF_PROCESS(
			process.in_frames = frames;
		);

		LOG_SDEBUG("wrote %u frames", frames);

	} else if (n == 0) {

		if (stream.state <= DISCONNECT) {
			LOG_INFO("partial decode");
			UNLOCK_O_direct;
			return DECODE_COMPLETE;
		} else {
			LOG_INFO("no frame decoded");
        }

	} else if (n == OV_HOLE) {

		// recoverable hole in stream, seen when skipping
		LOG_DEBUG("hole in stream");

	} else {

		LOG_INFO("ov_read error: %d", n);
		UNLOCK_O_direct;
		return DECODE_COMPLETE;
	}

	UNLOCK_O_direct;
	return DECODE_RUNNING;
}

static void vorbis_open(u8_t size, u8_t rate, u8_t chan, u8_t endianness) {
	if (!v->vf) {
		v->vf = malloc(sizeof(OggVorbis_File) + 128); // add some padding as struct size may be larger
		memset(v->vf, 0, sizeof(OggVorbis_File) + 128);
#if FRAME_BUF		
		v->write_buf = malloc(FRAME_BUF * BYTES_PER_FRAME);
#endif		
	} else {
		if (v->opened) {
			OV(v, clear, v->vf);
			v->opened = false;
		}
	}
}

static void vorbis_close(void) {
	if (v->opened) {
		OV(v, clear, v->vf);
		v->opened = false;
	}
	free(v->vf);
#if FRAME_BUF	
	free(v->write_buf);
	v->write_buf = NULL;
#endif	
	v->vf = NULL;
}

static bool load_vorbis() {
#if !LINKALL
	void *handle = dlopen(LIBVORBIS, RTLD_NOW);
	char *err;
	bool tremor = false;

	if (!handle) {
		handle = dlopen(LIBTREMOR, RTLD_NOW);
		if (handle) {
			tremor = true;
		} else {
			LOG_INFO("dlerror: %s", dlerror());
			return false;
		}
	}

	v->ov_read = tremor ? NULL : dlsym(handle, "ov_read");
	v->ov_read_tremor = tremor ? dlsym(handle, "ov_read") : NULL;
	v->ov_info = dlsym(handle, "ov_info");
	v->ov_clear = dlsym(handle, "ov_clear");
	v->ov_open_callbacks = dlsym(handle, "ov_open_callbacks");
	
	if ((err = dlerror()) != NULL) {
		LOG_INFO("dlerror: %s", err);		
		return false;
	}
	
	LOG_INFO("loaded %s", tremor ? LIBTREMOR : LIBVORBIS);
#endif

	return true;
}

struct codec *register_vorbis(void) {
	static struct codec ret = {
		'o',          // id
		"ogg",        // types
		4096,         // min read
		20480,        // min space
		vorbis_open,  // open
		vorbis_close, // close
		vorbis_decode,// decode
	};

	v = malloc(sizeof(struct vorbis));
	if (!v) {
		return NULL;
	}

	v->vf = NULL;
	v->opened = false;

	if (!load_vorbis()) {
		return NULL;
	}

	LOG_INFO("using vorbis to decode ogg");
	return &ret;
}
