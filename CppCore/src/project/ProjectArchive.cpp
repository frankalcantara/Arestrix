#include "project/ProjectArchive.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <vector>

namespace taskforge {
namespace project {

namespace fs = std::filesystem;

namespace {

constexpr uint16_t ZIP_VERSION = 20;
constexpr uint16_t ZIP_UTF8_FLAG = 0x0800;
constexpr uint16_t ZIP_STORE_METHOD = 0;

struct ZipEntryInfo {
    fs::path relativePath;
    uint32_t crc = 0;
    uint64_t size = 0;
    uint32_t localHeaderOffset = 0;
};

uint32_t crc32TableValue(uint32_t index) {
    uint32_t value = index;
    for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
    }
    return value;
}

const std::array<uint32_t, 256>& crc32Table() {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> values{};
        for (uint32_t i = 0; i < 256; ++i) values[i] = crc32TableValue(i);
        return values;
    }();
    return table;
}

uint32_t crc32Update(uint32_t crc, const unsigned char* data, size_t size) {
    const auto& table = crc32Table();
    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc;
}

void writeLe16(std::ostream& out, uint16_t value) {
    const char bytes[2] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF)
    };
    out.write(bytes, 2);
}

void writeLe32(std::ostream& out, uint32_t value) {
    const char bytes[4] = {
        static_cast<char>(value & 0xFF),
        static_cast<char>((value >> 8) & 0xFF),
        static_cast<char>((value >> 16) & 0xFF),
        static_cast<char>((value >> 24) & 0xFF)
    };
    out.write(bytes, 4);
}

void dosDateTime(uint16_t& dosTime, uint16_t& dosDate) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    dosTime = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
    dosDate = static_cast<uint16_t>(((local.tm_year - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
}

std::string toLowerAscii(std::string value) {
    for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

bool isExcludedDirectory(const std::string& name) {
    const std::string lower = toLowerAscii(name);
    return lower == ".git" || lower == ".arestrix" || lower == "build" ||
           lower == "dist" || lower == "node_modules" || lower == "runtime" ||
           lower == "models" || lower == "storage";
}

bool isExcludedFile(const std::string& name) {
    const std::string lower = toLowerAscii(name);
    if (lower == ".env" || lower.rfind(".env.", 0) == 0 ||
        lower == "credentials.json" || lower == "id_rsa" || lower == "id_ed25519") {
        return true;
    }
    const std::string archiveSuffix = ".arestrix.zip";
    if (lower.size() >= archiveSuffix.size() &&
        lower.compare(lower.size() - archiveSuffix.size(), archiveSuffix.size(), archiveSuffix) == 0) {
        return true;
    }
    const fs::path path(lower);
    const std::string ext = path.extension().string();
    return ext == ".db" || ext == ".sqlite" || ext == ".sqlite3" ||
           ext == ".db-wal" || ext == ".db-shm" || ext == ".key" ||
           ext == ".pem" || ext == ".p12" || ext == ".pfx" ||
           ext == ".gguf" || ext == ".safetensors" || ext == ".onnx" ||
           ext == ".bin";
}

bool collectFiles(const fs::path& root, std::vector<fs::path>& files, std::string& error) {
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        error = "Nao foi possivel ler a pasta do projeto: " + ec.message();
        return false;
    }
    const fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::directory_entry& entry = *it;
        const std::string name = entry.path().filename().string();
        if (entry.is_symlink(ec)) continue;
        if (entry.is_directory(ec)) {
            if (isExcludedDirectory(name)) it.disable_recursion_pending();
            continue;
        }
        if (!entry.is_regular_file(ec)) continue;
        if (isExcludedFile(name)) continue;
        const fs::path relative = fs::relative(entry.path(), root, ec);
        if (ec || relative.empty() || *relative.begin() == fs::path("..")) {
            error = "Falha ao calcular o caminho relativo do projeto.";
            return false;
        }
        files.push_back(relative);
    }
    std::sort(files.begin(), files.end());
    return true;
}

bool computeCrcAndSize(const fs::path& file, uint32_t& crc, uint64_t& size, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        error = "Nao foi possivel abrir o arquivo para exportacao: " + file.string();
        return false;
    }
    crc = 0xFFFFFFFFu;
    size = 0;
    std::array<unsigned char, 64 * 1024> buffer{};
    while (in) {
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count > 0) {
            crc = crc32Update(crc, buffer.data(), static_cast<size_t>(count));
            size += static_cast<uint64_t>(count);
        }
    }
    if (in.bad()) {
        error = "Falha ao ler o arquivo para exportacao: " + file.string();
        return false;
    }
    crc ^= 0xFFFFFFFFu;
    return true;
}

std::string pathToUtf8String(const fs::path& path) {
    const std::u8string value = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string zipEntryName(const std::string& topFolder, const fs::path& relative) {
    std::string name = topFolder + "/";
    name += pathToUtf8String(relative);
    std::replace(name.begin(), name.end(), '\\', '/');
    return name;
}

std::string sanitizeArchiveName(const std::string& projectName, const fs::path& root) {
    std::string name = projectName;
    if (name.empty()) name = pathToUtf8String(root.filename());
    if (name.empty()) name = "projeto";
    std::string clean;
    for (char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ' ||
            static_cast<unsigned char>(c) >= 0x80) {
            clean.push_back(c);
        } else if (c == '.') {
            clean.push_back(c);
        }
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    return clean.empty() ? "projeto" : clean;
}

fs::path uniqueArchivePath(const fs::path& targetDir, const std::string& baseName) {
    fs::path candidate = targetDir / (baseName + ".arestrix.zip");
    std::error_code ec;
    for (int index = 2; fs::exists(candidate, ec) && index < 10000; ++index) {
        candidate = targetDir / (baseName + " (" + std::to_string(index) + ").arestrix.zip");
    }
    return candidate;
}

bool writeEntry(std::ostream& out, const fs::path& file, const std::string& entryName,
                uint32_t crc, uint64_t size, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in.is_open()) {
        error = "Nao foi possivel abrir o arquivo para exportacao: " + file.string();
        return false;
    }

    uint16_t timeValue = 0;
    uint16_t dateValue = 0;
    dosDateTime(timeValue, dateValue);

    writeLe32(out, 0x04034b50u);
    writeLe16(out, ZIP_VERSION);
    writeLe16(out, ZIP_UTF8_FLAG);
    writeLe16(out, ZIP_STORE_METHOD);
    writeLe16(out, timeValue);
    writeLe16(out, dateValue);
    writeLe32(out, crc);
    writeLe32(out, static_cast<uint32_t>(size));
    writeLe32(out, static_cast<uint32_t>(size));
    writeLe16(out, static_cast<uint16_t>(entryName.size()));
    writeLe16(out, 0);
    out.write(entryName.data(), static_cast<std::streamsize>(entryName.size()));

    std::array<char, 64 * 1024> buffer{};
    while (in) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = in.gcount();
        if (count > 0) out.write(buffer.data(), count);
    }
    if (in.bad()) {
        error = "Falha ao ler o arquivo para exportacao: " + file.string();
        return false;
    }
    if (!out) {
        error = "Falha ao gravar os dados no arquivo ZIP.";
        return false;
    }
    return true;
}

} // namespace

bool ProjectArchive::exportProjectZip(
    const fs::path& projectRoot,
    const fs::path& targetDir,
    const std::string& projectName,
    fs::path& outZipPath,
    std::string& outError
) {
    outZipPath.clear();
    outError.clear();

    std::error_code ec;
    if (projectRoot.empty() || !fs::is_directory(projectRoot, ec)) {
        outError = "Pasta do projeto invalida.";
        return false;
    }
    if (!fs::exists(projectRoot / "arestrix.project.json", ec) &&
        !fs::exists(projectRoot / "taskforge.project.json", ec)) {
        outError = "A pasta selecionada nao contem um projeto Arestrix.";
        return false;
    }
    if (targetDir.empty()) {
        outError = "Pasta de destino invalida.";
        return false;
    }
    fs::create_directories(targetDir, ec);
    if (ec) {
        outError = "Nao foi possivel criar a pasta de destino: " + ec.message();
        return false;
    }

    std::vector<fs::path> files;
    if (!collectFiles(projectRoot, files, outError)) return false;
    if (files.empty()) {
        outError = "A pasta do projeto nao contem arquivos para exportar.";
        return false;
    }

    const std::string topFolder = sanitizeArchiveName(projectName, projectRoot);
    const fs::path zipPath = uniqueArchivePath(targetDir, topFolder);
    if (zipPath == fs::path{}) {
        outError = "Nao foi possivel definir o nome do arquivo de exportacao.";
        return false;
    }

    std::vector<ZipEntryInfo> entries;
    entries.reserve(files.size());
    for (const fs::path& relative : files) {
        ZipEntryInfo info;
        info.relativePath = relative;
        if (!computeCrcAndSize(projectRoot / relative, info.crc, info.size, outError)) return false;
        if (info.size > 0xFFFFFFFFull) {
            outError = "Arquivo maior que 4 GB nao e suportado na exportacao ZIP.";
            return false;
        }
        entries.push_back(std::move(info));
    }
    if (entries.size() > 0xFFFFu) {
        outError = "O projeto tem arquivos demais para o formato ZIP classico.";
        return false;
    }

    std::ofstream out(zipPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        outError = "Nao foi possivel criar o arquivo ZIP de exportacao.";
        return false;
    }

    uint32_t offset = 0;
    for (ZipEntryInfo& info : entries) {
        info.localHeaderOffset = offset;
        const std::string entryName = zipEntryName(topFolder, info.relativePath);
        if (entryName.size() > 0xFFFFu) {
            outError = "Um caminho do projeto e longo demais para o formato ZIP.";
            out.close();
            fs::remove(zipPath, ec);
            return false;
        }
        const uint64_t nextOffset = static_cast<uint64_t>(offset) + 30 + entryName.size() + info.size;
        if (nextOffset > 0xFFFFFFFFull) {
            outError = "O projeto excede o limite de 4 GB do formato ZIP.";
            out.close();
            fs::remove(zipPath, ec);
            return false;
        }
        if (!writeEntry(out, projectRoot / info.relativePath, entryName, info.crc, info.size, outError)) {
            out.close();
            fs::remove(zipPath, ec);
            return false;
        }
        offset = static_cast<uint32_t>(nextOffset);
    }

    const uint32_t centralOffset = offset;
    uint64_t centralSize64 = 0;
    for (const ZipEntryInfo& info : entries) {
        const std::string entryName = zipEntryName(topFolder, info.relativePath);
        if (entryName.size() > 0xFFFFu) {
            outError = "Um caminho do projeto e longo demais para o formato ZIP.";
            out.close();
            fs::remove(zipPath, ec);
            return false;
        }
        centralSize64 += 46 + entryName.size();
        if (centralSize64 > 0xFFFFFFFFull ||
            static_cast<uint64_t>(centralOffset) + centralSize64 > 0xFFFFFFFFull) {
            outError = "O diretorio central do ZIP excede o limite de 4 GB.";
            out.close();
            fs::remove(zipPath, ec);
            return false;
        }
        uint16_t timeValue = 0;
        uint16_t dateValue = 0;
        dosDateTime(timeValue, dateValue);
        writeLe32(out, 0x02014b50u);
        writeLe16(out, ZIP_VERSION);
        writeLe16(out, ZIP_VERSION);
        writeLe16(out, ZIP_UTF8_FLAG);
        writeLe16(out, ZIP_STORE_METHOD);
        writeLe16(out, timeValue);
        writeLe16(out, dateValue);
        writeLe32(out, info.crc);
        writeLe32(out, static_cast<uint32_t>(info.size));
        writeLe32(out, static_cast<uint32_t>(info.size));
        writeLe16(out, static_cast<uint16_t>(entryName.size()));
        writeLe16(out, 0);
        writeLe16(out, 0);
        writeLe16(out, 0);
        writeLe16(out, 0);
        writeLe32(out, 0);
        writeLe32(out, info.localHeaderOffset);
        out.write(entryName.data(), static_cast<std::streamsize>(entryName.size()));
    }

    const uint32_t centralSize = static_cast<uint32_t>(centralSize64);
    writeLe32(out, 0x06054b50u);
    writeLe16(out, 0);
    writeLe16(out, 0);
    writeLe16(out, static_cast<uint16_t>(entries.size()));
    writeLe16(out, static_cast<uint16_t>(entries.size()));
    writeLe32(out, centralSize);
    writeLe32(out, centralOffset);
    writeLe16(out, 0);
    out.close();

    if (!out) {
        outError = "Falha ao finalizar o arquivo ZIP de exportacao.";
        fs::remove(zipPath, ec);
        return false;
    }

    outZipPath = zipPath;
    return true;
}

} // namespace project
} // namespace taskforge
