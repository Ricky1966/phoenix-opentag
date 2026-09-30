// Prova sul PC di src/opt_parse.cpp contro i tag di esempio ufficiali OpenPrintTag
// (vectors/*.bin, con i valori attesi in *.expect generati dall'implementazione di
// riferimento di Prusa). Compilato con AddressSanitizer: un accesso fuori dai limiti
// e' un errore, oltre che una verifica sbagliata.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../src/opt_parse.h"

static int failures = 0;

#define CHECK(cond, ...)                                  \
  do {                                                    \
    if (!(cond)) {                                        \
      failures++;                                         \
      printf("  FAIL: ");                                 \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static std::vector<uint8_t> readFile(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::map<std::string, std::string> readExpect(const std::string &path) {
  std::map<std::string, std::string> m;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    size_t eq = line.find('=');
    if (eq != std::string::npos) m[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return m;
}

static std::string colorHex(const SpoolData &s) {
  char b[8];
  snprintf(b, sizeof b, "%02X%02X%02X", s.color[0], s.color[1], s.color[2]);
  return b;
}

static void checkVector(const std::string &dir, const std::string &name) {
  printf("%s\n", name.c_str());
  std::vector<uint8_t> mem = readFile(dir + "/" + name + ".bin");
  auto exp = readExpect(dir + "/" + name + ".expect");
  SpoolData s;
  CHECK(optParse(mem.data(), (int)mem.size(), &s), "payload non trovato");
  CHECK(s.optTag, "record MIME OpenPrintTag non riconosciuto");

  auto str = [&](const char *k, const char *got) {
    auto it = exp.find(k);
    std::string want = it == exp.end() ? "" : it->second;
    CHECK(want == got, "%s: atteso '%s', letto '%s'", k, want.c_str(), got);
  };
  auto num = [&](const char *k, int got) {
    auto it = exp.find(k);
    int want = it == exp.end() ? 0 : atoi(it->second.c_str());
    CHECK(want == got, "%s: atteso %d, letto %d", k, want, got);
  };
  auto flt = [&](const char *k, float got) {
    auto it = exp.find(k);
    if (it == exp.end()) { CHECK(std::isnan(got), "%s: atteso assente, letto %f", k, got); return; }
    float want = (float)atof(it->second.c_str());
    CHECK(std::fabs(want - got) < 0.005f, "%s: atteso %f, letto %f", k, want, got);
  };
  str("material", s.material);
  str("brand", s.brand);
  str("country", s.country);
  num("nozzleMin", s.nozzleMin);
  num("nozzleMax", s.nozzleMax);
  num("bedMin", s.bedMin);
  num("bedMax", s.bedMax);
  num("weight", s.weight);
  flt("density", s.density);
  flt("diameter", s.diameter);
  auto c = exp.find("color");
  if (c == exp.end()) CHECK(!s.hasColor, "colore letto ma non atteso");
  else CHECK(s.hasColor && colorHex(s) == c->second, "color: atteso %s, letto %s", c->second.c_str(), colorHex(s).c_str());
}

// Nessun input, per quanto malformato, deve leggere fuori dai limiti (lo intercetta
// AddressSanitizer): tag troncato a ogni lunghezza e con un byte alterato.
static void robustness(const std::string &dir, const std::string &name) {
  std::vector<uint8_t> mem = readFile(dir + "/" + name + ".bin");
  for (size_t n = 0; n <= mem.size(); n++) {
    std::vector<uint8_t> cut(mem.begin(), mem.begin() + n);   // buffer di dimensione esatta
    SpoolData s;
    optParse(cut.data(), (int)cut.size(), &s);
  }
  unsigned seed = 12345;
  for (int round = 0; round < 3000; round++) {
    std::vector<uint8_t> m = mem;
    int flips = 1 + round % 4;
    for (int k = 0; k < flips; k++) {
      seed = seed * 1103515245u + 12345u;
      m[(seed >> 8) % m.size()] = (uint8_t)(seed >> 3);
    }
    SpoolData s;
    optParse(m.data(), (int)m.size(), &s);
  }
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "vectors";
  const char *names[] = {"sample_tag", "tag_01", "tag_02", "tag_03", "tag_04", "tag_05"};
  for (const char *n : names) checkVector(dir, n);

  printf("tag legacy senza sezione meta (formato Odin) e con meta vuota\n");
  // CC E1, TLV 03 len, record corto MB|ME|SR TNF=2, tipo MIME, poi la mappa dei dati.
  const uint8_t mime[] = "application/vnd.openprinttag";
  const uint8_t data[] = {0xA3, 0x0A, 0x63, 'P', 'L', 'A', 0x0B, 0x63, 'X', 'Y', 'Z', 0x10, 0x19, 0x03, 0xE8};
  for (int withMeta = 0; withMeta < 2; withMeta++) {
    std::vector<uint8_t> rec = {0xD2, 27, (uint8_t)(sizeof data + withMeta)};
    rec.insert(rec.end(), mime, mime + 27);
    if (withMeta) rec.push_back(0xA0);   // meta vuota
    rec.insert(rec.end(), data, data + sizeof data);
    std::vector<uint8_t> mem = {0xE1, 0x40, 0x10, 0x01, 0x03, (uint8_t)rec.size()};
    mem.insert(mem.end(), rec.begin(), rec.end());
    mem.push_back(0xFE);
    SpoolData s;
    CHECK(optParse(mem.data(), (int)mem.size(), &s), "legacy meta=%d: non letto", withMeta);
    CHECK(strcmp(s.material, "PLA") == 0 && strcmp(s.brand, "XYZ") == 0 && s.weight == 1000,
          "legacy meta=%d: material='%s' brand='%s' weight=%d", withMeta, s.material, s.brand, s.weight);
  }

  for (const char *n : names) robustness(dir, n);
  printf("robustezza: troncamenti e alterazioni senza accessi fuori limite\n");

  printf(failures ? "\n%d verifiche FALLITE\n" : "\nTutte le verifiche superate\n", failures);
  return failures ? 1 : 0;
}
