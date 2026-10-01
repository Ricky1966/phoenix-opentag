#include "tag_writer.h"
#include "tag_parser.h"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

struct MemoryTag : TagWriteIO {
  std::array<uint8_t,320> memory{};
  uint8_t uid[8]={0x72,0x34,0x07,0x66,0x08,0x01,0x04,0xe0};
  uint8_t locks[80]={}; uint8_t blockSize=4; uint16_t blocks=80;
  bool present=true, securityOk=true;
  int counterWrites=0, failWrite=-1, badRead=-1, corruptWrite=-1, calls=0;
  std::vector<unsigned> written;
  bool identify(uint8_t out[8],uint8_t &size,uint16_t &count) override {
    memcpy(out,uid,8);size=blockSize;count=blocks;return present;
  }
  bool read(const uint8_t address[8],uint8_t block,uint8_t data[4]) override {
    if(!present || memcmp(address,uid,8) || badRead==block) return false;
    memcpy(data,memory.data()+4*block,4);return true;
  }
  bool security(const uint8_t address[8],uint8_t status[80]) override {
    if(memcmp(address,uid,8)) return false;
    memcpy(status,locks,80);return securityOk;
  }
  bool write(const uint8_t address[8],uint8_t block,const uint8_t data[4]) override {
    assert(!memcmp(address,uid,8)); ++calls;
    if(block>=79) { ++counterWrites; return false; } // SLIX2 block 79 = NXP counter
    if(failWrite==calls || !present || locks[block]) return false;
    written.push_back(block);memcpy(memory.data()+4*block,data,4);
    if(corruptWrite==calls) memory[4*block]^=0x80;
    return true;
  }
};
static std::vector<uint8_t> fixture(const char *name) {
  std::ifstream f(std::string("tests/fixtures/official/")+name+".bin",std::ios::binary);
  assert(f.good());return {std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
}
static TagSnapshot snapshot(MemoryTag &tag) {
  TagSnapshot s;assert(inspectTag(tag,s).ok());return s;
}
int main() {
  auto image=fixture("tag_01");
  for(const char *name:{"sample_tag","tag_01","tag_02","tag_03","tag_04","tag_05"}) {
    auto data=fixture(name);MemoryTag tag;auto before=snapshot(tag);
    auto r=writeTag(tag,before,data.data(),data.size(),false);
    if(std::string(name)=="tag_03") { assert(r.code==WriteCode::InvalidImage && tag.calls==0); continue; }
    assert(r.code==WriteCode::Verified && r.mayHaveChanged);
    assert(!memcmp(tag.memory.data(),data.data(),data.size()));
    assert(tag.written.front()==1 && tag.written.back()==1);
    SpoolData parsed;assert(parseType5Tag(tag.memory.data(),TAG_WRITE_BYTES,parsed)==TagParseResult::Valid);
    int calls=tag.calls;auto now=snapshot(tag);
    assert(writeTag(tag,now,data.data(),data.size(),false).code==WriteCode::Unchanged);
    assert(tag.calls==calls);
  }
  { // block 79 is a hardware counter: nonzero counter must not make a blank tag
    // look occupied, and the writer must never touch it
    MemoryTag t;t.memory[316]=0x12;t.memory[319]=0x01;auto s=snapshot(t);assert(s.blank);
    assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::Verified);
    assert(t.counterWrites==0);
    for(unsigned b:t.written) assert(b<79); }
  { // CC capacity above SLIX2 user memory is refused
    auto big=image;big[2]=0x28;MemoryTag t;auto s=snapshot(t);
    assert(writeTag(t,s,big.data(),big.size(),false).code==WriteCode::InvalidImage && t.calls==0); }
  { MemoryTag t;auto s=snapshot(t);t.uid[0]^=1;
    assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::TagChanged);assert(t.calls==0); }
  { MemoryTag t;auto s=snapshot(t);t.memory[70]=1;
    assert(writeTag(t,s,image.data(),image.size(),true).code==WriteCode::TagChanged);assert(t.calls==0); }
  { MemoryTag t;t.memory[100]=1;auto s=snapshot(t);
    assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::OverwriteRequired);assert(t.calls==0);
    assert(writeTag(t,s,image.data(),image.size(),true).ok()); }
  { MemoryTag t;t.memory[0]=0xe1;t.memory[1]=0x43;auto s=snapshot(t);
    assert(writeTag(t,s,image.data(),image.size(),true).code==WriteCode::Locked);assert(t.calls==0); }
  for(int block:{0,1,2,77}) { MemoryTag t;t.locks[block]=1;auto s=snapshot(t);
    assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::Locked);assert(t.calls==0); }
  { MemoryTag t;t.securityOk=false;auto s=snapshot(t);
    assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::SecurityFailed);assert(t.calls==0); }
  { MemoryTag t;t.blockSize=8;TagSnapshot s;assert(inspectTag(t,s).code==WriteCode::UnsupportedTag); }
  { MemoryTag t;t.blocks=64;TagSnapshot s;assert(inspectTag(t,s).code==WriteCode::UnsupportedTag); }
  { MemoryTag t;t.present=false;TagSnapshot s;assert(inspectTag(t,s).code==WriteCode::NoTag); }
  { MemoryTag t;auto s=snapshot(t);t.badRead=20;assert(writeTag(t,s,image.data(),image.size(),false).code==WriteCode::ReadFailed);assert(t.calls==0); }
  MemoryTag complete;auto s=snapshot(complete);auto good=writeTag(complete,s,image.data(),image.size(),false);
  for(unsigned fault=1;fault<=good.writes;++fault) {
    MemoryTag t;auto before=snapshot(t);t.failWrite=int(fault);
    auto r=writeTag(t,before,image.data(),image.size(),false);
    assert(!r.ok() && r.mayHaveChanged && t.calls==int(fault));
    if(fault>1) { SpoolData parsed;assert(parseType5Tag(t.memory.data(),TAG_WRITE_BYTES,parsed)!=TagParseResult::Valid); }
    MemoryTag c;auto snap=snapshot(c);c.corruptWrite=int(fault);
    assert(writeTag(c,snap,image.data(),image.size(),false).code==WriteCode::VerifyFailed);
  }
  for(size_t n=0;n<image.size();++n) { MemoryTag t;auto before=snapshot(t);
    assert(!writeTag(t,before,image.data(),n,false).ok());assert(t.calls==0); }
  for(unsigned pos:{0,1,2,3,4}) { auto invalid=image;invalid[pos]=0;MemoryTag t;auto before=snapshot(t);
    assert(writeTag(t,before,invalid.data(),invalid.size(),false).code==WriteCode::InvalidImage);assert(t.calls==0); }
  std::cout<<"Writer: official images, overwrite, identity, locks, every write interruption and verification: PASS\n";
}
