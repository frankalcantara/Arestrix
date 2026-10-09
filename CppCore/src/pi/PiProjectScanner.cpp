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
    res.projectName = "Pi Matemática - Produção de Artigos";

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

    // 5. Inspeciona pasta de destino (matematica)
    std::filesystem::path matDir = root.parent_path() / "matematica";
    if (std::filesystem::exists(matDir)) {
        res.destinationPostsDir = (matDir / "_posts" / "matematica").string();
        res.destinationDiagramsDir = (matDir / "assets" / "diagrams").string();
    } else {
        res.destinationPostsDir = (root / "outputs" / "_posts").string();
        res.destinationDiagramsDir = (root / "outputs" / "diagrams").string();
    }

    // 6. Monta as etapas do pipeline
    res.stages = {
        {
            "1-redigir",
            "1. Redação do Artigo",
            res.defaultModel,
            ".agents/redigir-math/SKILL.md",
            "Gere o artigo {itemId} baseado no plano.md. Salve em aulas/{itemId}.md",
            "aulas/{itemId}.md",
            1000
        },
        {
            "2-frontmatter",
            "2. Inserção de Frontmatter",
            "deepseek-flash",
            ".agents/criar-frontmatter/SKILL.md",
            "Leia aulas/{itemId}.md, monte o frontmatter e insira no topo com a ferramenta edit.",
            "aulas/{itemId}.md",
            1000
        },
        {
            "3-planejar-svg",
            "3. Planejamento de SVGs",
            "deepseek-flash",
            ".agents/planejar-svg/SKILL.md",
            "Leia aulas/{itemId}.md. Planeje todos os diagramas SVG e salve em aulas/{itemId}-svg-plan.md",
            "aulas/{itemId}-svg-plan.md",
            200
        },
        {
            "4-criar-svg",
            "4. Criação e Validação dos SVGs",
            "deepseek-flash",
            ".agents/criar-svg/SKILL.md",
            "Leia aulas/{itemId}.md e o plano de SVG. Crie os arquivos em ../matematica/assets/diagrams/ e insira os links no artigo.",
            "../matematica/assets/diagrams/",
            100
        },
        {
            "4b-validar-svg",
            "4b. Validação Visual dos SVGs (Jev)",
            "deepseek-flash",
            ".agents/validar-svg/SKILL.md",
            "Valide visualmente os SVGs e escreva o relatório em aulas/{itemId}-validacao-svg.md",
            "aulas/{itemId}-validacao-svg.md",
            50
        },
        {
            "5-animar-svg",
            "5. Animação dos SVGs",
            "deepseek-flash",
            ".agents/animar-svg/SKILL.md",
            "Injete as classes .anim-* e keyframes nos arquivos SVG planejados.",
            "../matematica/assets/diagrams/",
            100
        },
        {
            "6-publicar",
            "6. Publicação no Blog Matemática",
            "taskflow_static",
            "",
            "Copia o artigo concluído para _posts/matematica com a data e slug canônico.",
            res.destinationPostsDir,
            1000
        }
    };

    return res;
}

std::vector<std::string> PiProjectScanner::getKnownPiProjects() {
    std::vector<std::string> candidates = {
        "C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica",
        "C:\\Projetos\\frankalcantara.github.io.worktrees\\pi-matematica",
        "C:/Projetos/PiAgente",
        "C:\\Projetos\\PiAgente"
    };

    std::vector<std::string> valid;
    for (const auto& pathStr : candidates) {
        if (std::filesystem::exists(pathStr) && std::filesystem::is_directory(pathStr)) {
            std::string canonical = std::filesystem::canonical(pathStr).string();
            bool jaExiste = false;
            for (const auto& v : valid) {
                if (v == canonical) { jaExiste = true; break; }
            }
            if (!jaExiste) {
                valid.push_back(canonical);
            }
        }
    }
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
    return j;
}

} // namespace pi
} // namespace taskforge
