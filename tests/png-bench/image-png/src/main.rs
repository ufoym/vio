// Times image-png on the files listed on standard input (one path per line).
//
//   png-bench-image-png decode [reps]   decode to the PNG's own sample layout
//   png-bench-image-png encode [reps]   decode to 8-bit RGB/RGBA, then time
//                                       encoding it with the ultra-fast preset
//
// Prints one line per file: "image-png <mode> <path> <megapixels> <seconds>
// <bytes>", seconds being the fastest of the repetitions.
use std::io::{BufRead, Cursor};
use std::time::Instant;

fn decode(data: &[u8], t: png::Transformations) -> (png::OutputInfo, Vec<u8>) {
    let mut d = png::Decoder::new(Cursor::new(data));
    d.set_transformations(t);
    let mut r = d.read_info().unwrap();
    let mut buf = vec![0u8; r.output_buffer_size().unwrap()];
    let info = r.next_frame(&mut buf).unwrap();
    (info, buf)
}

fn encode(pixels: &[u8], w: u32, h: u32, alpha: bool) -> Vec<u8> {
    let mut out = Vec::new();
    let mut e = png::Encoder::new(&mut out, w, h);
    e.set_color(if alpha { png::ColorType::Rgba } else { png::ColorType::Rgb });
    e.set_depth(png::BitDepth::Eight);
    e.set_deflate_compression(png::DeflateCompression::FdeflateUltraFast);
    e.set_filter(png::Filter::Adaptive);
    e.write_header().unwrap().write_image_data(pixels).unwrap();
    out
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let mode = args.get(1).map(String::as_str).unwrap_or("decode").to_string();
    let reps: usize = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(5);
    for line in std::io::stdin().lock().lines() {
        let path = line.unwrap();
        let data = std::fs::read(&path).unwrap();
        let mut best = f64::INFINITY;
        let (mp, bytes);
        if mode == "hash" {
            // FNV-1a of the decoded samples (16-bit ones big-endian)
            let (_, buf) = decode(&data, png::Transformations::IDENTITY);
            let mut h: u64 = 0xcbf29ce484222325;
            for &b in &buf {
                h = (h ^ b as u64).wrapping_mul(0x100000001b3);
            }
            println!("{} {:016x}", path, h);
            continue;
        }
        if mode == "decode" {
            let (info, buf) = decode(&data, png::Transformations::IDENTITY);
            mp = info.width as f64 * info.height as f64 * 1e-6;
            bytes = buf.len();
            for _ in 0..reps {
                let t = Instant::now();
                let r = decode(&data, png::Transformations::IDENTITY);
                best = best.min(t.elapsed().as_secs_f64());
                std::hint::black_box(r);
            }
        } else {
            let (info, buf) =
                decode(&data, png::Transformations::EXPAND | png::Transformations::STRIP_16);
            let alpha = matches!(info.color_type, png::ColorType::Rgba | png::ColorType::GrayscaleAlpha);
            let (w, h) = (info.width, info.height);
            // gray and gray+alpha become RGB / RGBA
            let px: Vec<u8> = match info.color_type {
                png::ColorType::Grayscale => buf.iter().flat_map(|&g| [g, g, g]).collect(),
                png::ColorType::GrayscaleAlpha => {
                    buf.chunks(2).flat_map(|c| [c[0], c[0], c[0], c[1]]).collect()
                }
                _ => buf,
            };
            mp = w as f64 * h as f64 * 1e-6;
            let mut size = 0;
            for _ in 0..reps {
                let t = Instant::now();
                let out = encode(&px, w, h, alpha);
                best = best.min(t.elapsed().as_secs_f64());
                size = out.len();
            }
            bytes = size;
        }
        println!("image-png {} {} {:.6} {:.9} {}", mode, path, mp, best, bytes);
    }
}
