#include "tag_writer.h"
#include "tag_parser.h"
#include <cstring>

const char *writeCodeName(WriteCode c) {
  switch(c) {
    case WriteCode::Verified: return "verified";
    case WriteCode::Unchanged: return "unchanged";
    case WriteCode::NoTag: return "no_tag_or_io_error";
    case WriteCode::UnsupportedTag: return "unsupported_tag";
    case WriteCode::ReadFailed: return "read_failed";
    case WriteCode::TagChanged: return "tag_changed";
    case WriteCode::InvalidImage: return "invalid_image";
    case WriteCode::OverwriteRequired: return "overwrite_required";
    case WriteCode::SecurityFailed: return "security_status_unavailable";
    case WriteCode::Locked: return "locked";
    case WriteCode::WriteFailed: return "write_failed";
    case WriteCode::VerifyFailed: return "verify_failed";
  }
  return "unknown";
}
WriteResult inspectTag(TagWriteIO &io, TagSnapshot &snapshot) {
  TagSnapshot candidate;
  uint8_t blockSize=0; uint16_t blocks=0;
  if(!io.identify(candidate.uid,blockSize,blocks)) return WriteResult(WriteCode::NoTag);
  if(blockSize!=4 || blocks!=80 || candidate.uid[7]!=0xe0 ||
     candidate.uid[6]!=0x04 || candidate.uid[5]!=0x01)
    return WriteResult(WriteCode::UnsupportedTag);
  candidate.blank=true;
  for(unsigned block=0;block<80;++block) {
    if(!io.read(candidate.uid,uint8_t(block),candidate.memory+4*block)) {
      WriteResult r(WriteCode::ReadFailed); r.block=int(block); return r;
    }
    for(unsigned k=0;k<4;++k) if(candidate.memory[4*block+k]!=0) candidate.blank=false;
  }
  snapshot=candidate;
  return WriteResult(WriteCode::Verified);
}
namespace {
bool validateImage(const uint8_t *image,size_t size) {
  // Canonical writable four-byte CC and first TLV at block 1. The one-block
  // commit marker relies on this layout. Other valid layouts are read-only here.
  if(!image || size<10 || size>TAG_WRITE_BYTES || image[0]!=0xe1 || image[1]!=0x40 ||
     image[2]==0 || image[2]>40 || image[3]!=0x01 || image[4]!=0x03) return false;
  size_t offset=6, length=image[5];
  if(length==255) { offset=8; length=(size_t(image[6])<<8)|image[7]; }
  if(!length || offset+length>=size || image[offset+length]!=0xfe) return false;
  for(size_t i=offset+length+1;i<size;++i) if(image[i]!=0) return false;
  // Require the complete supplied image to fit the declared capacity.
  if(size_t(image[2])*8<size) return false;
  SpoolData parsed;
  return parseType5Tag(image,size,parsed)==TagParseResult::Valid;
}
}
WriteResult writeTag(TagWriteIO &io,const TagSnapshot &expected,
                     const uint8_t *image,size_t size,bool overwrite) {
  if(!validateImage(image,size)) return WriteResult(WriteCode::InvalidImage);
  TagSnapshot current;
  WriteResult r=inspectTag(io,current);
  if(!r.ok()) return r;
  if(memcmp(expected.uid,current.uid,8) || memcmp(expected.memory,current.memory,TAG_WRITE_BYTES))
    return WriteResult(WriteCode::TagChanged);
  uint8_t desired[TAG_WRITE_BYTES]={};
  memcpy(desired,image,size);
  if(!memcmp(desired,current.memory,TAG_WRITE_BYTES)) return WriteResult(WriteCode::Unchanged);
  if(!current.blank && !overwrite) return WriteResult(WriteCode::OverwriteRequired);
  // Refuse CC-declared access restrictions, including password protection.
  if((current.memory[0]==0xe1 || current.memory[0]==0xe2) && (current.memory[1]&0x0f))
    return WriteResult(WriteCode::Locked);
  uint8_t status[80]={};
  if(!io.security(current.uid,status)) return WriteResult(WriteCode::SecurityFailed);
  // Block 1 must be writable to invalidate/publish the length. Only changed
  // blocks otherwise need to be writable; existing locked identical data stays.
  for(unsigned b=0;b<80;++b) if((b==1 || memcmp(desired+4*b,current.memory+4*b,4)) && (status[b]&1)) {
    r=WriteResult(WriteCode::Locked); r.block=int(b); return r;
  }
  r=WriteResult(WriteCode::Verified);
  auto put=[&](unsigned block,const uint8_t *data)->bool {
    r.block=int(block); r.mayHaveChanged=true; ++r.writes;
    if(!io.write(current.uid,uint8_t(block),data)) { r.code=WriteCode::WriteFailed; return false; }
    uint8_t check[4];
    if(!io.read(current.uid,uint8_t(block),check) || memcmp(data,check,4)) {
      r.code=WriteCode::VerifyFailed; return false;
    }
    return true;
  };
  // Empty NDEF followed by terminator. Keep this block until every other byte
  // has been verified. Power loss can leave an incomplete tag, never success.
  const uint8_t unpublished[4]={0x03,0x00,0xfe,0x00};
  if(!put(1,unpublished)) return r;
  for(unsigned b=2;b<80;++b)
    if(memcmp(desired+4*b,current.memory+4*b,4) && !put(b,desired+4*b)) return r;
  if(memcmp(desired,current.memory,4) && !put(0,desired)) return r;
  // Verify the entire staged image, not just the blocks that were changed.
  for(unsigned b=0;b<80;++b) {
    uint8_t check[4]; const uint8_t *want=b==1?unpublished:desired+4*b;
    if(!io.read(current.uid,uint8_t(b),check) || memcmp(check,want,4)) {
      r.code=WriteCode::VerifyFailed; r.block=int(b); return r;
    }
  }
  if(!put(1,desired+4)) return r; // publish NDEF length last
  for(unsigned b=0;b<80;++b) {
    uint8_t check[4];
    if(!io.read(current.uid,uint8_t(b),check) || memcmp(check,desired+4*b,4)) {
      r.code=WriteCode::VerifyFailed; r.block=int(b); return r;
    }
  }
  r.block=-1;
  return r;
}
