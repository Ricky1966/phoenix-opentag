#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "tag_parser.h"

class String {
  std::string s;
public:
  String(const char *p=""):s(p) {}
  String(std::string p):s(p) {}
  bool startsWith(const char *p) const { return s.find(p)==0; }
  size_t length() const { return s.size(); }
  int indexOf(char c,int from) const { auto p=s.find(c,from); return p==std::string::npos?-1:int(p); }
  String substring(int from,int to) const { return s.substr(from,to-from); }
  String substring(int from) const { return s.substr(from); }
  char operator[](size_t i) const { return s[i]; }
  bool operator==(const String &b) const { return s==b.s; }
  bool operator!=(const String &b) const { return s!=b.s; }
  const char *c_str() const { return s.c_str(); }
};
static uint32_t clockMs=100;
uint32_t millis() { return clockMs; }
void delay(unsigned) {}
struct SerialMock {
  std::string output;
  void printf(const char *fmt,...) { char b[2048]; va_list args;va_start(args,fmt);vsnprintf(b,sizeof(b),fmt,args);va_end(args);output+=b; }
  void println(const char *s) { output+=s;output+='\n'; }
} Serial;
constexpr int ISO15693_EC_OK=0, EC_NO_CARD=-1;
struct Reader {
  bool present=true;
  uint8_t uid[8]={0x72,0x34,0x07,0x66,0x08,0x01,0x04,0xe0};
  uint8_t memory[320]={}; unsigned writes=0;
  void reset() {}
  bool setupRF() { return true; }
  int getInventory(uint8_t *out) { memcpy(out,uid,8);return present?0:-1; }
  int getSystemInfo(uint8_t *,uint8_t *s,uint16_t *n) { *s=4;*n=80;return 0; }
  int readSingleBlock(uint8_t *,uint8_t b,uint8_t *out,int) { memcpy(out,memory+4*b,4);return 0; }
  int getBlockSecurityStatus(uint8_t *,int,int,uint8_t *out) { memset(out,0,80);return 0; }
  int writeSingleBlock(uint8_t *,uint8_t b,uint8_t *in,int) { ++writes;memcpy(memory+4*b,in,4);return 0; }
} nfc;
SpoolData spool;
#include "nfc_writer.inc"
static void send(const std::string &command) { Serial.output.clear();assert(nfcWriterCommand(command.c_str())); }
static bool has(const char *word) { return Serial.output.find(word)!=std::string::npos; }
int main() {
  std::ifstream f("tests/fixtures/official/tag_01.bin",std::ios::binary);
  std::vector<uint8_t> image{std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>()};
  assert(!image.empty());std::string hex;
  for(uint8_t b:image) { char s[3];snprintf(s,3,"%02x",b);hex+=s; }
  assert(!nfcWriterCommand("show"));
  spool.valid=true;spool.present=true;send("write-info");assert(has("ready") && nfcWriterHolding());assert(!spool.valid && !spool.present);
  std::string ticket=writeTicket.c_str();
  send("write-commit wrong new "+hex);assert(has("ticket_invalid") && !nfc.writes);
  send("write-commit "+ticket+" new "+hex);assert(has("verified\":true") && nfc.writes);
  unsigned writes=nfc.writes;
  send("write-commit "+ticket+" new "+hex);assert(has("ticket_invalid") && nfc.writes==writes);
  assert(writerPoll() && nfcWriterHolding());
  nfc.present=false;writerPoll();writerPoll();assert(nfcWriterHolding());writerPoll();assert(!nfcWriterHolding());
  nfc.present=true;send("write-info");ticket=writeTicket.c_str();clockMs+=60001;
  send("write-commit "+ticket+" replace "+hex);assert(has("ticket_invalid") && nfc.writes==writes);
  send("write-info");ticket=writeTicket.c_str();send("write-commit "+ticket+" new zzzzzzzzzzzzzzzzzzzz");assert(has("invalid_hex"));
  send("write-commit "+ticket+" replace "+hex);assert(has("ticket_invalid") && nfc.writes==writes);
  send("write-info");ticket=writeTicket.c_str();nfc.uid[0]^=1;
  send("write-commit "+ticket+" replace "+hex);assert(has("tag_changed") && nfc.writes==writes);
  send("write-info");send("write-cancel");assert(has("cancelled") && writeTicket.length()==0 && nfcWriterHolding());
  puts("Writer protocol: tickets, expiry, replay, identity, delivery inhibition: PASS");
}
