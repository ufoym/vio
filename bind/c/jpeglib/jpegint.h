/*
 * jpegint.h - declarations seen by code that defines JPEG_INTERNALS before
 * including jpeglib.h: object states, private module types and utility
 * functions.  The private modules themselves are opaque in this library.
 */

/* Fixed-point and pointer-sized integer types */
#ifndef __UINTPTR_TYPE__
#define JUINTPTR  size_t
#else
#define JUINTPTR  __UINTPTR_TYPE__
#endif
typedef long JLONG;

/* Values of global_state */
#define CSTATE_START     100    /* after create_compress */
#define CSTATE_SCANNING  101    /* start_compress done, write_scanlines OK */
#define CSTATE_RAW_OK    102    /* start_compress done, write_raw_data OK */
#define CSTATE_WRCOEFS   103    /* jpeg_write_coefficients done */
#define DSTATE_START     200    /* after create_decompress */
#define DSTATE_INHEADER  201    /* reading header markers, no SOS yet */
#define DSTATE_READY     202    /* found SOS, ready for start_decompress */
#define DSTATE_PRELOAD   203    /* reading multiscan file in start_decompress*/
#define DSTATE_PRESCAN   204    /* performing dummy pass for 2-pass quant */
#define DSTATE_SCANNING  205    /* start_decompress done, read_scanlines OK */
#define DSTATE_RAW_OK    206    /* start_decompress done, read_raw_data OK */
#define DSTATE_BUFIMAGE  207    /* expecting jpeg_start_output */
#define DSTATE_BUFPOST   208    /* looking for SOS/EOI in jpeg_finish_output */
#define DSTATE_RDCOEFS   209    /* reading file in jpeg_read_coefficients */
#define DSTATE_STOPPING  210    /* looking for EOI in jpeg_finish_decompress */

/* Buffer modes of the processing modules */
typedef enum {
  JBUF_PASS_THRU,               /* plain stripwise operation */
  JBUF_SAVE_SOURCE,             /* run source subobject only, save output */
  JBUF_CRANK_DEST,              /* run dest subobject only, using saved data */
  JBUF_SAVE_AND_PASS            /* run both subobjects, save output */
} J_BUF_MODE;

/* Sample and difference arrays of lossless coding */
typedef int JDIFF;
typedef JDIFF *JDIFFROW;
typedef JDIFFROW *JDIFFARRAY;
typedef JDIFFARRAY *JDIFFIMAGE;

#define IsExtRGB(cs) \
  (cs == JCS_RGB || (cs >= JCS_EXT_RGB && cs <= JCS_EXT_ARGB))

#define LEFT_SHIFT(a, b)  ((JLONG)((unsigned long)(a) << (b)))
#define SHIFT_TEMPS
#define RIGHT_SHIFT(x, shft)  ((x) >> (shft))

#undef MAX
#define MAX(a, b)       ((a) > (b) ? (a) : (b))
#undef MIN
#define MIN(a, b)       ((a) < (b) ? (a) : (b))

/* Private modules (opaque) */
struct jpeg_comp_master;
struct jpeg_c_main_controller;
struct jpeg_c_prep_controller;
struct jpeg_c_coef_controller;
struct jpeg_marker_writer;
struct jpeg_color_converter;
struct jpeg_downsampler;
struct jpeg_forward_dct;
struct jpeg_entropy_encoder;
struct jpeg_decomp_master;
struct jpeg_d_main_controller;
struct jpeg_d_coef_controller;
struct jpeg_d_post_controller;
struct jpeg_input_controller;
struct jpeg_marker_reader;
struct jpeg_entropy_decoder;
struct jpeg_inverse_dct;
struct jpeg_upsampler;
struct jpeg_color_deconverter;
struct jpeg_color_quantizer;

/* Utility functions */
EXTERN(long) jdiv_round_up(long a, long b);
EXTERN(long) jround_up(long a, long b);
EXTERN(void) jcopy_sample_rows(JSAMPARRAY input_array, int source_row,
                               JSAMPARRAY output_array, int dest_row,
                               int num_rows, JDIMENSION num_cols);
EXTERN(void) j12copy_sample_rows(J12SAMPARRAY input_array, int source_row,
                                 J12SAMPARRAY output_array, int dest_row,
                                 int num_rows, JDIMENSION num_cols);
EXTERN(void) j16copy_sample_rows(J16SAMPARRAY input_array, int source_row,
                                 J16SAMPARRAY output_array, int dest_row,
                                 int num_rows, JDIMENSION num_cols);
EXTERN(void) jcopy_block_row(JBLOCKROW input_row, JBLOCKROW output_row,
                             JDIMENSION num_blocks);
EXTERN(void) jzero_far(void *target, size_t bytestozero);

/* Zigzag-to-natural order of coefficients (with 16 extra entries) */
extern const int jpeg_natural_order[];
