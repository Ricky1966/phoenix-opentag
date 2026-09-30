#include "tag_parser.h"
#include <cstring>
#include <cstdio>
#include <climits>

namespace {
constexpr size_t MAX_REGION = 512;
struct Head { unsigned type, ai; uint64_t value; bool indefinite; };
struct Cursor {
  const uint8_t *p;
  size_t n, i = 0;
  Cursor(const uint8_t *data, size_t size) : p(data), n(size) {}
  bool take(size_t count) { if (count > n - i) return false; i += count; return true; }
  bool head(Head &h) {
    if (i == n) return false;
    const uint8_t b = p[i++];
    h.type = b >> 5; h.ai = b & 31; h.indefinite = h.ai == 31; h.value = 0;
    if (h.ai < 24) { h.value = h.ai; return true; }
    if (h.indefinite) return h.type >= 2 && h.type <= 5;
    if (h.ai > 27) return false;
    const size_t count = size_t(1) << (h.ai - 24);
    if (count > n - i) return false;
    for (size_t k = 0; k < count; ++k) h.value = (h.value << 8) | p[i++];
    return true;
  }
  bool skip(unsigned depth = 0) {
    if (depth > 8) return false;
    Head h;
    if (!head(h)) return false;
    if (h.type == 2 || h.type == 3) {
      if (!h.indefinite) return h.value <= n-i && take(size_t(h.value));
      while (i < n && p[i] != 0xff) {
        Head part;
        if (!head(part) || part.type != h.type || part.indefinite || part.value > n-i || !take(size_t(part.value))) return false;
      }
      return i < n && take(1);
    }
    if (h.type == 4 || h.type == 5) {
      uint64_t left = h.value;
      if (!h.indefinite && left > n-i) return false;
      while (h.indefinite ? (i < n && p[i] != 0xff) : left-- > 0) {
        if (!skip(depth+1) || (h.type == 5 && !skip(depth+1))) return false;
      }
      return !h.indefinite || (i < n && take(1));
    }
    return h.type != 6 || skip(depth+1);
  }
};

bool utf8(const uint8_t *p, size_t n) {
  for (size_t i=0; i<n;) {
    uint32_t cp = p[i++]; unsigned count = 0; uint32_t minimum = 0;
    if (cp < 0x80) { if (!cp) return false; continue; }
    if (cp >= 0xc2 && cp <= 0xdf) { count=1; cp &= 31; minimum=0x80; }
    else if (cp >= 0xe0 && cp <= 0xef) { count=2; cp &= 15; minimum=0x800; }
    else if (cp >= 0xf0 && cp <= 0xf4) { count=3; cp &= 7; minimum=0x10000; }
    else return false;
    if (count > n-i) return false;
    while (count--) { uint8_t b=p[i++]; if ((b&0xc0)!=0x80) return false; cp=(cp<<6)|(b&63); }
    if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return false;
  }
  return true;
}
bool text(Cursor &c, char *out, size_t cap) {
  Head h;
  if (!c.head(h) || h.type!=3 || h.indefinite || h.value>=cap || h.value>c.n-c.i) return false;
  const size_t len=size_t(h.value);
  if (!utf8(c.p+c.i,len)) return false;
  memcpy(out,c.p+c.i,len); out[len]=0; return c.take(len);
}
bool number(Cursor &c, float &value) {
  Head h;
  if (!c.head(h) || h.indefinite) return false;
  if (h.type==0) value=float(h.value);
  else if (h.type==1) value=float(-1.0-double(h.value));
  else if (h.type==7 && h.ai==25) {
    const unsigned bits=unsigned(h.value), e=(bits>>10)&31, m=bits&1023;
    if (e==31) return false;
    value=e ? std::ldexp(float(1024+m),int(e)-25) : std::ldexp(float(m),-24);
    if (bits&0x8000) value=-value;
  } else if (h.type==7 && h.ai==26) {
    uint32_t bits=uint32_t(h.value); memcpy(&value,&bits,4);
  } else if (h.type==7 && h.ai==27) {
    double d; memcpy(&d,&h.value,8); value=float(d);
  } else return false;
  return std::isfinite(value);
}
bool positive(Cursor &c, float &v, bool zero=false) {
  return number(c,v) && (zero ? v>=0 : v>0);
}
bool integer(Cursor &c, int &v, int max) {
  Head h;
  if (!c.head(h) || h.type!=0 || h.value>uint64_t(max)) return false;
  v=int(h.value); return true;
}
struct Map {
  Head h{}; uint64_t remaining=0; bool ended=false;
  bool begin(Cursor &c) { if (!c.head(h)||h.type!=5) return false; remaining=h.value; return true; }
  bool next(Cursor &c, uint64_t &key) {
    if (ended) return false;
    if (h.indefinite && c.i<c.n && c.p[c.i]==0xff) { ++c.i; ended=true; return false; }
    if (!h.indefinite && !remaining) { ended=true; return false; }
    Head k;
    if (!c.head(k)||k.type!=0) return false;
    key=k.value; if (!h.indefinite) --remaining; return true;
  }
};
bool payload(const uint8_t *p,size_t len,SpoolData &s) {
  Cursor meta(p,len); Map map;
  if (!map.begin(meta)) return false;
  size_t region[4]={0,0,0,0}; bool has[4]={false,false,false,false};
  uint64_t key;
  while (map.next(meta,key)) {
    if (key<4) {
      Head h;
      if (has[key] || !meta.head(h) || h.type!=0 || h.value>len) return false;
      has[key]=true; region[key]=size_t(h.value);
    } else if (!meta.skip()) return false;
  }
  if (!map.ended || meta.i>MAX_REGION || (has[3]&&!has[2])) return false;
  const size_t start=has[0]?region[0]:meta.i;
  if (start<meta.i || start>=len) return false;
  const size_t aux=has[2]?region[2]:len;
  if (has[2] && (aux<meta.i || aux>=len || aux==start)) return false;
  const size_t end=has[1] ? start+region[1] : (aux>start ? aux : len);
  if (end>len || end<=start || end-start>MAX_REGION || (aux>start && aux<end)) return false;
  if (has[2]) {
    const size_t auxEnd=has[3]?aux+region[3]:(start>aux?start:len);
    if (auxEnd>len || auxEnd<=aux || auxEnd-aux>MAX_REGION || (aux<start && auxEnd>start)) return false;
  }
  Cursor main(p+start,end-start); Map fields;
  if (!fields.begin(main)) return false;
  uint64_t seen=0;
  while (fields.next(main,key)) {
    if (key<64) { const uint64_t mask=uint64_t(1)<<key; if (seen&mask) return false; seen|=mask; }
    switch (key) {
      case 0: {
        Head h;
        if (!main.head(h)||h.type!=2||h.indefinite||h.value!=16||16>main.n-main.i) return false;
        size_t pos=0;
        for (size_t k=0;k<16;++k) {
          if (k==4||k==6||k==8||k==10) s.instanceUuid[pos++]='-';
          snprintf(s.instanceUuid+pos,3,"%02x",main.p[main.i+k]); pos+=2;
        }
        main.take(16); break;
      }
      case 8: if (!integer(main,s.materialClass,65535)) return false; break;
      case 9: if (!integer(main,s.materialType,65535)) return false; break;
      case 10: if (!text(main,s.material,sizeof(s.material))) return false; break;
      case 11: if (!text(main,s.brand,sizeof(s.brand))) return false; break;
      case 55: if (!text(main,s.country,sizeof(s.country)) || strlen(s.country)!=2) return false; break;
      case 16: if (!positive(main,s.weight,true)) return false; break;
      case 17: if (!positive(main,s.actualWeight,true)) return false; break;
      case 18: if (!positive(main,s.emptyWeight,true)) return false; break;
      case 29: if (!positive(main,s.density)) return false; break;
      case 30: if (!positive(main,s.diameter)) return false; break;
      case 34: if (!integer(main,s.nozzleMin,INT_MAX)) return false; break;
      case 35: if (!integer(main,s.nozzleMax,INT_MAX)) return false; break;
      case 37: if (!integer(main,s.bedMin,INT_MAX)) return false; break;
      case 38: if (!integer(main,s.bedMax,INT_MAX)) return false; break;
      case 19: {
        Head h;
        if (!main.head(h)||h.type!=2||h.indefinite||(h.value!=3&&h.value!=4)||h.value>main.n-main.i) return false;
        memcpy(s.color,main.p+main.i,size_t(h.value)); main.take(size_t(h.value)); s.hasColor=true; break;
      }
      default: if (!main.skip()) return false;
    }
  }
  if (!fields.ended || s.materialClass!=0) return false; // Phoenix accepts FFF only
  if (s.nozzleMin>=0 && s.nozzleMax>=0 && s.nozzleMin>s.nozzleMax) return false;
  if (s.bedMin>=0 && s.bedMax>=0 && s.bedMin>s.bedMax) return false;
  if (std::isnan(s.diameter)) s.diameter=1.75f; // OpenPrintTag default for FFF
  s.valid=true; return true;
}
TagParseResult message(const uint8_t *p,size_t n,SpoolData &out) {
  Cursor c(p,n); bool first=true, ended=false, found=false; SpoolData parsed;
  while(c.i<c.n) {
    if(c.n-c.i<3) return TagParseResult::Invalid;
    const uint8_t hdr=c.p[c.i++], typeLen=c.p[c.i++];
    if (bool(hdr&0x80)!=first || (hdr&0x20) || (hdr&7)>5) return TagParseResult::Invalid;
    first=false;
    const size_t lenBytes=(hdr&0x10)?1:4;
    if(c.n-c.i<lenBytes) return TagParseResult::Invalid;
    uint32_t dataLen=0;
    for(size_t k=0;k<lenBytes;++k) dataLen=(dataLen<<8)|c.p[c.i++];
    size_t idLen=0;
    if(hdr&8) { if(c.i==c.n) return TagParseResult::Invalid; idLen=c.p[c.i++]; }
    if(typeLen>c.n-c.i) return TagParseResult::Invalid;
    constexpr char mime[]="application/vnd.openprinttag";
    const bool match=(hdr&7)==2 && typeLen==sizeof(mime)-1 && memcmp(c.p+c.i,mime,typeLen)==0;
    if(!c.take(typeLen)||!c.take(idLen)||dataLen>c.n-c.i) return TagParseResult::Invalid;
    if(match) {
      if(found || !payload(c.p+c.i,dataLen,parsed)) return TagParseResult::Invalid;
      found=true;
    }
    c.take(dataLen);
    if(hdr&0x40) { ended=true; break; }
  }
  if(!ended||c.i!=c.n) return TagParseResult::Invalid;
  if(!found) return TagParseResult::NotOpenPrintTag;
  // Keep the separately acquired physical identity, publish atomically.
  memcpy(parsed.uid,out.uid,sizeof(parsed.uid)); parsed.present=out.present;
  parsed.lastReadMs=out.lastReadMs; out=parsed;
  return TagParseResult::Valid;
}
}
TagParseResult parseType5Tag(const uint8_t *data,size_t size,SpoolData &out) {
  if(!data||!size) return TagParseResult::Incomplete;
  // Both standard Type-5 CC layouts. No raw-CBOR fallback.
  if(data[0]!=0xe1 && data[0]!=0xe2) return TagParseResult::NotOpenPrintTag;
  Cursor c(data,size);
  if(!c.take(data[0]==0xe1?4:8)) return TagParseResult::Incomplete;
  while(c.i<c.n) {
    const uint8_t type=c.p[c.i++];
    if(type==0) continue;
    if(type==0xfe) return TagParseResult::NotOpenPrintTag;
    if(c.i==c.n) return TagParseResult::Incomplete;
    size_t len=c.p[c.i++];
    if(len==255) {
      if(c.n-c.i<2) return TagParseResult::Incomplete;
      len=(size_t(c.p[c.i])<<8)|c.p[c.i+1]; c.i+=2;
    }
    if(len>c.n-c.i) return TagParseResult::Incomplete;
    if(type==3) return message(c.p+c.i,len,out);
    c.take(len);
  }
  return TagParseResult::Incomplete;
}
