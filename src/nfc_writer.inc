// Included by nfc.cpp: no second PN5180 instance or SPI initialization.
#include "tag_writer.h"
#include <esp_system.h>

class PnTagWriteIO : public TagWriteIO {
public:
  bool identify(uint8_t uid[8],uint8_t &size,uint16_t &blocks) override {
    nfc.reset();
    return nfc.setupRF() && nfc.getInventory(uid)==ISO15693_EC_OK &&
           nfc.getSystemInfo(uid,&size,&blocks)==ISO15693_EC_OK;
  }
  bool read(const uint8_t uid[8],uint8_t block,uint8_t data[4]) override {
    uint8_t address[8]; memcpy(address,uid,8);
    return nfc.readSingleBlock(address,block,data,4)==ISO15693_EC_OK;
  }
  bool security(const uint8_t uid[8],uint8_t status[80]) override {
    uint8_t address[8]; memcpy(address,uid,8);
    return nfc.getBlockSecurityStatus(address,0,80,status)==ISO15693_EC_OK;
  }
  bool write(const uint8_t uid[8],uint8_t block,const uint8_t data[4]) override {
    uint8_t address[8],value[4]; memcpy(address,uid,8); memcpy(value,data,4);
    const bool ok=nfc.writeSingleBlock(address,block,value,4)==ISO15693_EC_OK;
    delay(20); // SLIX2 programming settle time; yield to ESP32 runtime
    return ok;
  }
};
static PnTagWriteIO writerIO;
static TagSnapshot writeSnapshot;
static String writeTicket;
static uint32_t writeOpened=0;
static bool writerInhibit=false;
static unsigned writerMisses=0;

bool nfcWriterHolding() { return writerInhibit; }
static void writerReply(const WriteResult &r) {
  Serial.printf("PHOENIX-WRITE {\"status\":\"%s\",\"verified\":%s,\"may_have_changed\":%s,\"block\":%d,\"writes\":%u}\n",
    writeCodeName(r.code),r.ok()?"true":"false",r.mayHaveChanged?"true":"false",r.block,r.writes);
}
static void writerError(const char *code) {
  Serial.printf("PHOENIX-WRITE {\"status\":\"%s\",\"verified\":false}\n",code);
}
static int hexDigit(char c) {
  if(c>='0' && c<='9') return c-'0';
  if(c>='a' && c<='f') return c-'a'+10;
  if(c>='A' && c<='F') return c-'A'+10;
  return -1;
}
bool nfcWriterCommand(const char *input) {
  String cmd(input);
  if(!cmd.startsWith("write-")) return false;
  if(cmd=="write-info") {
    writeTicket="";
    writerInhibit=true; writerMisses=0;
    spool.valid=false; spool.present=false; // cancel pending activation/delivery
    const WriteResult r=inspectTag(writerIO,writeSnapshot);
    if(!r.ok()) { writerReply(r); return true; }
    char ticket[25];
    snprintf(ticket,sizeof(ticket),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
    writeTicket=ticket; writeOpened=millis();
    Serial.printf("PHOENIX-WRITE {\"status\":\"ready\",\"ticket\":\"%s\",\"expires_ms\":60000,\"uid\":\"",ticket);
    for(int i=7;i>=0;--i) Serial.printf("%02X",writeSnapshot.uid[i]);
    Serial.printf("\",\"capacity\":316,\"block_size\":4,\"blank\":%s,\"memory_hex\":\"",writeSnapshot.blank?"true":"false");
    for(uint8_t b:writeSnapshot.memory) Serial.printf("%02X",b);
    Serial.println("\"}");
    return true;
  }
  if(cmd=="write-cancel") {
    writeTicket="";
    Serial.println("PHOENIX-WRITE {\"status\":\"cancelled\",\"remove_tag_to_resume\":true}");
    return true;
  }
  // write-commit <ticket> <new|replace> <full-image-hex>
  if(!cmd.startsWith("write-commit ")) { writerError("unknown_command"); return true; }
  const int first=cmd.indexOf(' ',13), second=first<0?-1:cmd.indexOf(' ',first+1);
  if(first<0 || second<0) { writerError("invalid_request"); return true; }
  const String ticket=cmd.substring(13,first), mode=cmd.substring(first+1,second);
  if(writeTicket.length()==0 || ticket!=writeTicket || uint32_t(millis()-writeOpened)>60000) {
    writerError("ticket_invalid_or_expired"); return true;
  }
  writeTicket=""; // single-use even on error; no replay of destructive commands
  if(mode!="new" && mode!="replace") { writerError("invalid_mode"); return true; }
  const String hex=cmd.substring(second+1);
  if(hex.length()<20 || hex.length()>2*TAG_WRITE_BYTES || hex.length()%2) { writerError("invalid_image"); return true; }
  uint8_t image[TAG_WRITE_BYTES]={};
  for(size_t i=0;i<hex.length()/2;++i) {
    int hi=hexDigit(hex[2*i]),lo=hexDigit(hex[2*i+1]);
    if(hi<0 || lo<0) { writerError("invalid_hex"); return true; }
    image[i]=uint8_t((hi<<4)|lo);
  }
  writerReply(writeTag(writerIO,writeSnapshot,image,hex.length()/2,mode=="replace"));
  // Keep delivery inhibited until three empty inventories; do not auto-create
  // or activate this spool before a future Spoolman caller links its UID.
  return true;
}
static bool writerPoll() {
  if(!writerInhibit) return false;
  if(writeTicket.length() && uint32_t(millis()-writeOpened)<=60000) return true;
  writeTicket="";
  nfc.reset(); uint8_t uid[8];
  if(nfc.setupRF() && nfc.getInventory(uid)==EC_NO_CARD) {
    if(++writerMisses>=3) { writerInhibit=false; writerMisses=0; }
  } else writerMisses=0;
  return true;
}
