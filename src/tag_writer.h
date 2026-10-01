#pragma once
#include <cstddef>
#include <cstdint>

// First writer target: NXP ICODE SLIX2. The chip has 80 addressable blocks, but
// only blocks 0-78 (316 bytes) are user memory: block 79 is the NXP counter and
// is never written, read for comparison or counted as content.
constexpr unsigned TAG_WRITE_BLOCKS = 79;
constexpr size_t TAG_WRITE_BYTES = 4 * TAG_WRITE_BLOCKS;
constexpr uint8_t TAG_MAX_CC_CAPACITY = 0x27; // 8-byte units after the 4-byte CC
struct TagSnapshot {
  uint8_t uid[8] = {}; // ISO15693 wire order (LSB first)
  uint8_t memory[TAG_WRITE_BYTES] = {};
  bool blank = false;
};
class TagWriteIO {
public:
  virtual ~TagWriteIO() = default;
  virtual bool identify(uint8_t uid[8], uint8_t &blockSize, uint16_t &blocks) = 0;
  virtual bool read(const uint8_t uid[8], uint8_t block, uint8_t data[4]) = 0;
  virtual bool security(const uint8_t uid[8], uint8_t status[80]) = 0;
  virtual bool write(const uint8_t uid[8], uint8_t block, const uint8_t data[4]) = 0;
};
enum class WriteCode {
  Verified, Unchanged, NoTag, UnsupportedTag, ReadFailed, TagChanged,
  InvalidImage, OverwriteRequired, SecurityFailed, Locked, WriteFailed, VerifyFailed
};
struct WriteResult {
  WriteCode code;
  int block = -1;
  // True means a write was attempted: failure may have altered the tag.
  bool mayHaveChanged = false;
  unsigned writes = 0;
  explicit WriteResult(WriteCode value) : code(value) {}
  bool ok() const { return code == WriteCode::Verified || code == WriteCode::Unchanged; }
};
const char *writeCodeName(WriteCode code);
WriteResult inspectTag(TagWriteIO &io, TagSnapshot &snapshot);
// Explicit full reinitialization, NOT a region update. Unknown fields/records
// in the supplied image are retained byte-for-byte. Old content may be replaced
// only with overwrite=true. Nothing is locked or password-configured.
WriteResult writeTag(TagWriteIO &io, const TagSnapshot &expected,
                     const uint8_t *image, size_t size, bool overwrite);
