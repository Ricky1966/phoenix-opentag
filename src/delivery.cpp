#include "delivery.h"
#include <cstdio>

namespace {
std::string quote(const char *s) {
  std::string out="\"";
  for(const unsigned char *p=reinterpret_cast<const unsigned char *>(s);*p;++p) {
    if (*p=='"' || *p=='\\') { out+='\\'; out+=char(*p); }
    else if(*p<32) { char b[7]; snprintf(b,sizeof(b),"\\u%04x",*p); out+=b; }
    else out+=char(*p);
  }
  return out+'"';
}
void stringField(std::string &out,const char *key,const char *s) {
  if(*s) out+=","+quote(key)+":"+quote(s);
}
void numberField(std::string &out,const char *key,float value) {
  if (!std::isfinite(value)) return;
  char b[32]; snprintf(b,sizeof(b),"%.9g",double(value));
  out+=","+quote(key)+":"+b;
}
}
std::string spoolJson(const SpoolData &s,const char *reader,const char *firmware) {
  if (!s.present || !s.valid || !*s.uid) return "";
  std::string out="{\"schema_version\":1,\"reader_id\":"+quote(reader)+",\"firmware\":"+quote(firmware);
  // Canonical NFC-V UID: uppercase, MSB first, no separators. Serial stays familiar.
  char uid[25]; size_t n=0;
  for(const char *p=s.uid;*p && n<sizeof(uid)-1;++p) if(*p!=':') uid[n++]=*p;
  uid[n]=0;
  stringField(out,"uid",uid);
  stringField(out,"instance_uuid",s.instanceUuid);
  stringField(out,"material_name",s.material);
  stringField(out,"brand_name",s.brand);
  stringField(out,"country_of_origin",s.country);
  numberField(out,"material_class",float(s.materialClass));
  if(s.materialType>=0) numberField(out,"material_type",float(s.materialType));
  numberField(out,"filament_diameter",s.diameter);
  numberField(out,"density",s.density);
  numberField(out,"nominal_netto_full_weight",s.weight);
  numberField(out,"actual_netto_full_weight",s.actualWeight);
  numberField(out,"empty_container_weight",s.emptyWeight);
  if(s.nozzleMin>=0) numberField(out,"min_print_temperature",float(s.nozzleMin));
  if(s.nozzleMax>=0) numberField(out,"max_print_temperature",float(s.nozzleMax));
  if(s.bedMin>=0) numberField(out,"min_bed_temperature",float(s.bedMin));
  if(s.bedMax>=0) numberField(out,"max_bed_temperature",float(s.bedMax));
  if(s.hasColor) { char color[10]; snprintf(color,sizeof(color),"#%02X%02X%02X%02X",s.color[0],s.color[1],s.color[2],s.color[3]); stringField(out,"color_rgba",color); }
  return out+"}";
}
