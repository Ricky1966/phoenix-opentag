#include "tag_parser.h"
#include "delivery.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>
#include <random>
#include <fstream>
using Bytes=std::vector<uint8_t>;

Bytes wrap(const Bytes &payload,bool shortRecord=true,bool extra=false,bool extendedCC=false) {
  const char *mime="application/vnd.openprinttag";
  Bytes message;
  if(extra) message={0x91,1,2,'T',0,0}; // unrelated first NDEF record
  message.push_back(uint8_t((extra?0x40:0xc0)|2|(shortRecord?0x10:0)));
  message.push_back(uint8_t(strlen(mime)));
  if(!shortRecord) { message.push_back(0); message.push_back(0); message.push_back(uint8_t(payload.size()>>8)); }
  message.push_back(uint8_t(payload.size()));
  message.insert(message.end(),mime,mime+strlen(mime));
  message.insert(message.end(),payload.begin(),payload.end());
  Bytes out=extendedCC?Bytes{0xe2,0x40,0,0,0,0,1,0}:Bytes{0xe1,0x40,0x40,0};
  if(extra) { out.push_back(0);out.push_back(0xfd);out.push_back(1);out.push_back(0); }
  out.push_back(3);
  if(message.size()>=255) { out.push_back(255);out.push_back(uint8_t(message.size()>>8)); }
  out.push_back(uint8_t(message.size()));
  out.insert(out.end(),message.begin(),message.end());
  return out;
}
SpoolData decode(const Bytes &b) {
  SpoolData s; s.present=true; strcpy(s.uid,"E0:04:01:02:03:04:05:06");
  assert(parseType5Tag(b.data(),b.size(),s)==TagParseResult::Valid);
  assert(s.valid && s.present); return s;
}
void reject(const Bytes &b) {
  SpoolData s; strcpy(s.brand,"unchanged");
  assert(parseType5Tag(b.data(),b.size(),s)!=TagParseResult::Valid);
  assert(strcmp(s.brand,"unchanged")==0 && !s.valid);
}
int main() {
  std::ifstream fixture("tests/fixtures/openprinttag_sample.hex");
  std::string hex; fixture>>hex; assert(!hex.empty());
  Bytes official;
  for(size_t i=0;i<hex.size();i+=2) official.push_back(uint8_t(std::stoul(hex.substr(i,2),nullptr,16)));
  auto sample=decode(official);
  assert(strcmp(sample.brand,"Prusament")==0);
  assert(strcmp(sample.material,"PLA Galaxy Black")==0);
  assert(sample.weight==1000 && sample.actualWeight==1012);

  // Empty meta + main map: class FFF, display name, density integer, half diameter,
  // and fractional nominal weight (all valid encodings in the spec).
  Bytes main={0xa5,8,0,10,0x63,'P','L','A',0x18,29,1,0x18,30,0xf9,0x3f,0,16,0xf9,0x57,0xe0};
  Bytes payload={0xa0}; payload.insert(payload.end(),main.begin(),main.end());
  auto tag=wrap(payload); auto s=decode(tag);
  assert(strcmp(s.material,"PLA")==0 && s.diameter==1.75f && s.density==1 && s.weight==126);
  decode(wrap(payload,false,true,true));
  // Every truncated prefix of a valid tag is rejected, without changing output.
  for(size_t n=0;n<tag.size();++n) reject(Bytes(tag.begin(),tag.begin()+n));
  reject({0xe1,0,0,0,3,255}); // old NDEF overflow
  reject(wrap({0xa0,0xa2,8,0,10,0x78,0x18,'A'})); // old CBOR overflow
  reject(wrap({0xa0,0xa2,8,0,10,1})); // wrong type for text
  reject(wrap({0xa0,0xa2,8,0,0x18,30,0xf9,0x7c,0})); // infinity
  reject(wrap({0xa0,0xa2,8,0,0x18,30,0xf9,0x7e,0})); // NaN
  reject(wrap({0xa0,0xa2,8,0,0x18,30,0x20})); // negative diameter
  reject(wrap({0xa0,0xa2,8,0,8,0})); // duplicate key
  reject(wrap({0xa0,0xa2,8,0,10,0x62,0xc0,0x80})); // invalid UTF8
  reject(wrap({0xa0,0xa1,8,1})); // SLA must not auto-activate on Phoenix
  reject(wrap({0xa0,0xa0})); // missing required material class
  // Metadata explicit offsets, main padding and auxiliary region.
  Bytes regions={0xa3,0,10,1,8,2,18}; regions.resize(10,0);
  regions.insert(regions.end(),{0xa1,8,0}); regions.resize(18,0); regions.push_back(0xbf);regions.push_back(0xff);regions.resize(50,0);
  decode(wrap(regions,false));
  regions[2]=1;reject(wrap(regions)); // overlapping meta
  // Unknown recursive value, semantic tag, indefinite array, indefinite root map.
  decode(wrap({0xa0,0xbf,8,0,0x18,99,0xd8,42,0x9f,0xa1,0,1,0xff,0xff}));
  reject(wrap({0xa0,0xbf,8,0,0x18,99,0x9f}));
  // Complete message >255 bytes and data beyond the former 160-byte limit.
  Bytes large={0xa0,0xa2,8,0,0x18,99,0x59,1,0};large.resize(large.size()+256,0x55);
  decode(wrap(large,false));
  auto wrongMime=tag;wrongMime[9]='z';reject(wrongMime);
  auto wrongLen=tag;wrongLen[5]--;reject(wrongLen);
  auto cf=tag;cf[6]|=0x20;reject(cf);
  // JSON escaping and typed/omitted fields; stable fingerprint excludes time.
  strcpy(s.brand,"A\"B\\C\n");s.lastReadMs=100;
  const auto json=spoolJson(s,"reader","test");
  assert(json.find("E004010203040506")!=std::string::npos);
  assert(json.find("A\\\"B\\\\C\\u000a")!=std::string::npos);
  assert(json.find("nan")==std::string::npos);
  assert(json.find("actual_netto_full_weight")==std::string::npos);
  DeliveryState d; d.observe(s,json);assert(d.ready(0));d.finish(0,false);
  assert(!d.ready(4999)&&d.ready(5000));d.finish(5000,true);
  d.observe(s,json);assert(!d.ready(10000));
  s.valid=false;d.observe(s,"");assert(!d.ready(100000));
  s.valid=true;d.observe(s,json);assert(!d.ready(100000)); // transient failure no duplicate
  s.present=false;d.observe(s,"");s.present=true;d.observe(s,json);assert(d.ready(100000));
  d.finish(UINT32_MAX-1000,false);assert(!d.ready(3000)&&d.ready(4000));
  // A new spool replaces failed pending work; never deliver old offline backlog.
  d.observe(s,json+" ");assert(d.ready(4001));
  // Deterministic malformed-input smoke fuzzing under ASan/UBSan.
  std::mt19937 rng(5180);
  for(int k=0;k<20000;++k) {
    Bytes b=tag;
    for(unsigned j=0;j<1+rng()%6;++j) b[rng()%b.size()]=uint8_t(rng());
    SpoolData output;parseType5Tag(b.data(),b.size(),output);
    Bytes raw(rng()%600); for(auto &x:raw)x=uint8_t(rng());
    parseType5Tag(raw.data(),raw.size(),output);
  }
  std::cout<<"Parser, delivery and 40000 malformed-input cases: PASS\n";
}
