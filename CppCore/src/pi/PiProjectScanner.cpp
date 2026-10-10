#include "PiProjectScanner.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <regex>

namespace taskforge {
namespace pi {

PiProjectScanResult PiProjectScanner::scan(const std::string& directoryPath) {
    PiProjectScanResult res;
    std::filesystem::path root(directoryPath);

    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        res.isValid = false;
        return res;
    }

    res.isValid = true;
    res.rootPath = std::filesystem::canonical(root).string();
    res.projectName = root.filename().string();
    if (res.projectName.empty()) res.projectName = "Projeto Pi";

    // 1. Inspeciona .pi/settings.json
    std::filesystem::path settingsPath = root / ".pi" / "settings.json";
    if (std::filesystem::exists(settingsPath)) {
        try {
            std::ifstream f(settingsPath);
            nlohmann::json j = nlohmann::json::parse(f);
            if (j.contains("defaultProvider")) res.defaultProvider = j["defaultProvider"].get<std::string>();
            if (j.contains("defaultModel")) res.defaultModel = j["defaultModel"].get<std::string>();
        } catch (...) {
            // Mantém valores padrão
        }
    }

    // 2. Inspeciona .agents/
    std::filesystem::path agentsDir = root / ".agents";
    if (std::filesystem::exists(agentsDir) && std::filesystem::is_directory(agentsDir)) {
        for (const auto& entry : std::filesystem::directory_iterator(agentsDir)) {
            if (entry.is_directory()) {
                std::filesystem::path skillFile = entry.path() / "SKILL.md";
                if (std::filesystem::exists(skillFile)) {
                    PiSkillInfo skill;
                    skill.id = entry.path().filename().string();
                    skill.name = skill.id;
                    skill.path = (std::filesystem::path(".agents") / entry.path().filename() / "SKILL.md").string();
                    skill.description = "Skill do agente " + skill.name;
                    res.skills.push_back(skill);
                }
            }
        }
    }

    // 3. Inspeciona plano.md para extrair catálogo de artigos
    std::filesystem::path planoPath = root / "plano.md";
    if (std::filesystem::exists(planoPath)) {
        try {
            std::ifstream f(planoPath);
            std::string line;
            std::regex artRegex(R"((ART-[0-9]{3}))");
            std::smatch match;

            while (std::getline(f, line)) {
                if (std::regex_search(line, match, artRegex)) {
                    std::string artId = match[1].str();
                    bool jaExiste = false;
                    for (const auto& existing : res.articles) {
                        if (existing == artId) { jaExiste = true; break; }
                    }
                    if (!jaExiste) {
                        res.articles.push_back(artId);
                    }
                }
            }
        } catch (...) {}
    }

    // 4. Inspeciona aulas/pipeline-state.json
    std::filesystem::path statePath = root / "aulas" / "pipeline-state.json";
    res.stateFilePath = statePath.string();
    if (std::filesystem::exists(statePath)) {
        try {
            std::ifstream f(statePath);
            nlohmann::json jState = nlohmann::json::parse(f);
            for (auto it = jState.begin(); it != jState.end(); ++it) {
                if (it.value().is_object() && it.value().value("status", "") == "concluido") {
                    res.completedArticlesCount++;
                }
            }
        } catch (...) {}
    }

    // 5. Configura pastas de saida padrao do projeto
    std::filesystem::path outputsDir = root / "outputs";
    res.destinationPostsDir = (outputsDir / "posts").string();
    res.destinationDiagramsDir = (outputsDir / "diagrams").string();

    // 6. Monta as etapas do pipeline de forma genérica
    std::filesystem::path orqPath = root / "orquestrador.js";
    bool orqFound = false;

    if (std::filesystem::exists(orqPath)) {
        try {
            std::ifstream fOrq(orqPath);
            std::string line;
            std::string curId, curNome, curSkill, curModel;
            while (std::getline(fOrq, line)) {
                std::smatch m;
                if (std::regex_search(line, m, std::regex(R"(id:\s*['"`]([^'"`]+)['"`])"))) {
                    curId = m[1].str();
                }
                if (std::regex_search(line, m, std::regex(R"(nome:\s*['"`]([^'"`]+)['"`])"))) {
                    curNome = m[1].str();
                }
                if (std::regex_search(line, m, std::regex(R"(skill:\s*['"`]([^'"`]+)['"`])"))) {
                    curSkill = m[1].str();
                }
                if (std::regex_search(line, m, std::regex(R"(modelo:\s*([a-zA-Z0-9_-]+))"))) {
                    curModel = m[1].str();
                    if (curModel == "MODELO_REDACAO") curModel = res.defaultModel;
                    else if (curModel == "MODELO_DEMAIS") curModel = "deepseek-flash";
                }
                if (line.find('}') != std::string::npos && !curId.empty() && !curNome.empty()) {
                    PiStageInfo st;
                    st.id = curId;
                    st.name = curNome;
                    st.model = curModel.empty() ? res.defaultModel : curModel;
                    st.skillPath = curSkill;
                    st.prompt = "Execute a tarefa para " + curNome;
                    st.validationTarget = "artefato_validado";
                    st.minSizeBytes = 100;
                    res.stages.push_back(st);

                    curId.clear();
                    curNome.clear();
                    curSkill.clear();
                    curModel.clear();
                    orqFound = true;
                }
            }
        } catch (...) {}
    }

    // Se nenhuma etapa foi encontrada no orquestrador, cria etapas dinâmicas a partir das skills encontradas
    if (!orqFound && !res.skills.empty()) {
        int idx = 1;
        for (const auto& sk : res.skills) {
            PiStageInfo st;
            st.id = sk.id;
            st.name = std::to_string(idx++) + ". " + sk.name;
            st.model = res.defaultModel;
            st.skillPath = sk.path;
            st.prompt = "Execute a tarefa correspondente à skill " + sk.name;
            st.validationTarget = "";
            st.minSizeBytes = 100;
            res.stages.push_back(st);
        }
    }

    return res;
}

std::vector<std::string> PiProjectScanner::getKnownPiProjects() {
    std::vector<std::string> valid;
    // Lista dinamicamente pastas com .agents ou .pi em C:/Projetos se existirem
    try {
        std::filesystem::path base("C:/Projetos");
        if (std::filesystem::exists(base) && std::filesystem::is_directory(base)) {
            for (const auto& entry : std::filesystem::directory_iterator(base)) {
                if (entry.is_directory()) {
                    if (std::filesystem::exists(entry.path() / ".agents") || std::filesystem::exists(entry.path() / ".pi")) {
                        valid.push_back(std::filesystem::canonical(entry.path()).string());
                    }
                }
            }
        }
    } catch (...) {}
    return valid;
}

nlohmann::json PiProjectScanner::toJson(const PiProjectScanResult& res) {
    nlohmann::json j;
    j["isValid"] = res.isValid;
    j["rootPath"] = res.rootPath;
    j["projectName"] = res.projectName;
    j["defaultProvider"] = res.defaultProvider;
    j["defaultModel"] = res.defaultModel;
    j["destinationPostsDir"] = res.destinationPostsDir;
    j["destinationDiagramsDir"] = res.destinationDiagramsDir;
    j["completedArticlesCount"] = res.completedArticlesCount;
    j["totalArticlesCount"] = res.articles.size();
    j["stateFilePath"] = res.stateFilePath;

    j["skills"] = nlohmann::json::array();
    for (const auto& s : res.skills) {
        j["skills"].push_back({
            {"id", s.id},
            {"name", s.name},
            {"path", s.path},
            {"description", s.description}
        });
    }

    j["stages"] = nlohmann::json::array();
    for (const auto& st : res.stages) {
        j["stages"].push_back({
            {"id", st.id},
            {"name", st.name},
            {"model", st.model},
            {"skillPath", st.skillPath},
            {"prompt", st.prompt},
            {"validationTarget", st.validationTarget},
            {"minSizeBytes", st.minSizeBytes}
        });
    }

    j["articles"] = res.articles;
    j["workItems"] = res.articles;
    j["defaultTools"] = nlohmann::json::array();
    j["mcpTools"] = nlohmann::json::array();
    return j;
}

} // namespace pi
} // namespace taskforge
