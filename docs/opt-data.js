// Hand-written explanations for the optimization logs in README.md (docs/data.js).
// Entries are keyed by the first commit of each log line; the gains come from data.js.
//
// Timelines: lanes separated by "||", boxes by "|", each box "label:width" with an
// optional suffix: "+" the new or changed step, "!" wasted work or a stall, "_" an
// empty gap.  Widths are schematic, not measured.
window.VIO_OPT = {
 ideas: {
  overlap: { name: "Overlap independent work", short: "Overlap", blurb: "Give the core two independent chains, so one fills the other's stalls." },
  chain: { name: "Shorten the critical path", short: "Shorter chain", blurb: "Fewer or cheaper operations on the loop-carried dependency chain." },
  wide: { name: "Do more per operation", short: "Wider", blurb: "SIMD, 64-bit SWAR words and multi-symbol tables: several items per step." },
  skip: { name: "Skip work", short: "Skip work", blurb: "Bitmaps, samples and saved results so the work is done only where needed." },
  memory: { name: "Keep data in registers", short: "Registers", blurb: "No spills, false aliasing or store-forwarding stalls between steps." },
  alloc: { name: "Stop paying the allocator", short: "Allocation", blurb: "Reuse buffers instead of allocating, zeroing and faulting them in again." },
  compiler: { name: "Write what the compiler vectorizes", short: "Compiler", blurb: "Scalar code in the form each compiler turns into good machine code." },
  design: { name: "Rethink the data path", short: "Data path", blurb: "Fewer passes over the data, larger batches, a different algorithm." }
 },
 items: {
  // ───────── JPEG ─────────
  "3e78226": {
   t: "Marker-free 64-bit bit reader", idea: "wide",
   p: "Progressive scans refilled the bit buffer one byte at a time, checking every byte for a 0xFF marker, and sequential decoding spent one table lookup per symbol.",
   s: "While no marker is within reach, read 64 bits at once into a left-aligned buffer with no per-byte checks; refinement uses a non-zero bitmap; look up two AC symbols at once when both fit the 11-bit window. A shadow count keeps corrupt-data warnings exactly as before.",
   b: "byte:1|0xFF?:1!|byte:1|0xFF?:1!|byte:1|0xFF?:1!|symbol:2|symbol:2",
   a: "8 bytes:2+|2 symbols:2+"
  },
  "545fc4b": {
   t: "Batched progressive encoding, branch-free coders", idea: "design",
   p: "Progressive AC encoding took one block per call, with the statistics and output passes sharing branchy code; the lossless and arithmetic encoders branched on data in every sample.",
   s: "Encode a run of blocks per call with separate statistics and output code, pack correction bits in words and find new coefficients from bit masks. Magnitudes and interval updates are computed without branches; lossless writes 32 bits at a time.",
   b: "call:1!|block:2|mispredict:2!|call:1!|block:2|mispredict:2!",
   a: "call:1|block:2+|block:2+|block:2+"
  },
  "8a012f5": {
   t: "Pick the scalar form per instruction set", idea: "compiler",
   p: "On the x86-64 baseline (SSE2) there is no 32-bit vector multiply; the compiler emulated it, and per-sample color conversion and upsampling ran slower than tables.",
   s: "Use the per-sample formulas only where 32-bit vector multiplies exist (NEON, SSE4.1 and up); elsewhere keep the tables and running sums.",
   b: "emulated mul32:3!|emulated mul32:3!|add:1|pack:1",
   a: "table:1+|table:1+|add:1|pack:1"
  },
  "3b14b7b": {
   t: "Vector non-zero masks", idea: "wide",
   p: "Refinement decoding needs to know which of a block's 64 coefficients are already non-zero, and tested them one by one.",
   s: "Compare 8–16 coefficients per instruction and turn the flags into a bitmask; x86 reorders it to zig-zag a nibble at a time through tables, AArch64 with tbl before making bits.",
   b: "test ×8:1|test ×8:1|test ×8:1|test ×8:1|test ×8:1|test ×8:1|test ×8:1|test ×8:1",
   a: "cmp ×16:1+|cmp ×16:1+|cmp ×16:1+|cmp ×16:1+|→ bits:1+"
  },
  "4dd1653": {
   t: "Bit writer back in registers (MSVC)", idea: "memory",
   p: "A lambda coding each coefficient referred to the Huffman bit writer, so MSVC kept the writer in memory: every code written was a load and a store.",
   s: "Make the coefficient step a function that takes the writer, so it stays in registers; build the non-zero bitmap without a loop, which MSVC also kept in memory.",
   b: "load writer:1.5!|emit:1|store writer:1.5!|load writer:1.5!|emit:1|store writer:1.5!",
   a: "emit:1+|emit:1+"
  },
  "3193dec": {
   t: "Zeroed buffers from calloc", idea: "alloc",
   p: "Large zeroed buffers, such as the whole-image coefficient buffer of progressive decoding, were allocated and then cleared with a pass over every byte.",
   s: "Allocate them with calloc: large blocks come from the system already zeroed, and the clearing pass disappears.",
   b: "malloc:1|memset whole buffer:4!|decode:6",
   a: "calloc:1+|decode:6"
  },
  "bd858b0": {
   t: "Correction bits from the lowest position up", idea: "chain",
   p: "Applying refinement correction bits from the highest remaining position took an lzcnt every iteration: slow on x86, and right on the loop's dependency chain.",
   s: "Walk from the lowest set bit instead; clearing it (x & (x − 1)) is one fast operation on the chain.",
   b: "lzcnt:3!|clear:1|lzcnt:3!|clear:1|lzcnt:3!|clear:1",
   a: "x&(x−1):1+|x&(x−1):1+|x&(x−1):1+"
  },
  "c1674e9": {
   t: "SWAR fancy upsampling", idea: "wide",
   p: "Where the compiler leaves per-sample loops scalar (MSVC, the x86-64 baseline), fancy upsampling computed one 8-bit sample per operation.",
   s: "Put four samples in the 16-bit lanes of a 64-bit word; one add and shift handle all four, and the lanes never carry into each other. Kept for MSVC only in h2v1, where GCC and Clang do better with running sums.",
   b: "s0:1|s1:1|s2:1|s3:1|s4:1|s5:1|s6:1|s7:1",
   a: "s0–s3:1.4+|s4–s7:1.4+"
  },
  "76d0383": {
   t: "Byte stuffing 16 bytes at a time", idea: "wide",
   p: "Writing coded data called memchr to find each 0xFF and memcpy to copy up to it: two library calls per 0xFF, some 256 bytes apart.",
   s: "Copy 16 bytes at a time while word arithmetic shows they hold no 0xFF; stuff a zero byte only when one appears.",
   b: "memchr:2!|memcpy:2!|stuff:1|memchr:2!|memcpy:2!|stuff:1",
   a: "16 B:1+|16 B:1+|stuff:1|16 B:1+|16 B:1+|stuff:1"
  },
  "4e2bea0": {
   t: "Packed YCbCr→RGB tables", idea: "wide",
   p: "Scalar color conversion loaded four table terms per pixel: Cr→R, Cr→G, Cb→G and Cb→B.",
   s: "Pack the red, green and blue terms of Cr in one 64-bit word and those of Cb in another: two loads and one addition give all three channels.",
   b: "Cr→R:1|Cr→G:1|Cb→G:1|Cb→B:1|3 adds:2",
   a: "Cr word:1+|Cb word:1+|add:1+"
  },
  "692e995": {
   t: "Prepare the next block while writing this one", idea: "overlap",
   p: "Each block's vector work (zig-zag order, magnitude categories, non-zero bitmap) started only after the previous block's bit writing ended, with a mispredicted branch in between.",
   s: "Hand the encoder runs of up to 32 blocks; the kernel prepares block n + 1 before writing block n, so vector work overlaps the bit writing.",
   b: "prep 1:2|write 1:3|miss:1!|prep 2:2|write 2:3|miss:1!|prep 3:2|write 3:3",
   a: "prep 1:2|write 1:3|miss:1!|write 2:3|miss:1!|write 3:3||_:2|prep 2:2+|_:2|prep 3:2+"
  },
  "3e6f49f": {
   t: "Pipelined progressive AC encoding", idea: "overlap",
   p: "Progressive AC encoding had the same pattern: prepare a band, code it, mispredict at its end, and only then start the next band.",
   s: "Prepare the next block's band before coding the current one, as in sequential encoding.",
   b: "band 1:2|code 1:3|miss:1!|band 2:2|code 2:3|miss:1!",
   a: "band 1:2|code 1:3|miss:1!|code 2:3|miss:1!||_:2|band 2:2+"
  },
  "c7a099f": {
   t: "Refinement masks one block ahead", idea: "overlap",
   p: "The refinement decoder computed a block's non-zero mask after the mispredicted branch that ends the previous block, so the mask was on the critical path.",
   s: "Compute the next block's mask before decoding the current one, so it is ready when the branch resolves.",
   b: "mask 1:2|decode 1:4|miss:1!|mask 2:2|decode 2:4|miss:1!",
   a: "mask 1:2|decode 1:4|miss:1!|decode 2:4|miss:1!||_:2|mask 2:2+"
  },
  "5e4033e": {
   t: "Two-block forward DCT", idea: "overlap",
   p: "One block's DCT on Neon is a chain of dependent passes; with a single block the core waits for each pass's results.",
   s: "Transform a whole component row of an MCU run per call; the Neon kernel takes two adjacent blocks at once, two independent chains to interleave.",
   b: "A rows:2|wait:1!|A cols:2|wait:1!|B rows:2|wait:1!|B cols:2|wait:1!",
   a: "A rows:2|wait:0.5!|A cols:2||_:1|B rows:2+|B cols:2+"
  },
  "68f3a1d": {
   t: "One byte transpose for both blocks", idea: "wide",
   p: "The two-block DCT widened each block's samples to 16 bits and transposed the two blocks separately.",
   s: "Transpose the 16-byte rows of both blocks in one pass of byte, halfword and word steps, then widen.",
   b: "widen A:1|transpose A:3|widen B:1|transpose B:3",
   a: "transpose A+B bytes:3+|widen:1"
  },
  "62e36ff": {
   t: "Base pointers for table loads (GCC)", idea: "compiler",
   p: "GCC recomputed each per-coefficient address from the stack frame and the table's offset: three instructions where one indexed load does.",
   s: "Index the prepared block's arrays and the emit table through local base pointers.",
   b: "addr:1!|addr:1!|load:1|emit:2|addr:1!|addr:1!|load:1|emit:2",
   a: "load [p+i]:1+|emit:2|load [p+i]:1+|emit:2"
  },
  "571291d": {
   t: "Progressive bands built in registers", idea: "wide",
   p: "The AC bands of the usual progression scripts start at zig-zag position 1 or 6, and were gathered one coefficient at a time.",
   s: "On AArch64, permute the whole block into zig-zag order with table lookups and take each band row as an extract of two rows; on x86, shift the band from 6 out of the registers the band from 1 already holds.",
   b: "lane:1|lane:1|lane:1|lane:1|lane:1|lane:1|lane:1|lane:1",
   a: "tbl:1+|tbl:1+|ext:1+|ext:1+"
  },
  "f375a10": {
   t: "Correction bits only where needed", idea: "skip",
   p: "Refinement encoding visited every coefficient to decide whether it contributes a correction bit, with a select per coefficient.",
   s: "The prepare kernels already compare magnitudes with 1; they now return that bitmap of new coefficients, so correction bits are gathered only over coefficients that were non-zero before.",
   b: "check:1|check:1|check:1|check:1|check:1|check:1|check:1|check:1",
   a: "bitmap:1+|gather:1|gather:1|gather:1"
  },
  "3317d93": {
   t: "One 32-bit store per gray pixel", idea: "wide",
   p: "Compilers leave the grayscale-to-RGB loop scalar on x86, where three or four byte stores per pixel limit it.",
   s: "Store the sample times 0x01010101 as one 32-bit word; each store also writes the next pixel's first byte, which the next store overwrites.",
   b: "R:1|G:1|B:1|A:1|R:1|G:1|B:1|A:1",
   a: "RGBA:1+|RGBA:1+",
   code: ["out[0] = g; out[1] = g; out[2] = g;\nout[3] = 0xFF;      // 4 byte stores", "store32(out, g * 0x01010101u | alpha);\n                    // 1 word store"]
  },
  "680f0d8": {
   t: "BMI2 build of the Huffman fast path", idea: "chain",
   p: "Shifts by cl cost Intel cores two extra micro-ops each, and the bit buffer's shifts lie on the chain from one table lookup to the next.",
   s: "Compile the fast path a second time with BMI2, where shlx and shrx are single micro-ops, and pick it at run time.",
   b: "shl cl:3!|lookup:2|shr cl:3!|lookup:2",
   a: "shlx:1+|lookup:2|shrx:1+|lookup:2"
  },
  "1f83f2d": {
   t: "IDCT in named vectors (MSVC)", idea: "memory",
   p: "The 128-bit accurate IDCT kept the block in an array, and MSVC stored it to memory between the two passes.",
   s: "Keep the block in eight named vector variables from the loads to the stores.",
   b: "pass 1:3|store ×8:2!|load ×8:2!|pass 2:3",
   a: "pass 1:3|pass 2:3+"
  },

  // ───────── PNG ─────────
  "4a01186": {
   t: "Inflate straight into the image", idea: "design",
   p: "A plain decoder inflates into a scratch buffer, copies rows out, unfilters them byte by byte, decodes one literal per table lookup and checksums with table walks.",
   s: "Inflate into the image buffer and reconstruct rows in place with NEON/SSE2 filters; an 11-bit table whose entries hold two literals, three lookups per refill, 16-byte match copies; CRC-32 by carry-less multiplication, Adler-32 with SIMD.",
   b: "inflate:4|copy rows:2!|unfilter:3|CRC-32:2|Adler-32:2",
   a: "inflate in place:3+|unfilter:1.5+|CRC:0.5+|Adler:0.5+"
  },
  "80db3a4": {
   t: "Single-pass fast encoder", idea: "design",
   p: "A zlib-style encoder searches hash chains for matches at every byte, which dominates PNG encoding time, and gains little on filtered image data.",
   s: "Pick each row's filter by the smallest sum of absolute residuals (SIMD), code the image as one DEFLATE block with a Huffman code from sampled rows, code runs of zero residuals as distance-1 matches, and compute 32 structured literal codes per AVX2 step.",
   b: "filter trials:2|hash-chain match search:10!|Huffman:2",
   a: "SIMD filter:1+|RLE + codes:1+"
  },
  "ed781ce": {
   t: "Chained literal lookups", idea: "chain",
   p: "Each speculative literal lookup shifted the bit buffer by a running sum of the bit counts: an add, then a shift, on the chain.",
   s: "Shift the value the previous lookup already shifted, by that entry's own count: one shift, mask and load per lookup.",
   b: "sum += n:1!|shift:1|mask:1|load:2|sum += n:1!|shift:1|mask:1|load:2",
   a: "shift:1+|mask:1|load:2|shift:1+|mask:1|load:2"
  },
  "20ddc88": {
   t: "Sampled statistics, four codes per write", idea: "skip",
   p: "Two-pass encoding gathered Huffman statistics over every row, and the scalar coder wrote at most three literal codes per store.",
   s: "Take the statistics from about 32 KiB of rows; with literal codes limited to 14 bits, four of them (56 bits) fit one 64-bit write.",
   b: "stats: all rows:4!|code:4",
   a: "stats: 32 KiB:1+|code:3+"
  },
  "78b6799": {
   t: "AVX2 filter selection", idea: "wide",
   p: "Filter selection computes five candidate filters and their residual sums for every row, 16 bytes per step.",
   s: "With AVX2, do the candidates, their sums and the chosen filter 32 bytes per step.",
   b: "16 B:1|16 B:1|16 B:1|16 B:1",
   a: "32 B:1+|32 B:1+"
  },
  "e3125ea": {
   t: "Paeth on 16-bit lanes (x86)", idea: "chain",
   p: "Paeth reconstruction is a pixel-to-pixel chain; on bytes x86 needs extra steps for unsigned comparisons, 16-bit images ran scalar code, and each decode allocated a new image buffer.",
   s: "Paeth on 16-bit lanes for a shorter chain (with an SSE4.1 variant), SSE2 for 6- and 8-byte pixels, and the last image buffer kept per thread for the next decode.",
   b: "fix sign:2!|paeth:3|fix sign:2!|paeth:3",
   a: "paeth 16-bit:3+|paeth 16-bit:3+"
  },
  "b7c46cb": {
   t: "Previous pixel in a register", idea: "memory",
   p: "Gray and gray+alpha unfiltering reloaded the previous pixel from the row just stored, a store-to-load round trip per pixel; 16-bit images ran scalar code on Arm.",
   s: "Carry the previous pixel in a register, use a branchless Paeth choice, and add NEON kernels for 6- and 8-byte pixels.",
   b: "reload:2!|filter:1|store:1|reload:2!|filter:1|store:1",
   a: "filter:1+|store:1|filter:1+|store:1"
  },
  "3b275b6": {
   t: "One inflater per thread", idea: "alloc",
   p: "The decode tables (about 30 KiB) were allocated for every image, and small images paid for it on allocators that return memory to the system.",
   s: "Keep one inflater per thread and reuse its tables.",
   b: "alloc 30 KiB:3!|decode:2|free:1!|alloc 30 KiB:3!|decode:2|free:1!",
   a: "decode:2+|decode:2+"
  },
  "65386c4": {
   t: "Leaner de-interlacing", idea: "skip",
   p: "De-interlacing zeroed the whole image first and copied each pass's pixels with variable-length copies.",
   s: "Move pixels with fixed-size moves per pixel size, and zero the image only for packed (sub-byte) samples.",
   b: "zero image:3!|copy n:2|copy n:2|copy n:2",
   a: "move 4 B:1+|move 4 B:1+|move 4 B:1+"
  },
  "8309220": {
   t: "12-bit table, cheaper to build", idea: "wide",
   p: "A larger literal/length table resolves more codes in one lookup, but costs more to build for each block.",
   s: "Place each literal pair once, at the table size of its combined code length; doubling the table replicates it, so the 12-bit table builds faster than the 11-bit one did.",
   b: "build 11-bit:3|decode:4|2nd lookups:1!",
   a: "build 12-bit:2+|decode:4"
  },
  "cd4f332": {
   t: "Cheaper set-up for small images", idea: "skip",
   p: "For 64×64 icons, gathering statistics and building the Huffman code cost as much as encoding.",
   s: "Assign canonical codes over the 76-symbol reduced alphabet (a group's 16 symbols get consecutive codes), and sample about 8 KiB of data for the statistics.",
   b: "stats 32 KiB:3!|code 286 symbols:3!|encode:2",
   a: "stats 8 KiB:1+|code 76:1+|encode:2"
  },
  "aef2df7": {
   t: "Block copies in inflate's tail loop", idea: "wide",
   p: "The careful loop near the end of the input copied matches byte by byte, and images often end with long runs that this loop decodes.",
   s: "Fill distance-1 runs with memset and copy non-overlapping matches with memcpy.",
   b: "byte:1|byte:1|byte:1|byte:1|byte:1|byte:1|byte:1|byte:1",
   a: "memset run:2+"
  },
  "fb6801e": {
   t: "SIMD filter selection in zlib mode", idea: "wide",
   p: "zlib mode computed each candidate filter's residuals byte by byte.",
   s: "Reuse the fast mode's SIMD filter selection with the same choice (smallest sum, first filter on ties), so the files stay byte-identical.",
   b: "None:1.5|Sub:1.5|Up:1.5|Avg:1.5|Paeth:1.5|deflate:5",
   a: "5 filters, SIMD:2+|deflate:5"
  },
  "df87ff9": {
   t: "Word-wise match comparison", idea: "wide",
   p: "The longest-match search compared candidate strings a byte at a time, and codes were written out in small pieces.",
   s: "Compare 8 bytes at a time (the first and last two bytes as 16-bit words), and gather codes in 64 bits to write 32 at a time. Output stays identical to zlib's.",
   b: "cmp 1 B:1|cmp 1 B:1|cmp 1 B:1|cmp 1 B:1|cmp 1 B:1|cmp 1 B:1|cmp 1 B:1|cmp 1 B:1",
   a: "cmp 8 B:1+|xor → ctz:1+"
  },
  "f3cfc60": {
   t: "Symbols out of the aliasing zone", idea: "memory",
   p: "Block symbols went into bytes overlaid on the output buffer; since any byte store there could alias the deflater's state, the compiler reloaded that state after each store.",
   s: "Store the symbols in their own array of 16-bit pairs. Same block boundaries, same output.",
   b: "store sym:1|reload state:2!|store sym:1|reload state:2!",
   a: "store sym:1+|store sym:1+"
  },
  "1a7f107": {
   t: "Faster scalar code paths", idea: "compiler",
   p: "Without SIMD, unfiltering reloaded the previous pixel, Adler-32 and filter selection were in forms compilers did not vectorize, and CRC-32 ran one table chain.",
   s: "Keep the previous pixel in registers; write Adler-32 and filter selection as branch-free 8-bit arithmetic that compilers vectorize 16 bytes wide; braid CRC-32 into five independent 64-bit words.",
   b: "1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1",
   a: "16 B, auto-vectorized:2+"
  },
  "a6fc885": {
   t: "int arithmetic for MSVC", idea: "compiler",
   p: "MSVC vectorizes neither form of the scalar filter selection, and runs the 8-bit form slower than int arithmetic.",
   s: "With MSVC, compute Paeth and the absolute residuals on ints with 32-bit sums.",
   b: "u8 op:1|widen:1!|narrow:1!|u8 op:1|widen:1!|narrow:1!",
   a: "int op:1+|int op:1+"
  },
  "bd4da24": {
   t: "SSE2 Adler-32 without SSSE3", idea: "wide",
   p: "On x86-64 CPUs without SSSE3 (or with SSE2 forced), Adler-32 fell back to a scalar loop that MSVC does not vectorize: 5.7 GB/s.",
   s: "Instantiate the generic vector Adler-32 kernel with the SSE2 interface: 46.5 GB/s.",
   b: "1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1",
   a: "16 B:1+"
  },
  "f582411": {
   t: "Paeth with precomputed ranges", idea: "chain",
   p: "Each Paeth pixel depends on its left neighbor a, so the whole predictor (three absolute differences, comparisons, selections) sat on the chain along the row.",
   s: "For given b and c, the values of a that predict b or c form at most two ranges; their bounds (and x + b, x + c) are computed 16 bytes at a time off the chain, leaving three comparisons and two selections per pixel on it.",
   b: "pa:1|pb:1|pc:1|cmp:1|cmp:1|select:1|add:1",
   a: "cmp:1+|cmp:1+|cmp:1+|sel:1+|sel:1+||ranges ×16, off the chain:3"
  },
  "f35cffb": {
   t: "Vectorizable scalar Adler-32", idea: "compiler",
   p: "The scalar Adler-32 needed a horizontal sum after each 32-byte block, which kept compilers from vectorizing it fully; short-distance matches were copied byte by byte without NEON.",
   s: "Keep per-position sums across blocks and reduce once (or a plain and an index-weighted sum with Clang on AArch64); copy matches at distances 2 to 7 as a 16-byte pattern.",
   b: "sum 32 B:2|horizontal:2!|sum 32 B:2|horizontal:2!",
   a: "sum 32 B:2+|sum 32 B:2+|reduce:1"
  },
  "4978392": {
   t: "Byte-typed residuals (Clang)", idea: "compiler",
   p: "Residuals reached the absolute value as int, so Clang vectorized filter selection on 16- and 32-bit lanes.",
   s: "Keep them as bytes, and Clang uses 8-bit lanes: four times as many per vector. Files unchanged.",
   b: "4 lanes:1|4 lanes:1|4 lanes:1|4 lanes:1",
   a: "16 lanes:1+"
  },
  "77f90b0": {
   t: "A filter loop MSVC vectorizes", idea: "compiler",
   p: "MSVC vectorizes no loop containing ?:, and none reading cur[i − bpp] alongside cur[i].",
   s: "Select the Paeth predictor and take absolute values with arithmetic masks, and pass the operands through distinct restrict pointers. Files unchanged.",
   b: "1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1|1 B:1",
   a: "16 B:1+|16 B:1+",
   code: ["pred = pa <= pb && pa <= pc ? a\n     : pb <= pc ? b : c;\nres  = cur[i] - pred;  // a = cur[i - bpp]", "ma   = -(pa <= pb & pa <= pc);\nmb   = ~ma & -(pb <= pc);\npred = a & ma | b & mb | c & ~(ma | mb);\nres  = x[i] - pred;    // restrict operands"]
  },
  "d4abf9f": {
   t: "Field-by-field copy (MSVC)", idea: "memory",
   p: "MSVC copied the 24-byte bit writer around zero runs with a 16-byte load right after 8-byte stores of its fields; the load cannot be forwarded from those stores and stalls.",
   s: "Copy the writer field by field.",
   b: "store 8:1|store 8:1|load 16:1|stall:4!",
   a: "copy fields:2+"
  },
  "d0f39d8": {
   t: "Adler-32 on 64-bit words", idea: "wide",
   p: "MSVC and Clang on x86 vectorized the per-position scalar Adler-32 poorly or not at all.",
   s: "Sum the bytes of each 8-byte word with one multiply and keep per-position sums in 16-bit lanes: 17 instead of 11 GB/s.",
   b: "byte:1|byte:1|byte:1|byte:1|byte:1|byte:1|byte:1|byte:1",
   a: "word × 0x01…01:2+|lanes:1+"
  },
  "b3747bf": {
   t: "Braided CRC-32 in named words", idea: "memory",
   p: "MSVC kept the arrays holding the five braids of the scalar CRC-32 in memory.",
   s: "Name each braid as its own 64-bit variable: 10 instead of 5 GB/s without PCLMULQDQ.",
   b: "crc:2|spill:2!|reload:2!|crc:2",
   a: "crc:2+|crc:2+"
  },
  "47697f5": {
   t: "Paeth through t = 3c − a − b (Clang x86)", idea: "compiler",
   p: "The scalar Paeth predictor (three absolute differences and comparisons) stayed scalar under Clang on x86.",
   s: "On 16-bit lanes compute t = 3c − a − b: the prediction is max(a, b), min(a, b) or c by two comparisons with t, which Clang does for all of a pixel's bytes in SSE2 registers.",
   b: "R:2|G:2|B:2|A:2",
   a: "RGBA in SSE2:2+",
   code: ["p  = a + b - c;\npa = abs(p - a); pb = abs(p - b);\npc = abs(p - c);\npred = pa <= pb && pa <= pc ? a\n     : pb <= pc ? b : c;", "t  = 3 * c - a - b;\nlo = min(a, b); hi = max(a, b);\npred = t <= lo ? hi\n     : hi <= t ? lo : c;"]
  },
  "b10524b": {
   t: "Encoder output buffer per thread", idea: "alloc",
   p: "Windows returns large blocks to the system, so every image's output buffer took a page fault on each of its pages.",
   s: "Keep the last buffer released on a thread for the next encode, as the decoder already does for its image buffer.",
   b: "alloc:1|page faults:4!|encode:5|free:1",
   a: "encode:5+"
  },
  "175a138": {
   t: "16-bit filter selection (x86)", idea: "compiler",
   p: "SSE2 has signed 16-bit minimum, maximum and comparisons, but no unsigned byte comparisons, so byte-wise Paeth vectorized poorly.",
   s: "Compute the scalar selection on 16-bit values (Paeth by comparisons with 3c − a − b), which Clang maps onto native SSE2 instructions. Files unchanged.",
   b: "emulate u8 cmp:2!|select:1|emulate u8 cmp:2!|select:1",
   a: "16-bit cmp:1+|select:1|16-bit cmp:1+|select:1"
  },
  "87ff9cd": {
   t: "Two rows at once (NEON)", idea: "overlap",
   p: "Sub, Average and Paeth rows are one long chain of dependent operations along the row; the core spends much of it waiting.",
   s: "Reconstruct two consecutive rows together, the second one 192 bytes behind the first (whose output it reads): two independent chains. Data is inflated a row and 16 bytes past the image's start, so the second row never overwrites input still to be read.",
   b: "r1:1|wait:1!|r1:1|wait:1!|r1:1|wait:1!|r2:1|wait:1!|r2:1|wait:1!|r2:1|wait:1!",
   a: "r1:1|wait:1!|r1:1|wait:1!|r1:1|wait:1!||_:1|r2:1+|wait:1!|r2:1+|wait:1!|r2:1+"
  },
  "8f0da1f": {
   t: "Fewer operations in vector Paeth", idea: "chain",
   p: "The SSE2, AVX2 and generic Paeth predictors computed fresh differences for the sign test and took two separate minimums.",
   s: "Reuse the saturated differences c − a and c − b already computed for pa and pb, and get both selections from one minimum: pa ≤ min(pb, pc) and min(pb, pc) == pb. Predictions unchanged.",
   b: "diffs:2|sign diffs:2!|min:1|min:1!|select:1",
   a: "diffs:2|sign:1+|min:1|select:1"
  },
  "1b7636d": {
   t: "BMI2 build of the inflate loop", idea: "chain",
   p: "Shifts by cl take up to three micro-ops on Intel cores, and they lie on the chain from one table lookup to the next.",
   s: "Compile the inflate fast loop a second time in a BMI2 target region (GCC, Clang) and pick it at run time; shrx and shlx are one micro-op each.",
   b: "shr cl:3!|lookup:2|shr cl:3!|lookup:2",
   a: "shrx:1+|lookup:2|shrx:1+|lookup:2"
  },
  "801d2b2": {
   t: "Keep the Paeth residuals (MSVC scalar)", idea: "skip",
   p: "Filter selection computed the Paeth residuals only to cost them; when Paeth won, the most common case, the row was filtered a second time.",
   s: "Store the Paeth residuals in the output row while costing them.",
   b: "cost 5 filters:5|Paeth again:2!",
   a: "cost 5 + keep Paeth:5+"
  },
  "75e6e3f": {
   t: "Keep the Paeth residuals (SIMD)", idea: "skip",
   p: "The NEON, SSE2 and AVX2 selection had the same waste: Paeth rows were filtered again after being costed.",
   s: "Store the Paeth residuals as they are costed. Files unchanged.",
   b: "cost 5 filters:3|Paeth again:1.5!",
   a: "cost 5 + keep Paeth:3+"
  },
  "ce86b25": {
   t: "Paired Paeth rows (GCC scalar)", idea: "overlap",
   p: "Without NEON, GCC's scalar Paeth is one dependency chain along the row.",
   s: "Reconstruct two Paeth rows together, the second one pixel behind the first, giving GCC two independent chains to schedule.",
   b: "r1:1|wait:1!|r1:1|wait:1!|r2:1|wait:1!|r2:1|wait:1!",
   a: "r1:1|wait:1!|r1:1|wait:1!||_:1|r2:1+|wait:1!|r2:1+"
  },
  "faf21b4": {
   t: "Min/max Paeth (MSVC, Clang)", idea: "chain",
   p: "The textbook Paeth predictor takes three absolute differences and two comparisons per byte.",
   s: "With t = 3c − a − b, the prediction is max(a, b) if t ≤ min(a, b), else min(a, b) if max(a, b) ≤ t, else c: fewer operations, as conditional moves.",
   b: "p:1|3× abs:3|cmp:1|cmp:1|select:1",
   a: "t:1+|min/max:1+|cmp:1|cmp:1|cmov:1+",
   code: ["p  = a + b - c;\npa = abs(p - a); pb = abs(p - b);\npc = abs(p - c);\npred = pa <= pb && pa <= pc ? a\n     : pb <= pc ? b : c;", "t  = 3 * c - a - b;\nlo = min(a, b); hi = max(a, b);\npred = t <= lo ? hi\n     : hi <= t ? lo : c;"]
  },
  "ff9e269": {
   t: "Min/max Paeth for GCC", idea: "chain",
   p: "GCC's scalar Paeth still used the three absolute differences.",
   s: "The same min/max form, written branch-free for GCC.",
   b: "p:1|3× abs:3|cmp:1|cmp:1|select:1",
   a: "t:1+|min/max:1+|cmp:1|cmp:1|select:1"
  },
  "3b7ca71": {
   t: "Min/max in GCC's paired rows", idea: "chain",
   p: "GCC's paired Paeth rows kept the older three-difference predictor.",
   s: "Use the min/max form there too.",
   b: "p:1|3× abs:3|cmp:1|cmp:1|select:1||_:1|p:1|3× abs:3|cmp:1|cmp:1|select:1",
   a: "t:1+|min/max:1+|cmp:1|cmp:1|select:1||_:1|t:1+|min/max:1+|cmp:1|cmp:1|select:1"
  }
 }
};
