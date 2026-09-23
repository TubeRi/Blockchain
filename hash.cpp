// custom_hash.cpp
//
// Savadarbe 256 bitu maisos (hash) funkcija, atitinkanti universiteto
// projekto reikalavimus.
//
// ================================================================
// 1) IVESTIS
// ================================================================
//   - Palaikomas kintamo ilgio tekstas, iskaitant tuscia ivesti (0 baitu).
//   - Ivestis skaitoma kaip zalias baitu srautas (std::ios::binary),
//     be jokios simboliu/locale konversijos. Tekstas laikomas UTF-8
//     koduote, taciau maisos funkcijai tai nesvarbu - ji mato tik baitus,
//     todel bet koks korektiskas UTF-8 tekstas (iskaitant lietuviskas
//     raides a, c, e, e, i, s, u, u, z) sumaisomas teisingai.
//   - Failo turinys maisomas pagal tikslius jo baitus - jokio trim(),
//     jokio \r\n -> \n konvertavimo, tarpai/eilutes pabaigos yra tokie
//     patys baitai kaip ir bet kurie kiti.
//   - Praktinis ivesties dydzio apribojimas: MAX_INPUT_BYTES (numatytasis
//     100 MB). Tai apsauga nuo netycinio visos atminties issemimo -
//     didesni failai atmetami su aiskia klaida, o ne apdorojami is dalies.
//
// ================================================================
// 2) FIKSUOTA ISVESTIS
// ================================================================
//   - Pasirinktas fiksuotas dydis: 256 bitai (32 baitai).
//   - Vidinis mechanizmas: 4 nepriklausomos 64 bitu "juostos" (lanes),
//     kiekviena su savo pradine baze ir pirminiu skaiciumi (kad juostos
//     neduotu identisku tarpiniu rezultatu), po visu baitu apdorojimo
//     pritaikomas papildomas "avalanche" maisymas (splitmix64 stiliaus)
//     kiekvienai juostai atskirai geresniam bitu issklaidymui.
//   - Isvestis visada lygiai 32 baitai / 64 hex simboliai, nepriklausomai
//     nuo ivesties dydzio (net tuscios ivesties atveju).
//   - Visi hex skaitmenys isvedami, iskaitant pradinius nulius
//     (std::setw(2) + std::setfill('0') kiekvienam baitui atskirai).
//   - Nuosekliai naudojamos mazosios raides (a-f), niekada didziosios.
//
// ================================================================
// 3) DETERMINIZMAS
// ================================================================
//   - Funkcija NENAUDOJA jokio laiko (time()), atsitiktiniu skaiciu
//     generatoriaus (rand(), random_device) ar bet kokios kitos
//     kiekviena karta naujai generuojamos reiksmes.
//   - Visos pradines konstantos (FNV_OFFSET_BASIS_i, FNV_PRIME_i) yra
//     fiksuotos kompiliavimo metu (constexpr), todel tie patys ivesties
//     baitai VISADA duoda ta pacia 256 bitu reiksme - tiek kartojant
//     funkcijos kvietima ta paciu programos veikimo metu, tiek is naujo
//     paleidziant programa.
//
// ================================================================
// 4) EFEKTYVUMAS
// ================================================================
//   - Programoje yra atskiras "--benchmark" rezimas, kuris:
//       a) sugeneruoja deterministinius (ne atsitiktinius) duomenu
//          blokus didejanciais dydziais,
//       b) israto laika (std::chrono::steady_clock) kiekvieno dydzio
//          maisos skaiciavimui,
//       c) atspausdina lentele: ivesties dydis (baitais) -> laikas (ms).
//   - Sis matavimas atskirtas nuo teisingumo tikrinimo (normalaus
//     hash rezimo) - t.y. veikimo greitis vertinamas atskirai nuo to,
//     ar funkcija duoda teisinga/nuoseklu rezultata.

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip>
#include <chrono>
#include <array>

// ---- Konfiguracija ---------------------------------------------------

// Maksimalus leidziamas ivesties dydis baitais (praktinis dydzio apribojimas).
static constexpr std::uint64_t MAX_INPUT_BYTES = 100ULL * 1024 * 1024; // 100 MB

// Isvesties dydis: 256 bitai = 32 baitai = 4 juostos po 64 bitus.
static constexpr int NUM_LANES = 4;
static constexpr int HASH_BYTES = NUM_LANES * 8; // 32
using HashResult = std::array<std::uint8_t, HASH_BYTES>;

// Kiekviena juosta turi savo, skirtinga FNV-1a baze ir pirmini skaiciu,
// kad 4 juostos neevoliucionuotu identiskai net su ta pacia ivestimi.
// (Konstantos parinktos rankiniu budu - didelis nelyginis skaicius su
// gera bitu maisa; svarbu tik tai, kad jos butu FIKSUOTOS ir SKIRTINGOS.)
static constexpr std::uint64_t LANE_OFFSET[NUM_LANES] = {
    0xcbf29ce484222325ULL,
    0x9e3779b97f4a7c15ULL,
    0x100000001b3ULL ^ 0xdeadbeefcafef00dULL,
    0x84222325cbf29ce4ULL
};
static constexpr std::uint64_t LANE_PRIME[NUM_LANES] = {
    0x100000001b3ULL,
    0xff51afd7ed558ccdULL,
    0xc4ceb9fe1a85ec53ULL,
    0x2545f4914f6cdd1dULL
};

// ---- Maisos funkcija ---------------------------------------------------

// splitmix64 stiliaus "finalizavimo" maisymas - papildomai issklaido
// bitus po pagrindinio FNV-1a etapo (pagerina avalanche efekta).
static inline std::uint64_t finalizeMix(std::uint64_t x)
{
    x ^= x >> 30;
    x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27;
    x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

// Skaiciuoja 256 bitu (4 x 64 bit) maisa is tikslios baitu sekos.
// Ivestis: vector<unsigned char> - zali baitai, be jokio pakeitimo.
// Isvestis: fiksuoto ilgio 32 baitu masyvas (nepriklausomai nuo data.size()).
HashResult customHash256(const std::vector<unsigned char>& data)
{
    std::uint64_t lanes[NUM_LANES];
    for (int i = 0; i < NUM_LANES; ++i)
    {
        lanes[i] = LANE_OFFSET[i];
    }

    // Kiekvienas baitas paveikia visas 4 juostas, taciau kiekviena juosta
    // papildomai priklauso nuo baito pozicijos indekso, kad pasikartojantys
    // baitu blokai (pvz. "aaaa...") neduotu itartinai panasiu tarpiniu
    // busenu skirtingose juostose.
    for (std::size_t idx = 0; idx < data.size(); ++idx)
    {
        std::uint64_t byteVal = static_cast<std::uint64_t>(data[idx]);
        for (int lane = 0; lane < NUM_LANES; ++lane)
        {
            std::uint64_t mixed = byteVal ^ (static_cast<std::uint64_t>(idx) * LANE_PRIME[(lane + 1) % NUM_LANES]);
            lanes[lane] ^= mixed;
            lanes[lane] *= LANE_PRIME[lane];
        }
    }

    // Baigiamasis avalanche maisymas kiekvienai juostai atskirai.
    for (int lane = 0; lane < NUM_LANES; ++lane)
    {
        lanes[lane] = finalizeMix(lanes[lane] ^ static_cast<std::uint64_t>(data.size()));
    }

    HashResult result{};
    for (int lane = 0; lane < NUM_LANES; ++lane)
    {
        for (int b = 0; b < 8; ++b)
        {
            // Didziausias reiksmingas baitas pirmas (big-endian isvestyje).
            result[lane * 8 + b] = static_cast<std::uint8_t>((lanes[lane] >> ((7 - b) * 8)) & 0xFF);
        }
    }

    return result; // visada lygiai 32 baitai
}

// Konvertuoja 32 baitu rezultata i 64 simboliu hex eilute (mazosios raides),
// issaugant pradinius nulius (kiekvienas baitas -> lygiai 2 hex simboliai).
std::string toFixedHex(const HashResult& hash)
{
    std::ostringstream oss;
    oss << std::hex << std::nouppercase;
    for (std::uint8_t byte : hash)
    {
        oss << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str(); // visada tiksliai 64 simboliai
}

// ---- Ivesties skaitymas ---------------------------------------------------

// Skaito visus baitus is failo TIKSLIAI, be jokios konversijos.
bool readExactBytes(const std::string& path, std::vector<unsigned char>& out)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        std::cerr << "Klaida: nepavyko atidaryti failo: " << path << "\n";
        return false;
    }

    std::streamsize size = file.tellg();
    if (size < 0)
    {
        std::cerr << "Klaida: nepavyko nustatyti failo dydzio.\n";
        return false;
    }

    if (static_cast<std::uint64_t>(size) > MAX_INPUT_BYTES)
    {
        std::cerr << "Klaida: ivestis (" << size
                  << " B) virsija leistina riba ("
                  << MAX_INPUT_BYTES << " B).\n";
        return false;
    }

    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));

    if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size))
    {
        std::cerr << "Klaida: nepavyko perskaityti viso failo turinio.\n";
        return false;
    }

    return true;
}

// Skaito visus baitus is stdin TIKSLIAI, be jokios konversijos,
// stebint MAX_INPUT_BYTES riba skaitymo metu.
bool readExactBytesFromStdin(std::vector<unsigned char>& out)
{
    char buffer[4096];
    std::uint64_t total = 0;

    while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount() > 0)
    {
        std::streamsize got = std::cin.gcount();
        total += static_cast<std::uint64_t>(got);

        if (total > MAX_INPUT_BYTES)
        {
            std::cerr << "Klaida: ivestis virsija leistina riba ("
                      << MAX_INPUT_BYTES << " B).\n";
            return false;
        }

        out.insert(out.end(), buffer, buffer + got);
    }

    return true;
}

// ---- Efektyvumo matavimas (4 reikalavimas) ---------------------------------------------------

// Sugeneruoja deterministini (NE atsitiktini) duomenu bloka - paprastas
// pasikartojantis, bet nekonstantinis raidziu raiztas, kad rezultatai
// butu atkuriami ir nepriklausytu nuo laiko/rand().
std::vector<unsigned char> generateDeterministicData(std::size_t size)
{
    std::vector<unsigned char> data(size);
    for (std::size_t i = 0; i < size; ++i)
    {
        data[i] = static_cast<unsigned char>((i * 2654435761u) & 0xFF);
    }
    return data;
}

void runBenchmark()
{
    std::cout << "Ivesties dydis (baitai) | Laikas (ms)\n";
    std::cout << "------------------------|------------\n";

    // Didejantys ivesties dydziai (nuo maziausio iki keliu MB).
    const std::vector<std::size_t> sizes = {
        0, 1024, 10 * 1024, 100 * 1024,
        1024 * 1024, 5 * 1024 * 1024, 20 * 1024 * 1024
    };

    for (std::size_t size : sizes)
    {
        std::vector<unsigned char> data = generateDeterministicData(size);

        auto start = std::chrono::steady_clock::now();
        HashResult h = customHash256(data);
        auto end = std::chrono::steady_clock::now();

        // Panaudojame 'h' bent simboliskai, kad kompiliatorius neisoptimizuotu
        // viso skaiciavimo kaip "nenaudojamo".
        volatile unsigned char sink = h[0];
        (void)sink;

        double ms = std::chrono::duration<double, std::milli>(end - start).count();

        std::cout << std::setw(23) << size << " | " << ms << " ms\n";
    }
}

// ---- main ---------------------------------------------------

void printUsage(const char* progName)
{
    std::cerr << "Naudojimas:\n"
              << "  " << progName << "                 - skaityti is stdin\n"
              << "  " << progName << " <failas>        - skaityti is failo\n"
              << "  " << progName << " --benchmark      - paleisti veikimo laiko matavima\n";
}

int main(int argc, char* argv[])
{
    if (argc >= 2 && std::string(argv[1]) == "--benchmark")
    {
        runBenchmark();
        return 0;
    }

    std::vector<unsigned char> data;
    bool ok;

    if (argc >= 2)
    {
        // Failo rezimas: ./custom_hash <kelias_iki_failo>
        ok = readExactBytes(argv[1], data);
    }
    else
    {
        // Stdin rezimas: echo -n "tekstas" | ./custom_hash
        ok = readExactBytesFromStdin(data);
    }

    if (!ok)
    {
        return 1;
    }

    HashResult hash = customHash256(data);
    std::cout << toFixedHex(hash) << "\n";

    return 0;
}