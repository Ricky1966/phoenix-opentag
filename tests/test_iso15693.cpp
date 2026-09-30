#include <PN5180ISO15693.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

// Simulated RF responses, real ISO15693 protocol implementation under test.
static std::vector<uint8_t> reply;
static bool ioOk=true;
PN5180::PN5180(uint8_t,uint8_t,uint8_t) {}
bool PN5180::sendData(uint8_t*,int,uint8_t) { return ioOk; }
bool PN5180::readRegister(uint8_t reg,uint32_t *v) {
  if(!ioOk) return false;
  *v=reg==RX_STATUS ? uint32_t(reply.size()) : RX_SOF_DET_IRQ_STAT|RX_IRQ_STAT;
  return true;
}
uint8_t *PN5180::readData(int,uint8_t*) { return reply.data(); }
bool PN5180::clearIRQStatus(uint32_t) { return true; }
bool PN5180::loadRFConfig(uint8_t,uint8_t) { return ioOk; }
bool PN5180::setRF_on() { return ioOk; }
bool PN5180::writeRegisterWithAndMask(uint8_t,uint32_t) { return ioOk; }
bool PN5180::writeRegisterWithOrMask(uint8_t,uint32_t) { return ioOk; }
int main() {
  PN5180ISO15693 nfc(5,16,17);
  uint8_t uid[8]={}, size=0;
  uint16_t blocks=0;
  reply={0,4,1,2,3,4,5,6,7,8,255,31};
  assert(nfc.getSystemInfo(uid,&size,&blocks)==ISO15693_EC_OK);
  assert(size==32 && blocks==256);
  uint8_t narrow=0;
  assert(nfc.getSystemInfo(uid,&size,&narrow)!=ISO15693_EC_OK);
  reply.resize(11);
  assert(nfc.getSystemInfo(uid,&size,&blocks)!=ISO15693_EC_OK);
  reply={0,0,1,2,3,4,5,6,7,8}; // absent memory geometry
  assert(nfc.getSystemInfo(uid,&size,&blocks)!=ISO15693_EC_OK);
  reply={0,0};
  assert(nfc.getInventory(uid)!=ISO15693_EC_OK);
  uint8_t data[32];memset(data,0xaa,sizeof(data));
  reply={0,1,2};
  assert(nfc.readSingleBlock(uid,0,data,32)!=ISO15693_EC_OK);
  assert(data[0]==0xaa);
  reply.resize(33,0x55);reply[0]=0;
  assert(nfc.readSingleBlock(uid,255,data,32)==ISO15693_EC_OK);
  assert(data[31]==0x55);
  assert(nfc.readSingleBlock(uid,0,data,33)!=ISO15693_EC_OK);
  reply={1}; // error response missing its error-code byte
  assert(nfc.getInventory(uid)!=ISO15693_EC_OK);
  ioOk=false;
  assert(nfc.getInventory(uid)==ISO15693_EC_UNKNOWN_ERROR);
  assert(!nfc.setupRF());
  std::cout<<"ISO15693: 256 blocks, 32-byte blocks, short replies, I/O failure: PASS\n";
}
