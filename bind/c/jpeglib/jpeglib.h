/*
 * jpeglib.h - the classic JPEG library interface, implemented by vio.
 *
 * Source- and binary-compatible: the structures below have the standard
 * layout for JPEG_LIB_VERSION (see jconfig.h), so programs written for (or
 * compiled against) this interface work unchanged.  Includes the extensions
 * for extended RGB colorspaces, RGB565 output, 12- and 16-bit samples,
 * lossless JPEG, partial decompression (jpeg_skip_scanlines(),
 * jpeg_crop_scanline()), ICC profiles and in-memory sources/destinations.
 */

#ifndef JPEGLIB_H
#define JPEGLIB_H

#ifndef JCONFIG_INCLUDED
#include "jconfig.h"
#endif
#include "jmorecfg.h"

#ifdef __cplusplus
#ifndef DONT_USE_EXTERN_C
extern "C" {
#endif
#endif


/* Constants of the JPEG standard and limits of this implementation. */

#define DCTSIZE             8   /* basic DCT block size */
#define DCTSIZE2            64  /* DCTSIZE squared */
#define NUM_QUANT_TBLS      4   /* quantization tables */
#define NUM_HUFF_TBLS       4   /* Huffman tables of each class */
#define NUM_ARITH_TBLS      16  /* arithmetic-coding tables */
#define MAX_COMPS_IN_SCAN   4   /* components per scan */
#define MAX_SAMP_FACTOR     4   /* sampling factor */
#define C_MAX_BLOCKS_IN_MCU   10  /* compressor: blocks per MCU */
#ifndef D_MAX_BLOCKS_IN_MCU
#define D_MAX_BLOCKS_IN_MCU   10  /* decompressor: blocks per MCU */
#endif


/* Sample arrays: a row is a pointer to samples, an array is a list of rows
   and an image is a list of arrays (one per component). */

typedef JSAMPLE *JSAMPROW;
typedef JSAMPROW *JSAMPARRAY;
typedef JSAMPARRAY *JSAMPIMAGE;

typedef J12SAMPLE *J12SAMPROW;
typedef J12SAMPROW *J12SAMPARRAY;
typedef J12SAMPARRAY *J12SAMPIMAGE;

typedef J16SAMPLE *J16SAMPROW;
typedef J16SAMPROW *J16SAMPARRAY;
typedef J16SAMPARRAY *J16SAMPIMAGE;

typedef JCOEF JBLOCK[DCTSIZE2];  /* one block of coefficients */
typedef JBLOCK *JBLOCKROW;
typedef JBLOCKROW *JBLOCKARRAY;
typedef JBLOCKARRAY *JBLOCKIMAGE;

typedef JCOEF *JCOEFPTR;


/* Quantization table: 64 values in natural (not zigzag) order. */
typedef struct {
  UINT16 quantval[DCTSIZE2];
  boolean sent_table;           /* TRUE once written (compression) */
} JQUANT_TBL;

/* Huffman table definition, as in a DHT marker. */
typedef struct {
  UINT8 bits[17];               /* bits[k] = number of codes of length k */
  UINT8 huffval[256];           /* symbols in order of increasing length */
  boolean sent_table;           /* TRUE once written (compression) */
} JHUFF_TBL;


/* Per-component information. */
typedef struct {
  /* Set by the application (compression) or read from SOF (decompression) */
  int component_id;
  int component_index;          /* index in comp_info[] */
  int h_samp_factor;
  int v_samp_factor;
  int quant_tbl_no;
  /* Set from SOS (decompression) or by the application (compression) */
  int dc_tbl_no;
  int ac_tbl_no;

  /* Computed by the library */
  JDIMENSION width_in_blocks;
  JDIMENSION height_in_blocks;
#if JPEG_LIB_VERSION >= 70
  int DCT_h_scaled_size;
  int DCT_v_scaled_size;
#else
  int DCT_scaled_size;          /* scaled block size (decompression) */
#endif
  JDIMENSION downsampled_width;  /* actual width in samples */
  JDIMENSION downsampled_height; /* actual height in samples */
  boolean component_needed;     /* FALSE if the component is not decoded */

  /* Valid during one scan */
  int MCU_width;
  int MCU_height;
  int MCU_blocks;
  int MCU_sample_width;
  int last_col_width;
  int last_row_height;

  /* Quantization table latched at the start of the component's first scan */
  JQUANT_TBL *quant_table;

  void *dct_table;              /* private */
} jpeg_component_info;


/* One scan of a multi-scan (progressive or custom) compression script. */
typedef struct {
  int comps_in_scan;
  int component_index[MAX_COMPS_IN_SCAN];
  int Ss, Se;                   /* spectral selection (Ss: predictor in
                                   lossless mode) */
  int Ah, Al;                   /* successive approximation (Al: point
                                   transform in lossless mode) */
} jpeg_scan_info;


/* A saved APPn or COM marker (see jpeg_save_markers()). */
typedef struct jpeg_marker_struct *jpeg_saved_marker_ptr;

struct jpeg_marker_struct {
  jpeg_saved_marker_ptr next;
  UINT8 marker;                 /* marker code: JPEG_COM or JPEG_APP0 + n */
  unsigned int original_length; /* length of the marker's data */
  unsigned int data_length;     /* number of bytes saved */
  JOCTET *data;
};


/* Colorspaces. */

#define JCS_EXTENSIONS  1
#define JCS_ALPHA_EXTENSIONS  1

typedef enum {
  JCS_UNKNOWN,                  /* unspecified */
  JCS_GRAYSCALE,
  JCS_RGB,
  JCS_YCbCr,
  JCS_CMYK,
  JCS_YCCK,
  JCS_EXT_RGB,                  /* red/green/blue */
  JCS_EXT_RGBX,                 /* red/green/blue/x */
  JCS_EXT_BGR,                  /* blue/green/red */
  JCS_EXT_BGRX,                 /* blue/green/red/x */
  JCS_EXT_XBGR,                 /* x/blue/green/red */
  JCS_EXT_XRGB,                 /* x/red/green/blue */
  /* As above, but the 4th byte is set to 0xFF when decompressing */
  JCS_EXT_RGBA,
  JCS_EXT_BGRA,
  JCS_EXT_ABGR,
  JCS_EXT_ARGB,
  JCS_RGB565                    /* 5-6-5 packed RGB [decompression only] */
} J_COLOR_SPACE;

/* DCT/IDCT algorithms. */
typedef enum {
  JDCT_ISLOW,                   /* accurate integer */
  JDCT_IFAST,                   /* less accurate integer */
  JDCT_FLOAT                    /* floating point */
} J_DCT_METHOD;

#ifndef JDCT_DEFAULT
#define JDCT_DEFAULT  JDCT_ISLOW
#endif
#ifndef JDCT_FASTEST
#define JDCT_FASTEST  JDCT_IFAST
#endif

/* Dithering for color quantization. */
typedef enum {
  JDITHER_NONE,
  JDITHER_ORDERED,
  JDITHER_FS                    /* Floyd-Steinberg */
} J_DITHER_MODE;


/* Fields shared by compression and decompression objects. */
#define jpeg_common_fields \
  struct jpeg_error_mgr *err;   /* error handler */ \
  struct jpeg_memory_mgr *mem;  /* memory manager */ \
  struct jpeg_progress_mgr *progress; /* progress monitor, or NULL */ \
  void *client_data;            /* for the application's use */ \
  boolean is_decompressor; \
  int global_state              /* library state */

struct jpeg_common_struct {
  jpeg_common_fields;
};

typedef struct jpeg_common_struct *j_common_ptr;
typedef struct jpeg_compress_struct *j_compress_ptr;
typedef struct jpeg_decompress_struct *j_decompress_ptr;


/* Compression object. */
struct jpeg_compress_struct {
  jpeg_common_fields;

  struct jpeg_destination_mgr *dest;

  /* Source image description, set by the application */
  JDIMENSION image_width;
  JDIMENSION image_height;
  int input_components;         /* samples per pixel */
  J_COLOR_SPACE in_color_space;

  double input_gamma;           /* unused */

  /* Compression parameters: jpeg_set_defaults() initializes them; the
     application may change them before jpeg_start_compress(). */

#if JPEG_LIB_VERSION >= 70
  unsigned int scale_num, scale_denom; /* must be 1/1 */

  JDIMENSION jpeg_width;        /* computed by jpeg_calc_jpeg_dimensions() */
  JDIMENSION jpeg_height;
#endif

  int data_precision;           /* bits per sample */

  int num_components;           /* components in the JPEG image */
  J_COLOR_SPACE jpeg_color_space;

  jpeg_component_info *comp_info;

  JQUANT_TBL *quant_tbl_ptrs[NUM_QUANT_TBLS];
#if JPEG_LIB_VERSION >= 70
  int q_scale_factor[NUM_QUANT_TBLS];
#endif

  JHUFF_TBL *dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL *ac_huff_tbl_ptrs[NUM_HUFF_TBLS];

  UINT8 arith_dc_L[NUM_ARITH_TBLS];
  UINT8 arith_dc_U[NUM_ARITH_TBLS];
  UINT8 arith_ac_K[NUM_ARITH_TBLS];

  int num_scans;                /* length of scan_info[] */
  const jpeg_scan_info *scan_info; /* scan script, or NULL */

  boolean raw_data_in;          /* TRUE: jpeg_write_raw_data() is used */
  boolean arith_code;           /* TRUE: arithmetic coding */
  boolean optimize_coding;      /* TRUE: optimal Huffman tables */
  boolean CCIR601_sampling;     /* not supported */
#if JPEG_LIB_VERSION >= 70
  boolean do_fancy_downsampling; /* unused */
#endif
  int smoothing_factor;         /* 1..100, or 0 for no input smoothing */
  J_DCT_METHOD dct_method;

  unsigned int restart_interval; /* MCUs per restart interval, or 0 */
  int restart_in_rows;          /* the same in MCU rows (if nonzero) */

  /* Special markers */
  boolean write_JFIF_header;
  UINT8 JFIF_major_version;
  UINT8 JFIF_minor_version;
  UINT8 density_unit;           /* 0: aspect ratio only, 1: dpi, 2: dpcm */
  UINT16 X_density;
  UINT16 Y_density;
  boolean write_Adobe_marker;

  /* State: next_scanline counts the rows written so far */
  JDIMENSION next_scanline;

  /* Internal state */
  boolean progressive_mode;
  int max_h_samp_factor;
  int max_v_samp_factor;

#if JPEG_LIB_VERSION >= 70
  int min_DCT_h_scaled_size;
  int min_DCT_v_scaled_size;
#endif

  JDIMENSION total_iMCU_rows;

  int comps_in_scan;
  jpeg_component_info *cur_comp_info[MAX_COMPS_IN_SCAN];

  JDIMENSION MCUs_per_row;
  JDIMENSION MCU_rows_in_scan;

  int blocks_in_MCU;
  int MCU_membership[C_MAX_BLOCKS_IN_MCU];

  int Ss, Se, Ah, Al;           /* parameters of the current scan */

#if JPEG_LIB_VERSION >= 80
  int block_size;
  const int *natural_order;
  int lim_Se;
#endif

  /* Private modules */
  struct jpeg_comp_master *master;
  struct jpeg_c_main_controller *main;
  struct jpeg_c_prep_controller *prep;
  struct jpeg_c_coef_controller *coef;
  struct jpeg_marker_writer *marker;
  struct jpeg_color_converter *cconvert;
  struct jpeg_downsampler *downsample;
  struct jpeg_forward_dct *fdct;
  struct jpeg_entropy_encoder *entropy;
  jpeg_scan_info *script_space; /* scan script of jpeg_simple_progression() */
  int script_space_size;
};


/* Decompression object. */
struct jpeg_decompress_struct {
  jpeg_common_fields;

  struct jpeg_source_mgr *src;

  /* Image description, set by jpeg_read_header() */
  JDIMENSION image_width;
  JDIMENSION image_height;
  int num_components;
  J_COLOR_SPACE jpeg_color_space;

  /* Decompression parameters: jpeg_read_header() sets the defaults; the
     application may change them before jpeg_start_decompress(). */

  J_COLOR_SPACE out_color_space;

  unsigned int scale_num, scale_denom; /* output scaling */

  double output_gamma;          /* unused */

  boolean buffered_image;       /* TRUE: buffered-image mode */
  boolean raw_data_out;         /* TRUE: jpeg_read_raw_data() is used */

  J_DCT_METHOD dct_method;
  boolean do_fancy_upsampling;  /* TRUE: smooth (triangle-filter)
                                   upsampling */
  boolean do_block_smoothing;   /* TRUE: smooth early progressive scans */

  boolean quantize_colors;      /* TRUE: colormapped output */
  J_DITHER_MODE dither_mode;
  boolean two_pass_quantize;    /* TRUE: two-pass color selection */
  int desired_number_of_colors; /* 8..256 */
  /* Quantization modes needed in buffered-image mode */
  boolean enable_1pass_quant;
  boolean enable_external_quant;
  boolean enable_2pass_quant;

  /* Output description, computed by jpeg_start_decompress() or
     jpeg_calc_output_dimensions() */

  JDIMENSION output_width;
  JDIMENSION output_height;
  int out_color_components;     /* color components in out_color_space */
  int output_components;        /* samples per output pixel (1 when
                                   quantizing colors) */
  int rec_outbuf_height;        /* recommended rows per jpeg_read_scanlines()
                                   call */

  /* Colormap when quantizing colors: colormap[component][index] (a
     J12SAMPARRAY for 12-bit data) */
  int actual_number_of_colors;
  JSAMPARRAY colormap;

  /* State: output_scanline counts the rows returned so far */
  JDIMENSION output_scanline;

  /* Progress of input and output, for buffered-image mode */
  int input_scan_number;
  JDIMENSION input_iMCU_row;
  int output_scan_number;
  JDIMENSION output_iMCU_row;

  /* Progressive JPEG: coef_bits[c][k] is the current successive-
     approximation bit position of coefficient k of component c (-1 before
     its first scan), or NULL */
  int (*coef_bits)[DCTSIZE2];

  /* Tables read from the datastream */
  JQUANT_TBL *quant_tbl_ptrs[NUM_QUANT_TBLS];

  JHUFF_TBL *dc_huff_tbl_ptrs[NUM_HUFF_TBLS];
  JHUFF_TBL *ac_huff_tbl_ptrs[NUM_HUFF_TBLS];

  /* From SOF and other markers */
  int data_precision;

  jpeg_component_info *comp_info;

#if JPEG_LIB_VERSION >= 80
  boolean is_baseline;
#endif
  boolean progressive_mode;
  boolean arith_code;

  UINT8 arith_dc_L[NUM_ARITH_TBLS];
  UINT8 arith_dc_U[NUM_ARITH_TBLS];
  UINT8 arith_ac_K[NUM_ARITH_TBLS];

  unsigned int restart_interval;

  boolean saw_JFIF_marker;
  UINT8 JFIF_major_version;
  UINT8 JFIF_minor_version;
  UINT8 density_unit;
  UINT16 X_density;
  UINT16 Y_density;
  boolean saw_Adobe_marker;
  UINT8 Adobe_transform;

  boolean CCIR601_sampling;

  /* Markers saved by jpeg_save_markers() */
  jpeg_saved_marker_ptr marker_list;

  /* Internal state */

  int max_h_samp_factor;
  int max_v_samp_factor;

#if JPEG_LIB_VERSION >= 70
  int min_DCT_h_scaled_size;
  int min_DCT_v_scaled_size;
#else
  int min_DCT_scaled_size;
#endif

  JDIMENSION total_iMCU_rows;

  JSAMPLE *sample_range_limit;  /* range-limiting table */

  int comps_in_scan;
  jpeg_component_info *cur_comp_info[MAX_COMPS_IN_SCAN];

  JDIMENSION MCUs_per_row;
  JDIMENSION MCU_rows_in_scan;

  int blocks_in_MCU;
  int MCU_membership[D_MAX_BLOCKS_IN_MCU];

  int Ss, Se, Ah, Al;           /* parameters of the current scan */

#if JPEG_LIB_VERSION >= 80
  int block_size;
  const int *natural_order;
  int lim_Se;
#endif

  int unread_marker;            /* marker being processed, or 0 */

  /* Private modules */
  struct jpeg_decomp_master *master;
  struct jpeg_d_main_controller *main;
  struct jpeg_d_coef_controller *coef;
  struct jpeg_d_post_controller *post;
  struct jpeg_input_controller *inputctl;
  struct jpeg_marker_reader *marker;
  struct jpeg_entropy_decoder *entropy;
  struct jpeg_inverse_dct *idct;
  struct jpeg_upsampler *upsample;
  struct jpeg_color_deconverter *cconvert;
  struct jpeg_color_quantizer *cquantize;
};


/* Error handler.  jpeg_std_error() fills in the standard methods; the
   application may override them (error_exit must not return). */
struct jpeg_error_mgr {
  void (*error_exit) (j_common_ptr cinfo);
  /* msg_level -1: warning, 0: important message, 1+: trace detail */
  void (*emit_message) (j_common_ptr cinfo, int msg_level);
  void (*output_message) (j_common_ptr cinfo);
  /* Formats the current message into buffer (at least JMSG_LENGTH_MAX) */
  void (*format_message) (j_common_ptr cinfo, char *buffer);
#define JMSG_LENGTH_MAX  200
  void (*reset_error_mgr) (j_common_ptr cinfo);

  /* The current message: code and parameters */
  int msg_code;
#define JMSG_STR_PARM_MAX  80
  union {
    int i[8];
    char s[JMSG_STR_PARM_MAX];
  } msg_parm;

  int trace_level;              /* trace messages up to this level shown */

  long num_warnings;            /* number of corrupt-data warnings */

  /* Message tables: the library's, and an optional application table for
     codes first_addon_message..last_addon_message */
  const char * const *jpeg_message_table;
  int last_jpeg_message;
  const char * const *addon_message_table;
  int first_addon_message;
  int last_addon_message;
};


/* Progress monitor, called periodically when set. */
struct jpeg_progress_mgr {
  void (*progress_monitor) (j_common_ptr cinfo);

  long pass_counter;            /* work units completed in this pass */
  long pass_limit;              /* total work units in this pass */
  int completed_passes;
  int total_passes;
};


/* Destination of compressed data. */
struct jpeg_destination_mgr {
  JOCTET *next_output_byte;
  size_t free_in_buffer;

  void (*init_destination) (j_compress_ptr cinfo);
  boolean (*empty_output_buffer) (j_compress_ptr cinfo);
  void (*term_destination) (j_compress_ptr cinfo);
};


/* Source of compressed data. */
struct jpeg_source_mgr {
  const JOCTET *next_input_byte;
  size_t bytes_in_buffer;

  void (*init_source) (j_decompress_ptr cinfo);
  boolean (*fill_input_buffer) (j_decompress_ptr cinfo);
  void (*skip_input_data) (j_decompress_ptr cinfo, long num_bytes);
  boolean (*resync_to_restart) (j_decompress_ptr cinfo, int desired);
  void (*term_source) (j_decompress_ptr cinfo);
};


/* Memory manager.  Memory is allocated in pools: JPOOL_IMAGE is released at
   the end of each image, JPOOL_PERMANENT when the object is destroyed.
   Sample arrays hold JSAMPLE, J12SAMPLE or J16SAMPLE elements according to
   data_precision. */

#define JPOOL_PERMANENT  0
#define JPOOL_IMAGE      1
#define JPOOL_NUMPOOLS   2

typedef struct jvirt_sarray_control *jvirt_sarray_ptr;
typedef struct jvirt_barray_control *jvirt_barray_ptr;

struct jpeg_memory_mgr {
  void *(*alloc_small) (j_common_ptr cinfo, int pool_id, size_t sizeofobject);
  void *(*alloc_large) (j_common_ptr cinfo, int pool_id,
                        size_t sizeofobject);
  JSAMPARRAY (*alloc_sarray) (j_common_ptr cinfo, int pool_id,
                              JDIMENSION samplesperrow, JDIMENSION numrows);
  JBLOCKARRAY (*alloc_barray) (j_common_ptr cinfo, int pool_id,
                               JDIMENSION blocksperrow, JDIMENSION numrows);
  jvirt_sarray_ptr (*request_virt_sarray) (j_common_ptr cinfo, int pool_id,
                                           boolean pre_zero,
                                           JDIMENSION samplesperrow,
                                           JDIMENSION numrows,
                                           JDIMENSION maxaccess);
  jvirt_barray_ptr (*request_virt_barray) (j_common_ptr cinfo, int pool_id,
                                           boolean pre_zero,
                                           JDIMENSION blocksperrow,
                                           JDIMENSION numrows,
                                           JDIMENSION maxaccess);
  void (*realize_virt_arrays) (j_common_ptr cinfo);
  JSAMPARRAY (*access_virt_sarray) (j_common_ptr cinfo, jvirt_sarray_ptr ptr,
                                    JDIMENSION start_row, JDIMENSION num_rows,
                                    boolean writable);
  JBLOCKARRAY (*access_virt_barray) (j_common_ptr cinfo, jvirt_barray_ptr ptr,
                                     JDIMENSION start_row, JDIMENSION num_rows,
                                     boolean writable);
  void (*free_pool) (j_common_ptr cinfo, int pool_id);
  void (*self_destruct) (j_common_ptr cinfo);

  /* Limit on the total memory allocated (0: no limit) */
  long max_memory_to_use;

  long max_alloc_chunk;         /* unused */
};


/* Routine reading an APPn or COM marker (jpeg_set_marker_processor()). */
typedef boolean (*jpeg_marker_parser_method) (j_decompress_ptr cinfo);


#define JPP(arglist)    arglist


/* Standard error handler. */
EXTERN(struct jpeg_error_mgr *) jpeg_std_error(struct jpeg_error_mgr *err);

/* Object creation; the macros pass the caller's structure sizes so that a
   mismatch between header and library is detected. */
#define jpeg_create_compress(cinfo) \
  jpeg_CreateCompress((cinfo), JPEG_LIB_VERSION, \
                      (size_t)sizeof(struct jpeg_compress_struct))
#define jpeg_create_decompress(cinfo) \
  jpeg_CreateDecompress((cinfo), JPEG_LIB_VERSION, \
                        (size_t)sizeof(struct jpeg_decompress_struct))
EXTERN(void) jpeg_CreateCompress(j_compress_ptr cinfo, int version,
                                 size_t structsize);
EXTERN(void) jpeg_CreateDecompress(j_decompress_ptr cinfo, int version,
                                   size_t structsize);
EXTERN(void) jpeg_destroy_compress(j_compress_ptr cinfo);
EXTERN(void) jpeg_destroy_decompress(j_decompress_ptr cinfo);

/* Standard data sources and destinations. */
EXTERN(void) jpeg_stdio_dest(j_compress_ptr cinfo, FILE *outfile);
EXTERN(void) jpeg_stdio_src(j_decompress_ptr cinfo, FILE *infile);

EXTERN(void) jpeg_mem_dest(j_compress_ptr cinfo, unsigned char **outbuffer,
                           unsigned long *outsize);
EXTERN(void) jpeg_mem_src(j_decompress_ptr cinfo,
                          const unsigned char *inbuffer, unsigned long insize);

/* Compression parameters. */
EXTERN(void) jpeg_set_defaults(j_compress_ptr cinfo);
EXTERN(void) jpeg_set_colorspace(j_compress_ptr cinfo,
                                 J_COLOR_SPACE colorspace);
EXTERN(void) jpeg_default_colorspace(j_compress_ptr cinfo);
EXTERN(void) jpeg_set_quality(j_compress_ptr cinfo, int quality,
                              boolean force_baseline);
EXTERN(void) jpeg_set_linear_quality(j_compress_ptr cinfo, int scale_factor,
                                     boolean force_baseline);
#if JPEG_LIB_VERSION >= 70
EXTERN(void) jpeg_default_qtables(j_compress_ptr cinfo,
                                  boolean force_baseline);
#endif
EXTERN(void) jpeg_add_quant_table(j_compress_ptr cinfo, int which_tbl,
                                  const unsigned int *basic_table,
                                  int scale_factor, boolean force_baseline);
EXTERN(int) jpeg_quality_scaling(int quality);
EXTERN(void) jpeg_enable_lossless(j_compress_ptr cinfo,
                                  int predictor_selection_value,
                                  int point_transform);
EXTERN(void) jpeg_simple_progression(j_compress_ptr cinfo);
EXTERN(void) jpeg_suppress_tables(j_compress_ptr cinfo, boolean suppress);
EXTERN(JQUANT_TBL *) jpeg_alloc_quant_table(j_common_ptr cinfo);
EXTERN(JHUFF_TBL *) jpeg_alloc_huff_table(j_common_ptr cinfo);

/* Compression. */
EXTERN(void) jpeg_start_compress(j_compress_ptr cinfo,
                                 boolean write_all_tables);
EXTERN(JDIMENSION) jpeg_write_scanlines(j_compress_ptr cinfo,
                                        JSAMPARRAY scanlines,
                                        JDIMENSION num_lines);
EXTERN(JDIMENSION) jpeg12_write_scanlines(j_compress_ptr cinfo,
                                          J12SAMPARRAY scanlines,
                                          JDIMENSION num_lines);
EXTERN(JDIMENSION) jpeg16_write_scanlines(j_compress_ptr cinfo,
                                          J16SAMPARRAY scanlines,
                                          JDIMENSION num_lines);
EXTERN(void) jpeg_finish_compress(j_compress_ptr cinfo);

#if JPEG_LIB_VERSION >= 70
EXTERN(void) jpeg_calc_jpeg_dimensions(j_compress_ptr cinfo);
#endif

/* Raw (downsampled) data. */
EXTERN(JDIMENSION) jpeg_write_raw_data(j_compress_ptr cinfo, JSAMPIMAGE data,
                                       JDIMENSION num_lines);
EXTERN(JDIMENSION) jpeg12_write_raw_data(j_compress_ptr cinfo,
                                         J12SAMPIMAGE data,
                                         JDIMENSION num_lines);

/* Markers written after jpeg_start_compress() and before the first row. */
EXTERN(void) jpeg_write_marker(j_compress_ptr cinfo, int marker,
                               const JOCTET *dataptr, unsigned int datalen);
EXTERN(void) jpeg_write_m_header(j_compress_ptr cinfo, int marker,
                                 unsigned int datalen);
EXTERN(void) jpeg_write_m_byte(j_compress_ptr cinfo, int val);

/* Tables-only (abbreviated) datastream. */
EXTERN(void) jpeg_write_tables(j_compress_ptr cinfo);

EXTERN(void) jpeg_write_icc_profile(j_compress_ptr cinfo,
                                    const JOCTET *icc_data_ptr,
                                    unsigned int icc_data_len);

/* Decompression. */
EXTERN(int) jpeg_read_header(j_decompress_ptr cinfo, boolean require_image);
#define JPEG_SUSPENDED           0 /* data source suspended */
#define JPEG_HEADER_OK           1 /* image header read */
#define JPEG_HEADER_TABLES_ONLY  2 /* tables-only datastream */

EXTERN(boolean) jpeg_start_decompress(j_decompress_ptr cinfo);
EXTERN(JDIMENSION) jpeg_read_scanlines(j_decompress_ptr cinfo,
                                       JSAMPARRAY scanlines,
                                       JDIMENSION max_lines);
EXTERN(JDIMENSION) jpeg12_read_scanlines(j_decompress_ptr cinfo,
                                         J12SAMPARRAY scanlines,
                                         JDIMENSION max_lines);
EXTERN(JDIMENSION) jpeg16_read_scanlines(j_decompress_ptr cinfo,
                                         J16SAMPARRAY scanlines,
                                         JDIMENSION max_lines);
EXTERN(JDIMENSION) jpeg_skip_scanlines(j_decompress_ptr cinfo,
                                       JDIMENSION num_lines);
EXTERN(JDIMENSION) jpeg12_skip_scanlines(j_decompress_ptr cinfo,
                                         JDIMENSION num_lines);
EXTERN(void) jpeg_crop_scanline(j_decompress_ptr cinfo, JDIMENSION *xoffset,
                                JDIMENSION *width);
EXTERN(void) jpeg12_crop_scanline(j_decompress_ptr cinfo, JDIMENSION *xoffset,
                                  JDIMENSION *width);
EXTERN(boolean) jpeg_finish_decompress(j_decompress_ptr cinfo);

/* Raw (downsampled) data. */
EXTERN(JDIMENSION) jpeg_read_raw_data(j_decompress_ptr cinfo, JSAMPIMAGE data,
                                      JDIMENSION max_lines);
EXTERN(JDIMENSION) jpeg12_read_raw_data(j_decompress_ptr cinfo,
                                        J12SAMPIMAGE data,
                                        JDIMENSION max_lines);

/* Buffered-image mode. */
EXTERN(boolean) jpeg_has_multiple_scans(j_decompress_ptr cinfo);
EXTERN(boolean) jpeg_start_output(j_decompress_ptr cinfo, int scan_number);
EXTERN(boolean) jpeg_finish_output(j_decompress_ptr cinfo);
EXTERN(boolean) jpeg_input_complete(j_decompress_ptr cinfo);
EXTERN(void) jpeg_new_colormap(j_decompress_ptr cinfo);
EXTERN(int) jpeg_consume_input(j_decompress_ptr cinfo);
/* Return values of jpeg_consume_input() (also JPEG_SUSPENDED) */
#define JPEG_REACHED_SOS        1 /* start of a new scan */
#define JPEG_REACHED_EOI        2 /* end of image */
#define JPEG_ROW_COMPLETED      3 /* an iMCU row completed */
#define JPEG_SCAN_COMPLETED     4 /* the last iMCU row of a scan completed */

/* Output dimensions for the current parameters. */
#if JPEG_LIB_VERSION >= 80
EXTERN(void) jpeg_core_output_dimensions(j_decompress_ptr cinfo);
#endif
EXTERN(void) jpeg_calc_output_dimensions(j_decompress_ptr cinfo);

/* Marker handling. */
EXTERN(void) jpeg_save_markers(j_decompress_ptr cinfo, int marker_code,
                               unsigned int length_limit);
EXTERN(void) jpeg_set_marker_processor(j_decompress_ptr cinfo,
                                       int marker_code,
                                       jpeg_marker_parser_method routine);

/* Transcoding (lossless transformations). */
EXTERN(jvirt_barray_ptr *) jpeg_read_coefficients(j_decompress_ptr cinfo);
EXTERN(void) jpeg_write_coefficients(j_compress_ptr cinfo,
                                     jvirt_barray_ptr *coef_arrays);
EXTERN(void) jpeg_copy_critical_parameters(j_decompress_ptr srcinfo,
                                           j_compress_ptr dstinfo);

/* Aborting an image (the object remains usable) and destroying objects. */
EXTERN(void) jpeg_abort_compress(j_compress_ptr cinfo);
EXTERN(void) jpeg_abort_decompress(j_decompress_ptr cinfo);

EXTERN(void) jpeg_abort(j_common_ptr cinfo);
EXTERN(void) jpeg_destroy(j_common_ptr cinfo);

/* Default restart-marker resynchronization method. */
EXTERN(boolean) jpeg_resync_to_restart(j_decompress_ptr cinfo, int desired);

/* Reassembles the ICC profile from saved APP2 markers (requires
   jpeg_save_markers(cinfo, JPEG_APP0 + 2, 0xFFFF)); the profile is
   allocated with malloc(). */
EXTERN(boolean) jpeg_read_icc_profile(j_decompress_ptr cinfo,
                                      JOCTET **icc_data_ptr,
                                      unsigned int *icc_data_len);


/* Marker codes. */
#define JPEG_RST0       0xD0
#define JPEG_EOI        0xD9
#define JPEG_APP0       0xE0
#define JPEG_COM        0xFE


/* Dummy definitions of the private structures for compilers that require
   complete types. */
#ifdef INCOMPLETE_TYPES_BROKEN
#ifndef JPEG_INTERNALS
struct jvirt_sarray_control { long dummy; };
struct jvirt_barray_control { long dummy; };
struct jpeg_comp_master { long dummy; };
struct jpeg_c_main_controller { long dummy; };
struct jpeg_c_prep_controller { long dummy; };
struct jpeg_c_coef_controller { long dummy; };
struct jpeg_marker_writer { long dummy; };
struct jpeg_color_converter { long dummy; };
struct jpeg_downsampler { long dummy; };
struct jpeg_forward_dct { long dummy; };
struct jpeg_entropy_encoder { long dummy; };
struct jpeg_decomp_master { long dummy; };
struct jpeg_d_main_controller { long dummy; };
struct jpeg_d_coef_controller { long dummy; };
struct jpeg_d_post_controller { long dummy; };
struct jpeg_input_controller { long dummy; };
struct jpeg_marker_reader { long dummy; };
struct jpeg_entropy_decoder { long dummy; };
struct jpeg_inverse_dct { long dummy; };
struct jpeg_upsampler { long dummy; };
struct jpeg_color_deconverter { long dummy; };
struct jpeg_color_quantizer { long dummy; };
#endif
#endif


#ifdef JPEG_INTERNALS
#include "jpegint.h"
#include "jerror.h"
#endif

#ifdef __cplusplus
#ifndef DONT_USE_EXTERN_C
}
#endif
#endif

#endif /* JPEGLIB_H */
