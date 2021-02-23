/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Peter Schlaile <peter [at] schlaile [dot] de> 2011
 */

/** \file
 * \ingroup imbuf
 */

#include <stdlib.h>

#include "MEM_guardedalloc.h"

#include "BLI_endian_switch.h"
#include "BLI_fileops.h"
#include "BLI_ghash.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"
#ifdef _WIN32
#  include "BLI_winstuff.h"
#endif

#include "IMB_anim.h"
#include "IMB_indexer.h"
#include "imbuf.h"

#include "BKE_global.h"

#ifdef WITH_AVI
#  include "AVI_avi.h"
#endif

#ifdef WITH_FFMPEG
#  include "ffmpeg_compat.h"
#endif

static const char magic[] = "BlenMIdx";
static const char temp_ext[] = "_part";

static const int proxy_sizes[] = {IMB_PROXY_25, IMB_PROXY_50, IMB_PROXY_75, IMB_PROXY_100};
static const float proxy_fac[] = {0.25, 0.50, 0.75, 1.00};

#ifdef WITH_FFMPEG
static int tc_types[] = {
    IMB_TC_RECORD_RUN,
    IMB_TC_FREE_RUN,
    IMB_TC_INTERPOLATED_REC_DATE_FREE_RUN,
    IMB_TC_RECORD_RUN_NO_GAPS,
};
#endif

#define INDEX_FILE_VERSION 1

/* ----------------------------------------------------------------------
 * - time code index functions
 * ---------------------------------------------------------------------- */

anim_index_builder *IMB_index_builder_create(const char *name)
{

  anim_index_builder *rv = MEM_callocN(sizeof(struct anim_index_builder), "index builder");

  fprintf(stderr, "Starting work on index: %s\n", name);

  BLI_strncpy(rv->name, name, sizeof(rv->name));
  BLI_strncpy(rv->temp_name, name, sizeof(rv->temp_name));

  strcat(rv->temp_name, temp_ext);

  BLI_make_existing_file(rv->temp_name);

  rv->fp = BLI_fopen(rv->temp_name, "wb");

  if (!rv->fp) {
    fprintf(stderr,
            "Couldn't open index target: %s! "
            "Index build broken!\n",
            rv->temp_name);
    MEM_freeN(rv);
    return NULL;
  }

  fprintf(rv->fp, "%s%c%.3d", magic, (ENDIAN_ORDER == B_ENDIAN) ? 'V' : 'v', INDEX_FILE_VERSION);

  return rv;
}

void IMB_index_builder_add_entry(
    anim_index_builder *fp, int frameno, uint64_t seek_pos, uint64_t seek_pos_dts, uint64_t pts)
{
  fwrite(&frameno, sizeof(int), 1, fp->fp);
  fwrite(&seek_pos, sizeof(uint64_t), 1, fp->fp);
  fwrite(&seek_pos_dts, sizeof(uint64_t), 1, fp->fp);
  fwrite(&pts, sizeof(uint64_t), 1, fp->fp);
}

void IMB_index_builder_proc_frame(anim_index_builder *fp,
                                  uchar *buffer,
                                  int data_size,
                                  int frameno,
                                  uint64_t seek_pos,
                                  uint64_t seek_pos_dts,
                                  uint64_t pts)
{
  if (fp->proc_frame) {
    anim_index_entry e;
    e.frameno = frameno;
    e.seek_pos = seek_pos;
    e.seek_pos_dts = seek_pos_dts;
    e.pts = pts;

    fp->proc_frame(fp, buffer, data_size, &e);
  }
  else {
    IMB_index_builder_add_entry(fp, frameno, seek_pos, seek_pos_dts, pts);
  }
}

void IMB_index_builder_finish(anim_index_builder *fp, int rollback)
{
  if (fp->delete_priv_data) {
    fp->delete_priv_data(fp);
  }

  fclose(fp->fp);

  if (rollback) {
    unlink(fp->temp_name);
  }
  else {
    unlink(fp->name);
    BLI_rename(fp->temp_name, fp->name);
  }

  MEM_freeN(fp);
}

struct anim_index *IMB_indexer_open(const char *name)
{
  char header[13];
  struct anim_index *idx;
  FILE *fp = BLI_fopen(name, "rb");
  int i;

  if (!fp) {
    return NULL;
  }

  if (fread(header, 12, 1, fp) != 1) {
    fclose(fp);
    return NULL;
  }

  header[12] = 0;

  if (memcmp(header, magic, 8) != 0) {
    fclose(fp);
    return NULL;
  }

  if (atoi(header + 9) != INDEX_FILE_VERSION) {
    fclose(fp);
    return NULL;
  }

  idx = MEM_callocN(sizeof(struct anim_index), "anim_index");

  BLI_strncpy(idx->name, name, sizeof(idx->name));

  fseek(fp, 0, SEEK_END);

  idx->num_entries = (ftell(fp) - 12) / (sizeof(int) +      /* framepos */
                                         sizeof(uint64_t) + /* seek_pos */
                                         sizeof(uint64_t) + /* seek_pos_dts */
                                         sizeof(uint64_t)   /* pts */
                                        );

  fseek(fp, 12, SEEK_SET);

  idx->entries = MEM_callocN(sizeof(struct anim_index_entry) * idx->num_entries,
                             "anim_index_entries");

  size_t items_read = 0;
  for (i = 0; i < idx->num_entries; i++) {
    items_read += fread(&idx->entries[i].frameno, sizeof(int), 1, fp);
    items_read += fread(&idx->entries[i].seek_pos, sizeof(uint64_t), 1, fp);
    items_read += fread(&idx->entries[i].seek_pos_dts, sizeof(uint64_t), 1, fp);
    items_read += fread(&idx->entries[i].pts, sizeof(uint64_t), 1, fp);
  }

  if (UNLIKELY(items_read != idx->num_entries * 4)) {
    perror("error reading animation index file");
    MEM_freeN(idx->entries);
    MEM_freeN(idx);
    fclose(fp);
    return NULL;
  }

  if (((ENDIAN_ORDER == B_ENDIAN) != (header[8] == 'V'))) {
    for (i = 0; i < idx->num_entries; i++) {
      BLI_endian_switch_int32(&idx->entries[i].frameno);
      BLI_endian_switch_uint64(&idx->entries[i].seek_pos);
      BLI_endian_switch_uint64(&idx->entries[i].seek_pos_dts);
      BLI_endian_switch_uint64(&idx->entries[i].pts);
    }
  }

  fclose(fp);

  return idx;
}

uint64_t IMB_indexer_get_seek_pos(struct anim_index *idx, int frame_index)
{
  if (frame_index < 0) {
    frame_index = 0;
  }
  if (frame_index >= idx->num_entries) {
    frame_index = idx->num_entries - 1;
  }
  return idx->entries[frame_index].seek_pos;
}

uint64_t IMB_indexer_get_seek_pos_dts(struct anim_index *idx, int frame_index)
{
  if (frame_index < 0) {
    frame_index = 0;
  }
  if (frame_index >= idx->num_entries) {
    frame_index = idx->num_entries - 1;
  }
  return idx->entries[frame_index].seek_pos_dts;
}

int IMB_indexer_get_frame_index(struct anim_index *idx, int frameno)
{
  int len = idx->num_entries;
  int half;
  int middle;
  int first = 0;

  /* bsearch (lower bound) the right index */

  while (len > 0) {
    half = len >> 1;
    middle = first;

    middle += half;

    if (idx->entries[middle].frameno < frameno) {
      first = middle;
      first++;
      len = len - half - 1;
    }
    else {
      len = half;
    }
  }

  if (first == idx->num_entries) {
    return idx->num_entries - 1;
  }

  return first;
}

uint64_t IMB_indexer_get_pts(struct anim_index *idx, int frame_index)
{
  if (frame_index < 0) {
    frame_index = 0;
  }
  if (frame_index >= idx->num_entries) {
    frame_index = idx->num_entries - 1;
  }
  return idx->entries[frame_index].pts;
}

int IMB_indexer_get_duration(struct anim_index *idx)
{
  if (idx->num_entries == 0) {
    return 0;
  }
  return idx->entries[idx->num_entries - 1].frameno + 1;
}

int IMB_indexer_can_scan(struct anim_index *idx, int old_frame_index, int new_frame_index)
{
  /* makes only sense, if it is the same I-Frame and we are not
   * trying to run backwards in time... */
  return (IMB_indexer_get_seek_pos(idx, old_frame_index) ==
              IMB_indexer_get_seek_pos(idx, new_frame_index) &&
          old_frame_index < new_frame_index);
}

void IMB_indexer_close(struct anim_index *idx)
{
  MEM_freeN(idx->entries);
  MEM_freeN(idx);
}

int IMB_proxy_size_to_array_index(IMB_Proxy_Size pr_size)
{
  switch (pr_size) {
    case IMB_PROXY_NONE:
      /* if we got here, something is broken anyways, so sane defaults... */
      return 0;
    case IMB_PROXY_25:
      return 0;
    case IMB_PROXY_50:
      return 1;
    case IMB_PROXY_75:
      return 2;
    case IMB_PROXY_100:
      return 3;
    default:
      return 0;
  }
}

int IMB_timecode_to_array_index(IMB_Timecode_Type tc)
{
  switch (tc) {
    case IMB_TC_NONE: /* if we got here, something is broken anyways,
                       * so sane defaults... */
      return 0;
    case IMB_TC_RECORD_RUN:
      return 0;
    case IMB_TC_FREE_RUN:
      return 1;
    case IMB_TC_INTERPOLATED_REC_DATE_FREE_RUN:
      return 2;
    case IMB_TC_RECORD_RUN_NO_GAPS:
      return 3;
    default:
      return 0;
  }
}

/* ----------------------------------------------------------------------
 * - rebuild helper functions
 * ---------------------------------------------------------------------- */

static void get_index_dir(struct anim *anim, char *index_dir, size_t index_dir_len)
{
  if (!anim->index_dir[0]) {
    char fname[FILE_MAXFILE];
    BLI_split_dirfile(anim->name, index_dir, fname, index_dir_len, sizeof(fname));
    BLI_path_append(index_dir, index_dir_len, "BL_proxy");
    BLI_path_append(index_dir, index_dir_len, fname);
  }
  else {
    BLI_strncpy(index_dir, anim->index_dir, index_dir_len);
  }
}

void IMB_anim_get_fname(struct anim *anim, char *file, int size)
{
  char fname[FILE_MAXFILE];
  BLI_split_dirfile(anim->name, file, fname, size, sizeof(fname));
  BLI_strncpy(file, fname, size);
}

static bool get_proxy_filename(struct anim *anim,
                               IMB_Proxy_Size preview_size,
                               char *fname,
                               bool temp)
{
  char index_dir[FILE_MAXDIR];
  int i = IMB_proxy_size_to_array_index(preview_size);

  char proxy_name[256];
  char stream_suffix[20];
  const char *name = (temp) ? "proxy_%d%s_part.avi" : "proxy_%d%s.avi";

  stream_suffix[0] = 0;

  if (anim->streamindex > 0) {
    BLI_snprintf(stream_suffix, sizeof(stream_suffix), "_st%d", anim->streamindex);
  }

  BLI_snprintf(proxy_name,
               sizeof(proxy_name),
               name,
               (int)(proxy_fac[i] * 100),
               stream_suffix,
               anim->suffix);

  get_index_dir(anim, index_dir, sizeof(index_dir));

  if (BLI_path_ncmp(anim->name, index_dir, FILE_MAXDIR) == 0) {
    return false;
  }

  BLI_join_dirfile(fname, FILE_MAXFILE + FILE_MAXDIR, index_dir, proxy_name);
  return true;
}

static void get_tc_filename(struct anim *anim, IMB_Timecode_Type tc, char *fname)
{
  char index_dir[FILE_MAXDIR];
  int i = IMB_timecode_to_array_index(tc);
  const char *index_names[] = {
      "record_run%s%s.blen_tc",
      "free_run%s%s.blen_tc",
      "interp_free_run%s%s.blen_tc",
      "record_run_no_gaps%s%s.blen_tc",
  };

  char stream_suffix[20];
  char index_name[256];

  stream_suffix[0] = 0;

  if (anim->streamindex > 0) {
    BLI_snprintf(stream_suffix, 20, "_st%d", anim->streamindex);
  }

  BLI_snprintf(index_name, 256, index_names[i], stream_suffix, anim->suffix);

  get_index_dir(anim, index_dir, sizeof(index_dir));

  BLI_join_dirfile(fname, FILE_MAXFILE + FILE_MAXDIR, index_dir, index_name);
}

/* ----------------------------------------------------------------------
 * - common rebuilder structures
 * ---------------------------------------------------------------------- */

typedef struct IndexBuildContext {
  int anim_type;
} IndexBuildContext;

/* ----------------------------------------------------------------------
 * - ffmpeg rebuilder
 * ---------------------------------------------------------------------- */

#ifdef WITH_FFMPEG

struct input_ctx {
  AVFormatContext *format_context;
  AVCodecContext *codec_context;
  AVCodec *codec;
  AVStream *stream;
  int video_stream;
};

struct proxy_output_ctx {
  AVFormatContext *output_format;
  AVStream *stream;
  AVCodec *codec;
  AVCodecContext *codec_context;
  int cfra;
  int proxy_size;
  struct anim *anim;
};

struct transcode_output_ctx {
  AVCodecContext *codec_context;
  struct SwsContext *sws_ctx;
  int orig_height;
} transcode_output_ctx;

struct proxy_transcode_ctx {
  AVCodecContext *input_codec_context;
  struct transcode_output_ctx *output_context[IMB_PROXY_MAX_SLOT];
};

typedef struct FFmpegIndexBuilderContext {
  /* Common data for building process. */
  int anim_type;
  struct anim *anim;
  int quality;
  int num_proxy_sizes;
  int num_indexers;
  int num_transcode_threads;
  IMB_Timecode_Type tcs_in_use;
  IMB_Proxy_Size proxy_sizes_in_use;

  /* Builder contexts. */
  struct input_ctx *input_ctx;
  struct proxy_output_ctx *proxy_ctx[IMB_PROXY_MAX_SLOT];
  struct proxy_transcode_ctx **transcode_context_array;
  anim_index_builder *indexer[IMB_TC_MAX_SLOT];

  /* Common data for transcoding. */
  GHash *source_packets;
  GHash *transcoded_packets;

  /* Job coordination. */
  ThreadMutex packet_access_mutex;
  ThreadCondition reader_suspend_cond;
  ThreadMutex reader_suspend_mutex;
  ThreadCondition **transcode_suspend_cond;
  ThreadMutex **transcode_suspend_mutex;
  ThreadCondition writer_suspend_cond;
  ThreadMutex writer_suspend_mutex;
  ThreadCondition builder_suspend_cond;
  ThreadMutex builder_suspend_mutex;
  bool all_packets_read;
  int transcode_jobs_done;
  int last_gop_chunk_written;
  bool all_packets_written;
  short *stop;
  short *do_update;
  float *progress;

  /* TC index building */
  uint64_t seek_pos;
  uint64_t last_seek_pos;
  uint64_t seek_pos_dts;
  uint64_t seek_pos_pts;
  uint64_t last_seek_pos_dts;
  uint64_t start_pts;
  double frame_rate;
  double pts_time_base;
  int frameno, frameno_gapless;
  int start_pts_set;
} FFmpegIndexBuilderContext;

// work around stupid swscaler 16 bytes alignment bug...

static int round_up(int x, int mod)
{
  return x + ((mod - (x % mod)) % mod);
}

static struct SwsContext *alloc_proxy_output_sws_context(AVCodecContext *input_codec_ctx,
                                                         AVCodecContext *proxy_codec_ctx)
{
  struct SwsContext *sws_ctx = sws_getContext(input_codec_ctx->width,
                                              av_get_cropped_height_from_codec(input_codec_ctx),
                                              input_codec_ctx->pix_fmt,
                                              proxy_codec_ctx->width,
                                              proxy_codec_ctx->height,
                                              proxy_codec_ctx->pix_fmt,
                                              SWS_FAST_BILINEAR | SWS_PRINT_INFO,
                                              NULL,
                                              NULL,
                                              NULL);
  return sws_ctx;
}

static AVFormatContext *alloc_proxy_output_output_format_context(struct anim *anim, int proxy_size)
{
  char fname[FILE_MAX];

  get_proxy_filename(anim, proxy_size, fname, true);
  BLI_make_existing_file(fname);

  AVFormatContext *format_context = avformat_alloc_context();
  format_context->oformat = av_guess_format("avi", NULL, NULL);

  BLI_strncpy(format_context->filename, fname, sizeof(format_context->filename));

  /* Codec stuff must be initialized properly here. */
  if (avio_open(&format_context->pb, fname, AVIO_FLAG_WRITE) < 0) {
    fprintf(stderr,
            "Couldn't open outputfile! "
            "Proxy not built!\n");
    av_free(format_context);
    return NULL;
  }

  return format_context;
}

static struct proxy_output_ctx *alloc_proxy_output_ffmpeg(
    struct anim *anim, AVStream *inpuf_stream, int proxy_size, int width, int height, int quality)
{
  struct proxy_output_ctx *proxy_out_ctx = MEM_callocN(sizeof(struct proxy_output_ctx),
                                                       "alloc_proxy_output");

  proxy_out_ctx->proxy_size = proxy_size;
  proxy_out_ctx->anim = anim;

  proxy_out_ctx->output_format = alloc_proxy_output_output_format_context(anim, proxy_size);

  proxy_out_ctx->stream = avformat_new_stream(proxy_out_ctx->output_format, NULL);
  proxy_out_ctx->stream->id = 0;

  proxy_out_ctx->codec_context = proxy_out_ctx->stream->codec;
  proxy_out_ctx->codec_context->thread_count = BLI_system_thread_count();
  proxy_out_ctx->codec_context->thread_type = FF_THREAD_SLICE;
  proxy_out_ctx->codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
  proxy_out_ctx->codec_context->codec_id = AV_CODEC_ID_MJPEG;
  proxy_out_ctx->codec_context->width = width;
  proxy_out_ctx->codec_context->height = height;

  proxy_out_ctx->output_format->oformat->video_codec = proxy_out_ctx->codec_context->codec_id;
  proxy_out_ctx->codec = avcodec_find_encoder(proxy_out_ctx->codec_context->codec_id);

  if (!proxy_out_ctx->codec) {
    fprintf(stderr,
            "No ffmpeg MJPEG encoder available? "
            "Proxy not built!\n");
    av_free(proxy_out_ctx->output_format);
    return NULL;
  }

  if (proxy_out_ctx->codec->pix_fmts) {
    proxy_out_ctx->codec_context->pix_fmt = proxy_out_ctx->codec->pix_fmts[0];
  }
  else {
    proxy_out_ctx->codec_context->pix_fmt = AV_PIX_FMT_YUVJ420P;
  }

  proxy_out_ctx->codec_context->sample_aspect_ratio = proxy_out_ctx->stream->sample_aspect_ratio =
      inpuf_stream->codec->sample_aspect_ratio;

  proxy_out_ctx->codec_context->time_base.den = 25;
  proxy_out_ctx->codec_context->time_base.num = 1;
  proxy_out_ctx->stream->time_base = proxy_out_ctx->codec_context->time_base;

  if (proxy_out_ctx->output_format->flags & AVFMT_GLOBALHEADER) {
    proxy_out_ctx->codec_context->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }

  /* there's no  way to set JPEG quality in the same way as in AVI JPEG and image sequence,
   * but this seems to be giving expected quality result */
  int ffmpeg_quality = (int)(1.0f + 30.0f * (1.0f - (float)quality / 100.0f) + 0.5f);
  av_opt_set_int(proxy_out_ctx->codec_context, "qmin", ffmpeg_quality, 0);
  av_opt_set_int(proxy_out_ctx->codec_context, "qmax", ffmpeg_quality, 0);

  fprintf(stderr, "Starting work on proxy: %s\n", proxy_out_ctx->output_format->filename);
  if (avformat_write_header(proxy_out_ctx->output_format, NULL) < 0) {
    fprintf(stderr,
            "Couldn't set output parameters? "
            "Proxy not built!\n");
    av_free(proxy_out_ctx->output_format);
    return 0;
  }

  return proxy_out_ctx;
}

static void free_proxy_output_ffmpeg(struct proxy_output_ctx *ctx, int rollback)
{
  char fname[FILE_MAX];
  char fname_tmp[FILE_MAX];

  if (!ctx) {
    return;
  }

  av_write_trailer(ctx->output_format);

  if (ctx->output_format->oformat) {
    if (!(ctx->output_format->oformat->flags & AVFMT_NOFILE)) {
      avio_close(ctx->output_format->pb);
    }
  }
  avformat_free_context(ctx->output_format);

  get_proxy_filename(ctx->anim, ctx->proxy_size, fname_tmp, true);

  if (rollback) {
    unlink(fname_tmp);
  }
  else {
    get_proxy_filename(ctx->anim, ctx->proxy_size, fname, false);
    unlink(fname);
    BLI_rename(fname_tmp, fname);
  }

  MEM_freeN(ctx);
}

static AVFormatContext *index_ffmpeg_context_open_input_format(FFmpegIndexBuilderContext *context,
                                                               struct anim *anim)
{
  AVFormatContext *format_context = avformat_alloc_context();

  if (avformat_open_input(&format_context, anim->name, NULL, NULL) != 0) {
    return NULL;
  }

  if (avformat_find_stream_info(format_context, NULL) < 0) {
    avformat_close_input(&format_context);
    return NULL;
  }

  return format_context;
}

static int index_ffmpeg_context_find_video_stream(struct anim *anim,
                                                  AVFormatContext *format_context)
{
  int streamcount = anim->streamindex;
  int video_stream = -1;
  for (int i = 0; i < format_context->nb_streams; i++) {
    if (format_context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
      if (streamcount > 0) {
        streamcount--;
        continue;
      }
      video_stream = i;
      break;
    }
  }

  return video_stream;
}

static struct input_ctx *index_ffmpeg_create_input_context(FFmpegIndexBuilderContext *context)
{
  struct input_ctx *input_context = MEM_callocN(sizeof(struct input_ctx), "input context");

  input_context->format_context = index_ffmpeg_context_open_input_format(context, context->anim);
  if (input_context->format_context == NULL) {
    MEM_freeN(input_context);
    return NULL;
  }

  input_context->video_stream = index_ffmpeg_context_find_video_stream(
      context->anim, input_context->format_context);
  if (input_context->video_stream == -1) {
    avformat_close_input(&input_context->format_context);
    MEM_freeN(input_context);
    return NULL;
  }

  input_context->stream = input_context->format_context->streams[input_context->video_stream];
  input_context->codec_context = input_context->stream->codec;

  input_context->codec = avcodec_find_decoder(input_context->codec_context->codec_id);
  if (input_context->codec == NULL) {
    avformat_close_input(&input_context->format_context);
    MEM_freeN(input_context);
    return NULL;
  }

  input_context->codec_context->workaround_bugs = 1;

  if (avcodec_open2(input_context->codec_context, input_context->codec, NULL) < 0) {
    avformat_close_input(&input_context->format_context);
    MEM_freeN(input_context);
    return NULL;
  }

  input_context->codec = avcodec_find_decoder(input_context->codec_context->codec_id);
  if (input_context->codec == NULL) {
    avformat_close_input(&input_context->format_context);
    MEM_freeN(input_context);
    return NULL;
  }

  input_context->codec_context->workaround_bugs = 1;

  if (avcodec_open2(input_context->codec_context, input_context->codec, NULL) < 0) {
    avformat_close_input(&input_context->format_context);
    MEM_freeN(input_context);
    return NULL;
  }

  return input_context;
}

static void index_ffmpeg_free_input_context(struct input_ctx *input_context)
{
  avcodec_flush_buffers(input_context->codec_context);
  avcodec_close(input_context->codec_context);
  avformat_close_input(&input_context->format_context);
  MEM_freeN(input_context);
}

static IndexBuildContext *index_ffmpeg_create_context(struct anim *anim,
                                                      IMB_Timecode_Type tcs_in_use,
                                                      IMB_Proxy_Size proxy_sizes_in_use,
                                                      int quality)
{
  FFmpegIndexBuilderContext *context = MEM_callocN(sizeof(FFmpegIndexBuilderContext),
                                                   "FFmpeg index builder context");

  context->anim = anim;
  context->quality = quality;
  context->tcs_in_use = tcs_in_use;
  context->proxy_sizes_in_use = proxy_sizes_in_use;
  context->num_proxy_sizes = IMB_PROXY_MAX_SLOT;
  context->num_indexers = IMB_TC_MAX_SLOT;
  context->num_transcode_threads = BLI_system_thread_count();

  /* Setup input file context. */
  context->input_ctx = index_ffmpeg_create_input_context(context);
  if (context->input_ctx == NULL) {
    MEM_freeN(context);
    return NULL;
  }

  /* Setup proxy file writing context. */
  struct input_ctx *input_context = context->input_ctx;
  memset(context->proxy_ctx, 0, sizeof(context->proxy_ctx));
  for (int i = 0; i < context->num_proxy_sizes; i++) {
    if (context->proxy_sizes_in_use & proxy_sizes[i]) {
      context->proxy_ctx[i] = alloc_proxy_output_ffmpeg(
          context->anim,
          input_context->stream,
          proxy_sizes[i],
          input_context->codec_context->width * proxy_fac[i],
          av_get_cropped_height_from_codec(input_context->codec_context) * proxy_fac[i],
          context->quality);
      if (!context->proxy_ctx[i]) {
        context->proxy_sizes_in_use &= ~proxy_sizes[i];
      }
    }
  }

  /* Setup indexing context. */
  memset(context->indexer, 0, sizeof(context->indexer));
  for (int i = 0; i < context->num_indexers; i++) {
    if (tcs_in_use & tc_types[i]) {
      char fname[FILE_MAX];

      get_tc_filename(anim, tc_types[i], fname);

      context->indexer[i] = IMB_index_builder_create(fname);
      if (!context->indexer[i]) {
        tcs_in_use &= ~tc_types[i];
      }
    }
  }

  return (IndexBuildContext *)context;
}

static void index_ffmpeg_free_context(FFmpegIndexBuilderContext *context, int stop)
{
  int i;

  index_ffmpeg_free_input_context(context->input_ctx);

  for (i = 0; i < context->num_indexers; i++) {
    if (context->tcs_in_use & tc_types[i]) {
      IMB_index_builder_finish(context->indexer[i], stop);
    }
  }

  for (i = 0; i < context->num_proxy_sizes; i++) {
    if (context->proxy_sizes_in_use & proxy_sizes[i]) {
      free_proxy_output_ffmpeg(context->proxy_ctx[i], stop);
    }
  }

  MEM_freeN(context);
}

static struct transcode_output_ctx *index_ffmpeg_create_transcode_output_context(
    AVStream *input_stream,
    AVStream *output_stream,
    AVCodecContext *proxy_codec_context,
    AVCodec *proxy_codec,
    int width,
    int height)
{
  struct transcode_output_ctx *output_ctx = MEM_callocN(sizeof(struct transcode_output_ctx),
                                                        "alloc_proxy_output");

  output_ctx->codec_context = avcodec_alloc_context3(NULL);
  avcodec_copy_context(output_ctx->codec_context, proxy_codec_context);
  avcodec_open2(output_ctx->codec_context, proxy_codec, NULL);

  output_ctx->orig_height = av_get_cropped_height_from_codec(input_stream->codec);

  if (input_stream->codec->width != width || input_stream->codec->height != height ||
      input_stream->codec->pix_fmt != output_ctx->codec_context->pix_fmt) {
    output_ctx->sws_ctx = alloc_proxy_output_sws_context(input_stream->codec,
                                                         output_ctx->codec_context);
  }

  return output_ctx;
}

static void index_ffmpeg_free_transcode_output_context(struct transcode_output_ctx *output_ctx)
{

  if (!output_ctx) {
    return;
  }

  sws_freeContext(output_ctx->sws_ctx);
  avcodec_close(output_ctx->codec_context);
  MEM_freeN(output_ctx);
}

static void index_ffmpeg_free_transcode_contexts(FFmpegIndexBuilderContext *context)
{
  for (int i = 0; i < context->num_transcode_threads; i++) {
    struct proxy_transcode_ctx *transcode_context = context->transcode_context_array[i];
    /* Free input codec context. */
    avcodec_flush_buffers(transcode_context->input_codec_context);
    avcodec_close(transcode_context->input_codec_context);

    /* Free mutex and thread condition */
    BLI_condition_end(context->transcode_suspend_cond[i]);
    MEM_freeN(context->transcode_suspend_cond[i]);
    BLI_mutex_free(context->transcode_suspend_mutex[i]);

    for (int size = 0; size < context->num_proxy_sizes; size++) {
      if (transcode_context->output_context[size] == NULL) {
        continue;
      }
      index_ffmpeg_free_transcode_output_context(transcode_context->output_context[size]);
    }
    MEM_freeN(transcode_context);
  }
  MEM_freeN(context->transcode_context_array);
  MEM_freeN(context->transcode_suspend_mutex);
  MEM_freeN(context->transcode_suspend_cond);
}

static void index_ffmpeg_create_transcode_context(FFmpegIndexBuilderContext *context,
                                                  short *stop,
                                                  short *do_update,
                                                  float *progress)
{
  /* Setup transcoding input contexts and data. */
  context->transcode_context_array = MEM_callocN(sizeof(context->transcode_context_array) *
                                                     context->num_transcode_threads,
                                                 "transcode context array");
  context->transcode_suspend_cond = MEM_callocN(sizeof(context->transcode_suspend_cond) *
                                                    context->num_transcode_threads,
                                                "transcode suspend cond array");
  context->transcode_suspend_mutex = MEM_callocN(sizeof(context->transcode_suspend_mutex) *
                                                     context->num_transcode_threads,
                                                 "transcode suspend mutex array");

  /* Job coordination */
  context->stop = stop;
  context->do_update = do_update;
  context->progress = progress;
  context->last_gop_chunk_written = 0;
  context->all_packets_written = false;
  BLI_condition_init(&context->reader_suspend_cond);
  BLI_condition_init(&context->writer_suspend_cond);
  BLI_condition_init(&context->builder_suspend_cond);
  BLI_mutex_init(&context->reader_suspend_mutex);
  BLI_mutex_init(&context->writer_suspend_mutex);
  BLI_mutex_init(&context->builder_suspend_mutex);
  BLI_mutex_init(&context->packet_access_mutex);
  context->source_packets = BLI_ghash_new(
      BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp, "source packets ghash");
  context->transcoded_packets = BLI_ghash_new(
      BLI_ghashutil_ptrhash, BLI_ghashutil_ptrcmp, "transcoded packets ghash");

  for (int i = 0; i < context->num_transcode_threads; i++) {
    context->transcode_context_array[i] = MEM_mallocN(sizeof(struct proxy_transcode_ctx),
                                                      "transcode context");
    struct proxy_transcode_ctx *transcode_context = context->transcode_context_array[i];

    /* Copy input codec context. */
    transcode_context->input_codec_context = avcodec_alloc_context3(NULL);
    avcodec_copy_context(transcode_context->input_codec_context,
                         context->input_ctx->codec_context);
    avcodec_open2(transcode_context->input_codec_context, context->input_ctx->codec, NULL);

    /* Setup mutex and therad condition. */
    context->transcode_suspend_mutex[i] = BLI_mutex_alloc();
    context->transcode_suspend_cond[i] = MEM_callocN(sizeof(ThreadCondition),
                                                     "transcode suspend cond");
    BLI_condition_init(context->transcode_suspend_cond[i]);
  }

  /* Setup transcoding output contexts. */
  for (int i = 0; i < context->num_transcode_threads; i++) {
    struct proxy_transcode_ctx *transcode_context = context->transcode_context_array[i];
    memset(transcode_context->output_context, 0, sizeof(transcode_context->output_context));
    for (int size = 0; size < context->num_proxy_sizes; size++) {
      struct proxy_output_ctx *proxy_context = context->proxy_ctx[size];
      if (proxy_context == NULL) {
        continue;
      }
      transcode_context->output_context[size] = index_ffmpeg_create_transcode_output_context(
          context->input_ctx->stream,
          proxy_context->stream,
          proxy_context->codec_context,
          proxy_context->codec,
          context->input_ctx->codec_context->width * proxy_fac[size],
          av_get_cropped_height_from_codec(context->input_ctx->codec_context) * proxy_fac[size]);
    }
  }
}

static void index_ffmpeg_free_transcode_context(FFmpegIndexBuilderContext *context)
{

  BLI_condition_end(&context->reader_suspend_cond);
  BLI_condition_end(&context->writer_suspend_cond);
  BLI_condition_end(&context->builder_suspend_cond);
  BLI_mutex_end(&context->reader_suspend_mutex);
  BLI_mutex_end(&context->writer_suspend_mutex);
  BLI_mutex_end(&context->builder_suspend_mutex);
  BLI_mutex_end(&context->packet_access_mutex);

  BLI_ghash_free(context->source_packets, NULL, MEM_freeN);
  BLI_ghash_free(context->transcoded_packets, NULL, MEM_freeN);

  index_ffmpeg_free_transcode_contexts(context);
}

typedef struct output_packet_wrap {
  AVPacket *output_packet[IMB_PROXY_MAX_SLOT];
  int frame_index;
  int gop_chunk_index;
  int64_t pos;
  bool is_transcoded;

  /* Needed for TC building.*/
  uint64_t pts_from_frame;
} output_packet_wrap;

typedef struct source_packet_wrap {
  AVPacket *input_packet;
  int frame_index;
  int gop_chunk_index;
} source_packet_wrap;

typedef struct TranscodeJob {
  FFmpegIndexBuilderContext *context;
  int thread_number;
} TranscodeJob;

static source_packet_wrap *create_source_packet_wrap(FFmpegIndexBuilderContext *context,
                                                     AVPacket *packet,
                                                     int gop_chunk_index,
                                                     uint64_t frame_index)
{
  source_packet_wrap *packet_wrap = MEM_callocN(sizeof(source_packet_wrap), "");
  packet_wrap->input_packet = packet;
  packet_wrap->frame_index = frame_index;
  packet_wrap->gop_chunk_index = gop_chunk_index;
  BLI_mutex_lock(&context->packet_access_mutex);
  BLI_ghash_insert(context->source_packets, (void *)frame_index, packet_wrap);
  BLI_mutex_unlock(&context->packet_access_mutex);
  return packet_wrap;
}

static output_packet_wrap *create_output_packet_wrap(FFmpegIndexBuilderContext *context,
                                                     AVPacket *packet,
                                                     int gop_chunk_index,
                                                     uint64_t frame_index)
{

  output_packet_wrap *packet_wrap = MEM_callocN(sizeof(output_packet_wrap), "");
  BLI_mutex_lock(&context->packet_access_mutex);
  BLI_ghash_insert(context->transcoded_packets, (void *)frame_index, packet_wrap);
  packet_wrap->pos = packet->pos;
  packet_wrap->frame_index = frame_index;
  packet_wrap->gop_chunk_index = gop_chunk_index;
  BLI_mutex_unlock(&context->packet_access_mutex);
  return packet_wrap;
}

static source_packet_wrap *get_source_packet_wrap(FFmpegIndexBuilderContext *context,
                                                  uint64_t index)
{
  BLI_mutex_lock(&context->packet_access_mutex);
  source_packet_wrap *packet_wrap = BLI_ghash_lookup(context->source_packets, (void *)index);
  BLI_mutex_unlock(&context->packet_access_mutex);
  return packet_wrap;
}

static output_packet_wrap *get_output_packet_wrap(FFmpegIndexBuilderContext *context,
                                                  uint64_t index)
{
  BLI_mutex_lock(&context->packet_access_mutex);
  output_packet_wrap *packet_wrap = BLI_ghash_lookup(context->transcoded_packets, (void *)index);
  BLI_mutex_unlock(&context->packet_access_mutex);
  return packet_wrap;
}

static int index_ffmpeg_transcode_source_packet_count_get(FFmpegIndexBuilderContext *context)
{
  GHash *av_packet_base = context->source_packets;
  BLI_mutex_lock(&context->packet_access_mutex);
  int packets_read = BLI_ghash_len(av_packet_base);
  BLI_mutex_unlock(&context->packet_access_mutex);
  return packets_read;
}

static void index_ffmpeg_read_resume(FFmpegIndexBuilderContext *context)
{
  ThreadCondition *suspend_cond = &context->reader_suspend_cond;
  BLI_condition_notify_one(suspend_cond);
}

static void index_ffmpeg_read_suspend(FFmpegIndexBuilderContext *context, int gop_chunk_index)
{
  /* All transcode threads must have at least 1 GOP chunk available. Greater lookahead will be
   * probably better for files with small GOP size. */
  int gop_lookahead_margin = context->num_transcode_threads * 5;
  ThreadMutex *suspend_mutex = &context->reader_suspend_mutex;
  ThreadCondition *suspend_cond = &context->reader_suspend_cond;
  BLI_mutex_lock(suspend_mutex);
  while (gop_chunk_index > (context->last_gop_chunk_written + gop_lookahead_margin) &&
         !context->all_packets_written) {
    BLI_condition_wait(suspend_cond, suspend_mutex);
  }
  BLI_mutex_unlock(suspend_mutex);
}

static void index_ffmpeg_transcode_resume(FFmpegIndexBuilderContext *context)
{
  for (int i = 0; i < context->num_transcode_threads; i++) {
    ThreadCondition *suspend_cond = context->transcode_suspend_cond[i];
    BLI_condition_notify_one(suspend_cond);
  }
}

static void index_ffmpeg_transcode_wait_for_packet(TranscodeJob *transcode_job, int frame_index)
{
  FFmpegIndexBuilderContext *context = transcode_job->context;
  ThreadMutex *suspend_mutex = context->transcode_suspend_mutex[transcode_job->thread_number];
  ThreadCondition *suspend_cond = context->transcode_suspend_cond[transcode_job->thread_number];
  BLI_mutex_lock(suspend_mutex);
  while (index_ffmpeg_transcode_source_packet_count_get(context) <= frame_index &&
         !context->all_packets_read) {
    BLI_condition_wait(suspend_cond, suspend_mutex);
  }
  BLI_mutex_unlock(suspend_mutex);
}

static void index_ffmpeg_write_resume(FFmpegIndexBuilderContext *context)
{
  ThreadCondition *suspend_cond = &context->writer_suspend_cond;
  BLI_condition_notify_one(suspend_cond);
}

static output_packet_wrap *get_decoded_output_packet_wrap(FFmpegIndexBuilderContext *context,
                                                          uint64_t index)
{
  ThreadMutex *suspend_mutex = &context->writer_suspend_mutex;
  ThreadCondition *suspend_cond = &context->writer_suspend_cond;
  BLI_mutex_lock(suspend_mutex);

  /* Try to get packet. */
  output_packet_wrap *packet_wrap = get_output_packet_wrap(context, index);
  while (!context->all_packets_read && packet_wrap == NULL) {
    BLI_condition_wait(suspend_cond, suspend_mutex);
    packet_wrap = get_output_packet_wrap(context, index);
  }

  if (packet_wrap == NULL) {
    BLI_mutex_unlock(suspend_mutex);
    return NULL;
  }

  /* Wait until packet is transcoded. */
  while (!packet_wrap->is_transcoded &&
         context->transcode_jobs_done < context->num_transcode_threads) {
    BLI_condition_wait(suspend_cond, suspend_mutex);
  }

  BLI_mutex_unlock(suspend_mutex);
  return packet_wrap;
}

static void build_timecode_index(FFmpegIndexBuilderContext *context, int frame_index)
{
  source_packet_wrap *source_wrap = get_source_packet_wrap(context, frame_index);
  output_packet_wrap *output_wrap = get_output_packet_wrap(context, frame_index);
  AVPacket *source_packet = source_wrap->input_packet;

  if (source_packet->flags & AV_PKT_FLAG_KEY) {
    context->last_seek_pos = context->seek_pos;
    context->last_seek_pos_dts = context->seek_pos_dts;
    context->seek_pos = source_packet->pos;
    context->seek_pos_dts = source_packet->dts;
    context->seek_pos_pts = source_packet->pts;
  }

  uint64_t s_pos = context->seek_pos;
  uint64_t s_dts = context->seek_pos_dts;
  uint64_t pts = output_wrap->pts_from_frame;

  if (!context->start_pts_set) {
    context->start_pts = pts;
    context->start_pts_set = true;
  }

  context->frameno = floor(
      (pts - context->start_pts) * context->pts_time_base * context->frame_rate + 0.5);

  /* decoding starts *always* on I-Frames,
   * so: P-Frames won't work, even if all the
   * information is in place, when we seek
   * to the I-Frame presented *after* the P-Frame,
   * but located before the P-Frame within
   * the stream */

  if (pts < context->seek_pos_pts) {
    s_pos = context->last_seek_pos;
    s_dts = context->last_seek_pos_dts;
  }

  for (int i = 0; i < context->num_indexers; i++) {
    if (context->tcs_in_use & tc_types[i]) {
      int tc_frameno = context->frameno;

      if (tc_types[i] == IMB_TC_RECORD_RUN_NO_GAPS) {
        tc_frameno = context->frameno_gapless;
      }

      IMB_index_builder_proc_frame(context->indexer[i],
                                   source_packet->data,
                                   source_packet->size,
                                   tc_frameno,
                                   s_pos,
                                   s_dts,
                                   pts);
    }
  }
  context->frameno_gapless++;
  av_packet_free(&source_wrap->input_packet);
}

static void *index_ffmpeg_read_packets(void *job_data)
{
  FFmpegIndexBuilderContext *context = job_data;
  struct input_ctx *input_ctx = context->input_ctx;

  /* Needed for TC building. */
  context->frame_rate = av_q2d(
      av_guess_frame_rate(context->input_ctx->format_context, context->input_ctx->stream, NULL));
  context->pts_time_base = av_q2d(context->input_ctx->stream->time_base);

  int gop_chunk_index = -1; /* First I-frame will increase this to 0. */
  int ret = 0;
  int frame_index = 0;

  while (ret >= 0) {
    index_ffmpeg_read_suspend(context, gop_chunk_index);
    AVPacket *av_packet = av_packet_alloc();
    ret = av_read_frame(input_ctx->format_context, av_packet);

    if (*context->stop || ret < 0) {
      break;
    }

    /* Only process packets from chosen video stream. */
    if (av_packet->stream_index != input_ctx->video_stream) {
      continue;
    }

    /* Packets are separated into segments separated by I-frames, because decoding must start on
     * I-frame. */
    if (av_packet->flags & AV_PKT_FLAG_KEY) {
      gop_chunk_index++;
    }

    create_source_packet_wrap(context, av_packet, gop_chunk_index, frame_index);
    create_output_packet_wrap(context, av_packet, gop_chunk_index, frame_index);

    frame_index++;
    index_ffmpeg_transcode_resume(context);
  }

  printf("read %d packets\n", frame_index - 1);

  context->all_packets_read = true;
  index_ffmpeg_transcode_resume(context);
  BLI_condition_notify_one(&context->builder_suspend_cond);
  return 0;
}

static bool index_ffmpeg_decode_packet(struct proxy_transcode_ctx *transcode_ctx,
                                       AVPacket *av_packet,
                                       AVFrame *decoded_frame)
{
  int frame_finished = 0;
  avcodec_decode_video2(
      transcode_ctx->input_codec_context, decoded_frame, &frame_finished, av_packet);
  return frame_finished != 0;
}

static void index_ffmpeg_scale_frame(TranscodeJob *transcode_job,
                                     AVFrame *decoded_frame,
                                     AVFrame **r_scaled_frame)
{
  FFmpegIndexBuilderContext *context = transcode_job->context;
  int thread_number = transcode_job->thread_number;
  struct proxy_transcode_ctx *transcode_ctx =
      transcode_job->context->transcode_context_array[thread_number];

  for (int size = 0; size < context->num_proxy_sizes; size++) {
    struct transcode_output_ctx *output_ctx = transcode_ctx->output_context[size];
    if (output_ctx == NULL) {
      continue;
    }

    struct SwsContext *sws_ctx = output_ctx->sws_ctx;
    if (sws_ctx && decoded_frame &&
        (decoded_frame->data[0] || decoded_frame->data[1] || decoded_frame->data[2] ||
         decoded_frame->data[3])) {
      sws_scale(sws_ctx,
                (const uint8_t *const *)decoded_frame->data,
                decoded_frame->linesize,
                0,
                output_ctx->orig_height,
                r_scaled_frame[size]->data,
                r_scaled_frame[size]->linesize);
    }
  }
}

static void index_ffmpeg_encode_frame(TranscodeJob *transcode_job,
                                      output_packet_wrap *packet_wrap,
                                      AVFrame **scaled_frame)
{
  int thread_number = transcode_job->thread_number;
  FFmpegIndexBuilderContext *context = transcode_job->context;
  struct proxy_transcode_ctx *transcode_ctx = (struct proxy_transcode_ctx *)
                                                  context->transcode_context_array[thread_number];

  for (int size = 0; size < IMB_PROXY_MAX_SLOT; size++) {
    struct transcode_output_ctx *output_ctx = transcode_ctx->output_context[size];

    if (output_ctx == NULL) {
      continue;
    }

    AVPacket *packet = av_packet_alloc();

    AVFrame *frame = scaled_frame[size];
    int got_output;
    int ret = avcodec_encode_video2(output_ctx->codec_context, packet, frame, &got_output);
    if (ret < 0) {
      fprintf(stderr,
              "Error encoding proxy frame %d for '%s'\n",
              packet_wrap->frame_index,
              context->proxy_ctx[size]->output_format->filename);
      *context->stop = 1;
      return;
    }

    if (got_output) {
      if (packet->pts != AV_NOPTS_VALUE) {
        packet->pts = av_rescale_q(packet->pts,
                                   output_ctx->codec_context->time_base,
                                   context->proxy_ctx[size]->stream->time_base);
      }
      if (packet->dts != AV_NOPTS_VALUE) {
        packet->dts = av_rescale_q(packet->dts,
                                   output_ctx->codec_context->time_base,
                                   context->proxy_ctx[size]->stream->time_base);
      }
      packet->stream_index = context->proxy_ctx[size]->stream->index;

      packet_wrap->output_packet[size] = packet;
    }
  }
}

static void index_ffmpeg_transcode_init_temporary_data(struct proxy_transcode_ctx *transcode_ctx,
                                                       AVFrame **r_scaled_frame)
{
  for (int size = 0; size < IMB_PROXY_MAX_SLOT; size++) {
    struct transcode_output_ctx *output_ctx = transcode_ctx->output_context[size];
    if (output_ctx == NULL) {
      continue;
    }

    r_scaled_frame[size] = av_frame_alloc();
    avpicture_fill((AVPicture *)r_scaled_frame[size],
                   MEM_mallocN(avpicture_get_size(output_ctx->codec_context->pix_fmt,
                                                  round_up(output_ctx->codec_context->width, 16),
                                                  output_ctx->codec_context->height),
                               "alloc proxy output frame"),
                   output_ctx->codec_context->pix_fmt,
                   round_up(output_ctx->codec_context->width, 16),
                   output_ctx->codec_context->height);
  }
}

static void index_ffmpeg_transcode_free_temporary_data(struct proxy_transcode_ctx *transcode_ctx,
                                                       AVFrame *decoded_frame,
                                                       AVFrame **scaled_frame)
{
  av_frame_free(&decoded_frame);
  for (int size = 0; size < IMB_PROXY_MAX_SLOT; size++) {
    struct transcode_output_ctx *output_ctx = transcode_ctx->output_context[size];
    if (output_ctx == NULL) {
      continue;
    }

    MEM_freeN(scaled_frame[size]->data[0]);
    av_frame_free(&scaled_frame[size]);
  }
}

static void *index_ffmpeg_transcode_packets(void *job_data)
{
  TranscodeJob *transcode_job = job_data;
  FFmpegIndexBuilderContext *context = transcode_job->context;
  int threads_total = context->num_transcode_threads;
  int thread_number = transcode_job->thread_number;
  struct proxy_transcode_ctx *transcode_ctx =
      transcode_job->context->transcode_context_array[thread_number];

  /* Prepare temporary data */
  AVFrame *decoded_frame = av_frame_alloc();
  AVFrame *scaled_frame[IMB_PROXY_MAX_SLOT];
  index_ffmpeg_transcode_init_temporary_data(transcode_ctx, scaled_frame);

  /* Some codecs do not output frames immediately (H264). These codecs require flushing. */
  bool needs_flushing = context->input_ctx->codec->capabilities & AV_CODEC_CAP_DELAY;
  int frame_index = 0;
  int output_packet_frame_index = 0;
  int gop_chunk_jump_length = 0;
  source_packet_wrap *source_packet;
  output_packet_wrap *output_packet = NULL;
  do {
    /* Ensure, that packet is read before accessing it. */
    index_ffmpeg_transcode_wait_for_packet(transcode_job, frame_index);
    source_packet = get_source_packet_wrap(context, frame_index);

    if (*context->stop || source_packet == NULL) {
      break;
    }

    frame_index++;

    /* Each thread works on own segment of packets. Jump GOP's until we find next one that we can
     * work on. */
    if ((source_packet->gop_chunk_index % threads_total) != thread_number) {
      gop_chunk_jump_length++;
      continue;
    }

    output_packet = get_output_packet_wrap(context, output_packet_frame_index);

    /* Increment output_packet_frame_index after jumping GOP chunks. This way last jump can be
     * ignored so codec can be flushed. */
    if ((output_packet->gop_chunk_index % threads_total) != thread_number &&
        gop_chunk_jump_length > 0) {
      output_packet_frame_index += gop_chunk_jump_length;
      gop_chunk_jump_length = 0;
      output_packet = get_output_packet_wrap(context, output_packet_frame_index);
    }

    if (index_ffmpeg_decode_packet(transcode_ctx, source_packet->input_packet, decoded_frame)) {
      index_ffmpeg_scale_frame(transcode_job, decoded_frame, scaled_frame);

      if (!output_packet) {
        BLI_assert(!"Missing output packet, this shouldn't happen");
        break;
        return 0;
      }

      index_ffmpeg_encode_frame(transcode_job, output_packet, scaled_frame);

      /* pts_from_frame is needed for TC index builder. */
      output_packet->pts_from_frame = av_get_pts_from_frame(context->input_ctx->format_context,
                                                            decoded_frame);
      output_packet->is_transcoded = true;
      output_packet_frame_index++;
      index_ffmpeg_write_resume(context);
    }
  } while (source_packet);

  /* Flush decoder. */
  if (output_packet && needs_flushing) {
    output_packet = get_output_packet_wrap(context, output_packet->frame_index + 1);
    AVPacket *flushing_packet = av_packet_alloc();
    while (output_packet &&
           index_ffmpeg_decode_packet(transcode_ctx, flushing_packet, decoded_frame)) {
      index_ffmpeg_scale_frame(transcode_job, decoded_frame, scaled_frame);
      index_ffmpeg_encode_frame(transcode_job, output_packet, scaled_frame);

      /* pts_from_frame is needed for TC index builder. */
      output_packet->pts_from_frame = av_get_pts_from_frame(context->input_ctx->format_context,
                                                            decoded_frame);
      output_packet->is_transcoded = true;
      output_packet = get_output_packet_wrap(context, output_packet->frame_index + 1);
      index_ffmpeg_write_resume(context);
    }
    av_packet_free(&flushing_packet);
  }

  index_ffmpeg_transcode_free_temporary_data(transcode_ctx, decoded_frame, scaled_frame);
  context->transcode_jobs_done++;
  index_ffmpeg_write_resume(context);
  BLI_condition_notify_one(&context->builder_suspend_cond);
  return 0;
}

static void *index_ffmpeg_write_frames(void *job_data)
{
  FFmpegIndexBuilderContext *context = job_data;

  int frame_index = 0;
  output_packet_wrap *output_packet;
  while (output_packet = get_decoded_output_packet_wrap(context, frame_index)) {

    if (*context->stop) {
      break;
    }

    for (int size = 0; size < context->num_proxy_sizes; size++) {
      if (output_packet->output_packet[size]) {
        AVPacket *packet = output_packet->output_packet[size];

        if (av_interleaved_write_frame(context->proxy_ctx[size]->output_format, packet) != 0) {
          fprintf(stderr,
                  "Error writing proxy frame %d "
                  "into '%s'\n",
                  context->proxy_ctx[size]->cfra - 1,
                  context->proxy_ctx[size]->output_format->filename);
          return 0;
        }

        av_packet_free(&packet);
      }
    }

    uint64_t stream_size = avio_size(context->input_ctx->format_context->pb);
    float next_progress = (float)((int)floor(
                              ((double)output_packet->pos) * 100 / ((double)stream_size) + 0.5)) /
                          100;

    if (*context->progress != next_progress) {
      *context->progress = next_progress;
      *context->do_update = true;
    }

    context->last_gop_chunk_written = output_packet->gop_chunk_index;
    build_timecode_index(context, frame_index);

    frame_index++;
    index_ffmpeg_read_resume(context);
  }

  context->all_packets_written = true;
  BLI_condition_notify_one(&context->builder_suspend_cond);
  return 0;
}

static TranscodeJob **index_rebuild_ffmpeg_init_jobs(FFmpegIndexBuilderContext *context,
                                                     ListBase *reader_thread,
                                                     ListBase *transcoder_thread,
                                                     ListBase *encoder_thread)
{
  BLI_threadpool_init(reader_thread, index_ffmpeg_read_packets, 1);
  BLI_threadpool_init(
      transcoder_thread, index_ffmpeg_transcode_packets, context->num_transcode_threads);
  BLI_threadpool_init(encoder_thread, index_ffmpeg_write_frames, 1);

  TranscodeJob **transcode_job_array = MEM_callocN(
      sizeof(TranscodeJob *) * context->num_transcode_threads, "transcode job array");

  for (int i = 0; i < context->num_transcode_threads; i++) {
    transcode_job_array[i] = MEM_callocN(sizeof(TranscodeJob), "transcode job");
    transcode_job_array[i]->context = context;
    transcode_job_array[i]->thread_number = i;
  }

  return transcode_job_array;
}

static void index_rebuild_ffmpeg_free_jobs(FFmpegIndexBuilderContext *context,
                                           ListBase *reader_thread,
                                           ListBase *transcoder_thread,
                                           ListBase *writer_thread,
                                           TranscodeJob **transcode_job_array)
{
  for (int i = 0; i < context->num_transcode_threads; i++) {
    MEM_freeN(transcode_job_array[i]);
  }
  MEM_freeN(transcode_job_array);
  BLI_threadpool_remove(reader_thread, context);
  BLI_threadpool_end(reader_thread);
  BLI_threadpool_remove(transcoder_thread, context);
  BLI_threadpool_end(transcoder_thread);
  BLI_threadpool_remove(writer_thread, context);
  BLI_threadpool_end(writer_thread);
}

static int index_rebuild_ffmpeg(FFmpegIndexBuilderContext *context,
                                short *stop,
                                short *do_update,
                                float *progress)
{
  index_ffmpeg_create_transcode_context(context, stop, do_update, progress);

  ListBase reader_thread;
  ListBase transcoder_thread;
  ListBase writer_thread;
  TranscodeJob **transcode_job_array = index_rebuild_ffmpeg_init_jobs(
      context, &reader_thread, &transcoder_thread, &writer_thread);

  /* Read packets. */
  BLI_threadpool_insert(&reader_thread, context);

  /* Transcode. Job runs in multiple threads working on chunks separated by I-frames. */
  for (int i = 0; i < context->num_transcode_threads; i++) {
    BLI_threadpool_insert(&transcoder_thread, transcode_job_array[i]);
  }

  /* Write frames. */
  BLI_threadpool_insert(&writer_thread, context);

  /* Wait until all jobs are done. */
  BLI_mutex_lock(&context->builder_suspend_mutex);
  while (!context->all_packets_read ||
         context->transcode_jobs_done < context->num_transcode_threads ||
         !context->all_packets_written) {
    BLI_condition_wait(&context->builder_suspend_cond, &context->builder_suspend_mutex);
  }
  BLI_mutex_unlock(&context->builder_suspend_mutex);

  /* Free jobs. */
  index_rebuild_ffmpeg_free_jobs(
      context, &reader_thread, &transcoder_thread, &writer_thread, transcode_job_array);

  index_ffmpeg_free_transcode_context(context);
  return 1;
}

#endif

/* ----------------------------------------------------------------------
 * - internal AVI (fallback) rebuilder
 * ---------------------------------------------------------------------- */

#ifdef WITH_AVI
typedef struct FallbackIndexBuilderContext {
  int anim_type;

  struct anim *anim;
  AviMovie *proxy_ctx[IMB_PROXY_MAX_SLOT];
  IMB_Proxy_Size proxy_sizes_in_use;
} FallbackIndexBuilderContext;

static AviMovie *alloc_proxy_output_avi(
    struct anim *anim, char *filename, int width, int height, int quality)
{
  int x, y;
  AviFormat format;
  double framerate;
  AviMovie *avi;
  /* it doesn't really matter for proxies, but sane defaults help anyways...*/
  short frs_sec = 25;
  float frs_sec_base = 1.0;

  IMB_anim_get_fps(anim, &frs_sec, &frs_sec_base, false);

  x = width;
  y = height;

  framerate = (double)frs_sec / (double)frs_sec_base;

  avi = MEM_mallocN(sizeof(AviMovie), "avimovie");

  format = AVI_FORMAT_MJPEG;

  if (AVI_open_compress(filename, avi, 1, format) != AVI_ERROR_NONE) {
    MEM_freeN(avi);
    return NULL;
  }

  AVI_set_compress_option(avi, AVI_OPTION_TYPE_MAIN, 0, AVI_OPTION_WIDTH, &x);
  AVI_set_compress_option(avi, AVI_OPTION_TYPE_MAIN, 0, AVI_OPTION_HEIGHT, &y);
  AVI_set_compress_option(avi, AVI_OPTION_TYPE_MAIN, 0, AVI_OPTION_QUALITY, &quality);
  AVI_set_compress_option(avi, AVI_OPTION_TYPE_MAIN, 0, AVI_OPTION_FRAMERATE, &framerate);

  avi->interlace = 0;
  avi->odd_fields = 0;

  return avi;
}

static IndexBuildContext *index_fallback_create_context(struct anim *anim,
                                                        IMB_Timecode_Type UNUSED(tcs_in_use),
                                                        IMB_Proxy_Size proxy_sizes_in_use,
                                                        int quality)
{
  FallbackIndexBuilderContext *context;
  int i;

  /* since timecode indices only work with ffmpeg right now,
   * don't know a sensible fallback here...
   *
   * so no proxies...
   */
  if (proxy_sizes_in_use == IMB_PROXY_NONE) {
    return NULL;
  }

  context = MEM_callocN(sizeof(FallbackIndexBuilderContext), "fallback index builder context");

  context->anim = anim;
  context->proxy_sizes_in_use = proxy_sizes_in_use;

  memset(context->proxy_ctx, 0, sizeof(context->proxy_ctx));

  for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
    if (context->proxy_sizes_in_use & proxy_sizes[i]) {
      char fname[FILE_MAX];

      get_proxy_filename(anim, proxy_sizes[i], fname, true);
      BLI_make_existing_file(fname);

      context->proxy_ctx[i] = alloc_proxy_output_avi(
          anim, fname, anim->x * proxy_fac[i], anim->y * proxy_fac[i], quality);
    }
  }

  return (IndexBuildContext *)context;
}

static void index_rebuild_fallback_finish(FallbackIndexBuilderContext *context, int stop)
{
  struct anim *anim = context->anim;
  char fname[FILE_MAX];
  char fname_tmp[FILE_MAX];
  int i;

  for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
    if (context->proxy_sizes_in_use & proxy_sizes[i]) {
      AVI_close_compress(context->proxy_ctx[i]);
      MEM_freeN(context->proxy_ctx[i]);

      get_proxy_filename(anim, proxy_sizes[i], fname_tmp, true);
      get_proxy_filename(anim, proxy_sizes[i], fname, false);

      if (stop) {
        unlink(fname_tmp);
      }
      else {
        unlink(fname);
        rename(fname_tmp, fname);
      }
    }
  }
}

static void index_rebuild_fallback(FallbackIndexBuilderContext *context,
                                   const short *stop,
                                   short *do_update,
                                   float *progress)
{
  int cnt = IMB_anim_get_duration(context->anim, IMB_TC_NONE);
  int i, pos;
  struct anim *anim = context->anim;

  for (pos = 0; pos < cnt; pos++) {
    struct ImBuf *ibuf = IMB_anim_absolute(anim, pos, IMB_TC_NONE, IMB_PROXY_NONE);
    struct ImBuf *tmp_ibuf = IMB_dupImBuf(ibuf);
    float next_progress = (float)pos / (float)cnt;

    if (*progress != next_progress) {
      *progress = next_progress;
      *do_update = true;
    }

    if (*stop) {
      break;
    }

    IMB_flipy(tmp_ibuf);

    for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
      if (context->proxy_sizes_in_use & proxy_sizes[i]) {
        int x = anim->x * proxy_fac[i];
        int y = anim->y * proxy_fac[i];

        struct ImBuf *s_ibuf = IMB_dupImBuf(tmp_ibuf);

        IMB_scalefastImBuf(s_ibuf, x, y);

        IMB_convert_rgba_to_abgr(s_ibuf);

        AVI_write_frame(context->proxy_ctx[i], pos, AVI_FORMAT_RGB32, s_ibuf->rect, x * y * 4);

        /* note that libavi free's the buffer... */
        s_ibuf->rect = NULL;

        IMB_freeImBuf(s_ibuf);
      }
    }

    IMB_freeImBuf(tmp_ibuf);
    IMB_freeImBuf(ibuf);
  }
}

#endif /* WITH_AVI */

/* ----------------------------------------------------------------------
 * - public API
 * ---------------------------------------------------------------------- */

IndexBuildContext *IMB_anim_index_rebuild_context(struct anim *anim,
                                                  IMB_Timecode_Type tcs_in_use,
                                                  IMB_Proxy_Size proxy_sizes_in_use,
                                                  int quality,
                                                  const bool overwrite,
                                                  GSet *file_list)
{
  IndexBuildContext *context = NULL;
  IMB_Proxy_Size proxy_sizes_to_build = proxy_sizes_in_use;
  int i;

  /* Don't generate the same file twice! */
  if (file_list) {
    for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
      IMB_Proxy_Size proxy_size = proxy_sizes[i];
      if (proxy_size & proxy_sizes_to_build) {
        char filename[FILE_MAX];
        if (get_proxy_filename(anim, proxy_size, filename, false) == false) {
          return NULL;
        }
        void **filename_key_p;
        if (!BLI_gset_ensure_p_ex(file_list, filename, &filename_key_p)) {
          *filename_key_p = BLI_strdup(filename);
        }
        else {
          proxy_sizes_to_build &= ~proxy_size;
          printf("Proxy: %s already registered for generation, skipping\n", filename);
        }
      }
    }
  }

  if (!overwrite) {
    IMB_Proxy_Size built_proxies = IMB_anim_proxy_get_existing(anim);
    if (built_proxies != 0) {

      for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
        IMB_Proxy_Size proxy_size = proxy_sizes[i];
        if (proxy_size & built_proxies) {
          char filename[FILE_MAX];
          if (get_proxy_filename(anim, proxy_size, filename, false) == false) {
            return NULL;
          }
          printf("Skipping proxy: %s\n", filename);
        }
      }
    }
    proxy_sizes_to_build &= ~built_proxies;
  }

  fflush(stdout);

  if (proxy_sizes_to_build == 0) {
    return NULL;
  }

  switch (anim->curtype) {
#ifdef WITH_FFMPEG
    case ANIM_FFMPEG:
      context = index_ffmpeg_create_context(anim, tcs_in_use, proxy_sizes_to_build, quality);
      break;
#endif
#ifdef WITH_AVI
    default:
      context = index_fallback_create_context(anim, tcs_in_use, proxy_sizes_to_build, quality);
      break;
#endif
  }

  if (context) {
    context->anim_type = anim->curtype;
  }

  return context;

  UNUSED_VARS(tcs_in_use, proxy_sizes_in_use, quality);
}

void IMB_anim_index_rebuild(struct IndexBuildContext *context,
                            /* NOLINTNEXTLINE: readability-non-const-parameter. */
                            short *stop,
                            /* NOLINTNEXTLINE: readability-non-const-parameter. */
                            short *do_update,
                            /* NOLINTNEXTLINE: readability-non-const-parameter. */
                            float *progress)
{
  switch (context->anim_type) {
#ifdef WITH_FFMPEG
    case ANIM_FFMPEG:
      index_rebuild_ffmpeg((FFmpegIndexBuilderContext *)context, stop, do_update, progress);
      break;
#endif
#ifdef WITH_AVI
    default:
      index_rebuild_fallback((FallbackIndexBuilderContext *)context, stop, do_update, progress);
      break;
#endif
  }

  UNUSED_VARS(stop, do_update, progress);
}

void IMB_anim_index_rebuild_finish(IndexBuildContext *context, short stop)
{
  switch (context->anim_type) {
#ifdef WITH_FFMPEG
    case ANIM_FFMPEG:
      index_ffmpeg_free_context((FFmpegIndexBuilderContext *)context, stop);
      break;
#endif
#ifdef WITH_AVI
    default:
      index_rebuild_fallback_finish((FallbackIndexBuilderContext *)context, stop);
      break;
#endif
  }

  /* static defined at top of the file */
  UNUSED_VARS(stop, proxy_sizes);
}

void IMB_free_indices(struct anim *anim)
{
  int i;

  for (i = 0; i < IMB_PROXY_MAX_SLOT; i++) {
    if (anim->proxy_anim[i]) {
      IMB_close_anim(anim->proxy_anim[i]);
      anim->proxy_anim[i] = NULL;
    }
  }

  for (i = 0; i < IMB_TC_MAX_SLOT; i++) {
    if (anim->curr_idx[i]) {
      IMB_indexer_close(anim->curr_idx[i]);
      anim->curr_idx[i] = NULL;
    }
  }

  anim->proxies_tried = 0;
  anim->indices_tried = 0;
}

void IMB_anim_set_index_dir(struct anim *anim, const char *dir)
{
  if (STREQ(anim->index_dir, dir)) {
    return;
  }
  BLI_strncpy(anim->index_dir, dir, sizeof(anim->index_dir));

  IMB_free_indices(anim);
}

struct anim *IMB_anim_open_proxy(struct anim *anim, IMB_Proxy_Size preview_size)
{
  char fname[FILE_MAX];
  int i = IMB_proxy_size_to_array_index(preview_size);

  if (anim->proxy_anim[i]) {
    return anim->proxy_anim[i];
  }

  if (anim->proxies_tried & preview_size) {
    return NULL;
  }

  get_proxy_filename(anim, preview_size, fname, false);

  /* proxies are generated in the same color space as animation itself */
  anim->proxy_anim[i] = IMB_open_anim(fname, 0, 0, anim->colorspace);

  anim->proxies_tried |= preview_size;

  return anim->proxy_anim[i];
}

struct anim_index *IMB_anim_open_index(struct anim *anim, IMB_Timecode_Type tc)
{
  char fname[FILE_MAX];
  int i = IMB_timecode_to_array_index(tc);

  if (anim->curr_idx[i]) {
    return anim->curr_idx[i];
  }

  if (anim->indices_tried & tc) {
    return NULL;
  }

  get_tc_filename(anim, tc, fname);

  anim->curr_idx[i] = IMB_indexer_open(fname);

  anim->indices_tried |= tc;

  return anim->curr_idx[i];
}

int IMB_anim_index_get_frame_index(struct anim *anim, IMB_Timecode_Type tc, int position)
{
  struct anim_index *idx = IMB_anim_open_index(anim, tc);

  if (!idx) {
    return position;
  }

  return IMB_indexer_get_frame_index(idx, position);
}

IMB_Proxy_Size IMB_anim_proxy_get_existing(struct anim *anim)
{
  const int num_proxy_sizes = IMB_PROXY_MAX_SLOT;
  IMB_Proxy_Size existing = 0;
  int i;
  for (i = 0; i < num_proxy_sizes; i++) {
    IMB_Proxy_Size proxy_size = proxy_sizes[i];
    char filename[FILE_MAX];
    get_proxy_filename(anim, proxy_size, filename, false);
    if (BLI_exists(filename)) {
      existing |= proxy_size;
    }
  }
  return existing;
}
