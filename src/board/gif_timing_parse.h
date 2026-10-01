// board/gif_timing_parse.h
//
// Reads a GIF's per-frame delays and loop count out of the file itself.
//
// raylib's LoadImageAnim* decodes the frames but throws the timing away -- it
// calls stbi_load_gif_from_memory and then frees the delay array, so the
// animation arrives with no schedule attached and every frame would have to be
// shown for the same length of time. Rather than reach into stb (not a public
// raylib interface, and the same call would have to be re-implemented for the
// web build where the bytes never touch a file) this walks the GIF container
// directly, which is a small and completely specified format.
//
// Raylib-free and file-free: it takes bytes, so the same code serves a path and a
// browser drop, and the test target can hand it a hand-built GIF.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "board/gif_timing.h"

namespace referentia::board {

/**
 * ============================================================================
 * GIF Container Parsing
 * ============================================================================
 *
 * Only the parts that carry timing are interpreted. Image data, the palette and
 * the LZW stream are stepped over using their declared lengths, never decoded,
 * which is what keeps this to a hundred lines: a GIF is a header, a screen
 * descriptor, and then a flat sequence of blocks, each of which is either an
 * extension, an image, or the trailer.
 * ============================================================================
 */

/** GIF block introducers, from the spec's block structure. */
inline constexpr std::uint8_t kGifExtensionIntroducer = 0x21;
inline constexpr std::uint8_t kGifImageSeparator = 0x2C;
inline constexpr std::uint8_t kGifTrailer = 0x3B;

/** The graphic control extension: the only block that carries a frame delay. */
inline constexpr std::uint8_t kGifGraphicControlLabel = 0xF9;

/** The application extension, which is where the loop count lives. */
inline constexpr std::uint8_t kGifApplicationExtensionLabel = 0xFF;

/** The "NETSCAPE2.0" application id, the de-facto loop-count extension. */
inline const char* const kGifNetscapeAppId = "NETSCAPE2.0";

/** How much of that id is compared, for the reason given at the use site. */
inline constexpr std::size_t kGifNetscapeAppIdPrefix = 8;

/** Guards against a malformed length field walking off the end of the buffer. */
inline constexpr std::size_t kGifMinHeaderSize = 13;

/**
 * Whether these bytes are a GIF at all.
 *
 * Sniffed from the magic rather than trusted from the file extension, because
 * the extension is a claim the file makes about itself. A .gif that is actually
 * a JPEG is common enough on the web, and trying to parse it as a container
 * produces nonsense delays rather than an error.
 */
inline bool IsGifData(const unsigned char* data, std::size_t size) {
  if (data == nullptr || size < 6) return false;
  return data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
         data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a';
}

/** Total duration of the file, as advertised, before any delay is applied. */
inline constexpr int kGifDelayUnitMs = 10;  // one hundredth of a second

/** Advances past a chain of sub-blocks, returning false if it runs off the end. */
inline bool SkipSubBlocks(const unsigned char* data, std::size_t size,
                          std::size_t& pos) {
  while (pos < size) {
    const std::size_t block_size = data[pos++];
    if (block_size == 0) return true;  // block terminator
    if (pos + block_size > size) return false;
    pos += block_size;
  }
  return false;
}

/**
 * Walks a sub-block chain, reporting the first one whose first byte is `tag`.
 *
 * The application extension's payload is not at a fixed offset -- it is a chain
 * of sub-blocks, and the one carrying the loop count is identified by its
 * leading byte. Returns false if the chain runs off the end of the buffer.
 */
inline bool FindTaggedSubBlock(const unsigned char* data, std::size_t size,
                               std::size_t& pos, std::uint8_t tag,
                               int& value_lo, int& value_hi) {
  while (pos < size) {
    const std::size_t sub_size = data[pos++];
    if (sub_size == 0) return true;  // terminator
    if (pos + sub_size > size) return false;
    if (sub_size >= 3 && data[pos] == tag) {
      value_lo = data[pos + 1];
      value_hi = data[pos + 2];
    }
    pos += sub_size;
  }
  return false;
}

/**
 * Extracts the frame schedule from GIF bytes.
 *
 * Returns a timing whose `frame_delays_ms` has one entry per image in the file,
 * in order, and whose `loop_count` is the NETSCAPE2.0 loop count (0 = forever,
 * which is also the default when the file has no application extension).
 *
 * A file that is not a GIF, or is truncated, yields a timing with a single frame
 * of the default delay. That is not a silent success: the caller has the real
 * decoded frames from raylib and will show them, so a still image is the correct
 * outcome for a file whose timing could not be read, and it is the outcome for
 * every non-GIF format this app supports anyway.
 *
 * Frames with no graphic control extension before them get the default delay,
 * which is the spec's own fallback and what browsers use.
 */
inline GifTiming ParseGifTiming(const unsigned char* data, std::size_t size) {
  GifTiming timing;
  if (!IsGifData(data, size) || size < kGifMinHeaderSize) return timing;

  // Header is 6 bytes, logical screen descriptor 7, then the global colour table
  // if the packed field says there is one. A malformed packed field here would
  // throw the parse off by up to 768 bytes and produce garbage delays, so the
  // table size is computed from the byte rather than assumed absent.
  std::size_t pos = 6 + 7;
  const std::uint8_t packed = data[10];
  if (packed & 0x80) {
    const std::size_t entries = std::size_t{2} << (packed & 0x07);
    pos += entries * 3;
  }
  if (pos >= size) return timing;

  // The delay in hundredths applies to the next image, so it is remembered until
  // an image block consumes it. -1 means "no extension seen since the last
  // frame", which is distinct from a declared delay of zero.
  int pending_delay_units = -1;
  int frames_seen = 0;

  while (pos < size) {
    const std::uint8_t introducer = data[pos++];

    if (introducer == kGifTrailer) break;

    if (introducer == kGifExtensionIntroducer) {
      if (pos >= size) break;
      const std::uint8_t label = data[pos++];

      if (label == kGifGraphicControlLabel) {
        // One fixed 4-byte block: packed, delay (2 bytes little endian),
        // transparent colour index. The block's own size byte is consumed first
        // -- skipping it reads the packed byte as the low half of the delay,
        // which is a plausible-looking number 256 times too large.
        if (pos >= size) return timing;
        const std::size_t block_size = data[pos++];
        if (block_size < 4 || pos + block_size > size) return timing;
        pending_delay_units = data[pos + 1] | (data[pos + 2] << 8);
        pos += block_size;
        if (pos < size && data[pos] == 0) pos++;  // extension terminator
        continue;
      }

      if (label == kGifApplicationExtensionLabel) {
        // A block whose contents are not as the spec describes. The spec allots
        // the application an 8-byte id followed by a 3-byte auth code, but every
        // encoder in existence writes the 11-character string "NETSCAPE2.0" into
        // a block it declares as 11 bytes, with no auth code at all -- so the
        // sub-blocks follow an 11-byte id, not an 8-byte one. Checking for the
        // 8-byte form alone would miss every real looping GIF and quietly give
        // it "loop for ever". Matching the common prefix accepts both.
        if (pos >= size) return timing;
        const std::size_t block_size = data[pos++];
        if (pos + block_size > size) return timing;
        const std::size_t id_len =
            std::min(block_size, kGifNetscapeAppIdPrefix);
        const bool netscape =
            std::string(reinterpret_cast<const char*>(data + pos), id_len) ==
            std::string(kGifNetscapeAppId, id_len);
        pos += block_size;
        if (netscape) {
          int lo = 0;
          int hi = 0;
          if (!FindTaggedSubBlock(data, size, pos, 1, lo, hi)) break;
          timing.loop_count = lo | (hi << 8);
        } else if (!SkipSubBlocks(data, size, pos)) {
          break;
        }
        continue;
      }

      // Some other extension: stepped over, since none of them carry timing.
      if (!SkipSubBlocks(data, size, pos)) break;
      continue;
    }

    if (introducer == kGifImageSeparator) {
      // Image descriptor: 9 bytes, then a local colour table if the packed field
      // has one, then the LZW block chain, which is stepped over rather than
      // decoded.
      if (pos + 9 > size) break;
      const std::uint8_t img_packed = data[pos + 8];
      pos += 9;
      if (img_packed & 0x80) {
        const std::size_t entries = std::size_t{2} << (img_packed & 0x07);
        pos += entries * 3;
      }
      if (pos >= size) break;
      pos++;  // LZW minimum code size
      if (!SkipSubBlocks(data, size, pos)) break;

      const int ms = pending_delay_units < 0
                         ? kMinFrameDelayMs
                         : pending_delay_units * kGifDelayUnitMs;
      timing.frame_delays_ms.push_back(SanitisedDelay(ms));
      pending_delay_units = -1;
      frames_seen++;
      continue;
    }

    // Unknown introducer: the file is not something this parser can walk, and
    // guessing past it would mean the delays that come back belong to the wrong
    // frames. What was read so far is still usable, so it is returned.
    break;
  }

  // No image blocks at all means the container was not really a GIF, and a
  // schedule with no frames would make every caller special-case the empty case.
  if (frames_seen == 0) {
    timing.frame_delays_ms.clear();
    timing.loop_count = 0;
  }
  return timing;
}

}  // namespace referentia::board
