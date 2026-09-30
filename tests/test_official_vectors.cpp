// Prova di src/tag_parser.cpp sui tag di esempio ufficiali della specifica OpenPrintTag
// (tests/fixtures/official/*.bin, licenza MIT di Prusa Research: vedi LICENSE.openprinttag).
// I valori attesi (*.expect) sono stati generati con la loro implementazione di
// riferimento in Python. Con AddressSanitizer: un accesso fuori dai limiti e' un errore.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "tag_parser.h"

static int failures = 0;

#define CHECK(cond, ...)             \
  do {                               \
    if (!(cond)) {                   \
      failures++;                    \
      printf("  FAIL: ");            \
      printf(__VA_ARGS__);           \
      printf("\n");                  \
    }                                \
  } while (0)

static std::vector<uint8_t> readFile(const std::string &path) {
  std::ifstream f(path.c_str(), std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static std::map<std::string, std::string> readExpect(const std::string &path) {
  std::map<std::string, std::string> m;
  std::ifstream f(path.c_str());
  std::string line;
  while (std::getline(f, line)) {
    size_t eq = line.find('=');
    if (eq != std::string::npos) m[line.substr(0, eq)] = line.substr(eq + 1);
  }
  return m;
}

struct Vector {
  const char *name;
  TagParseResult result;  // tag_03 ha la temperatura massima sotto la minima: il parser lo rifiuta di proposito
};

static void checkVector(const std::string &dir, const Vector &v) {
  printf("%s\n", v.name);
  std::vector<uint8_t> mem = readFile(dir + "/" + v.name + ".bin");
  std::map<std::string, std::string> exp = readExpect(dir + "/" + v.name + ".expect");
  SpoolData s;
  TagParseResult r = parseType5Tag(mem.data(), mem.size(), s);
  CHECK(r == v.result, "risultato %d, atteso %d", (int)r, (int)v.result);
  if (v.result != TagParseResult::Valid) return;

  std::map<std::string, std::string>::const_iterator it;
  CHECK(exp["material"] == s.material, "material: atteso '%s', letto '%s'", exp["material"].c_str(), s.material);
  CHECK(exp["brand"] == s.brand, "brand: atteso '%s', letto '%s'", exp["brand"].c_str(), s.brand);
  CHECK(exp["country"] == s.country, "country: atteso '%s', letto '%s'", exp["country"].c_str(), s.country);

  struct Int { const char *key; int got; };
  const Int ints[] = {{"nozzleMin", s.nozzleMin}, {"nozzleMax", s.nozzleMax}, {"bedMin", s.bedMin}, {"bedMax", s.bedMax}};
  for (size_t i = 0; i < sizeof ints / sizeof ints[0]; i++) {
    it = exp.find(ints[i].key);
    int want = it == exp.end() ? -1 : atoi(it->second.c_str());   // -1 = campo assente
    CHECK(want == ints[i].got, "%s: atteso %d, letto %d", ints[i].key, want, ints[i].got);
  }

  struct Flt { const char *key; float got; };
  const Flt flts[] = {{"weight", s.weight}, {"density", s.density}, {"diameter", s.diameter}};
  for (size_t i = 0; i < sizeof flts / sizeof flts[0]; i++) {
    it = exp.find(flts[i].key);
    if (it == exp.end()) {
      // Campo assente: vuoto, salvo il diametro, che la specifica fissa a 1.75 mm se manca.
      bool isDiameter = strcmp(flts[i].key, "diameter") == 0;
      if (isDiameter) CHECK(std::fabs(flts[i].got - 1.75f) < 0.005f, "diameter: atteso 1.75 di default, letto %f", flts[i].got);
      else CHECK(std::isnan(flts[i].got), "%s: atteso assente, letto %f", flts[i].key, flts[i].got);
      continue;
    }
    float want = (float)atof(it->second.c_str());
    CHECK(std::fabs(want - flts[i].got) < 0.005f, "%s: atteso %f, letto %f", flts[i].key, want, flts[i].got);
  }

  it = exp.find("color");
  char hex[8];
  snprintf(hex, sizeof hex, "%02X%02X%02X", s.color[0], s.color[1], s.color[2]);
  if (it == exp.end()) CHECK(!s.hasColor, "colore letto ma non atteso");
  else CHECK(s.hasColor && it->second == hex, "color: atteso %s, letto %s", it->second.c_str(), hex);
}

// Nessun input, per quanto malformato, deve uscire dai limiti (lo intercetta ASan):
// tag troncato a ogni lunghezza e con byte alterati.
static void robustness(const std::string &dir, const char *name) {
  std::vector<uint8_t> mem = readFile(dir + "/" + name + ".bin");
  for (size_t n = 0; n <= mem.size(); n++) {
    std::vector<uint8_t> cut(mem.begin(), mem.begin() + n);   // buffer di dimensione esatta
    SpoolData s;
    parseType5Tag(cut.data(), cut.size(), s);
  }
  unsigned seed = 12345;
  for (int round = 0; round < 3000; round++) {
    std::vector<uint8_t> m = mem;
    for (int k = 0; k < 1 + round % 4; k++) {
      seed = seed * 1103515245u + 12345u;
      m[(seed >> 8) % m.size()] = (uint8_t)(seed >> 3);
    }
    SpoolData s;
    parseType5Tag(m.data(), m.size(), s);
  }
}

int main(int argc, char **argv) {
  std::string dir = argc > 1 ? argv[1] : "tests/fixtures/official";
  const Vector vectors[] = {
      {"sample_tag", TagParseResult::Valid}, {"tag_01", TagParseResult::Valid}, {"tag_02", TagParseResult::Valid},
      {"tag_03", TagParseResult::Invalid},   {"tag_04", TagParseResult::Valid}, {"tag_05", TagParseResult::Valid}};
  for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) checkVector(dir, vectors[i]);
  for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) robustness(dir, vectors[i].name);
  printf("robustezza: troncamenti e alterazioni senza accessi fuori limite\n");
  printf(failures ? "\n%d verifiche FALLITE\n" : "\nTutte le verifiche superate\n", failures);
  return failures ? 1 : 0;
}
