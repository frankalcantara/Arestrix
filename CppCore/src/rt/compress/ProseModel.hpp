#pragma once

#include <memory>
#include <string>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {

// Backend de inferencia do Kompress (token classification). A interface existe para a politica
// e o mascaramento serem testaveis sem o modelo; o adaptador ONNX entra quando a dependencia
// (ONNX Runtime) estiver no build.
class ProseModel {
public:
    virtual ~ProseModel() = default;
    virtual bool available() const = 0;
    // "cpu", "dml" (DirectML/GPU) ou "mock": de onde veio a inferencia.
    virtual const char* provider() const { return "cpu"; }
    // Probabilidade de manter cada token, na mesma ordem de `ids` (sem [CLS]/[SEP]).
    virtual std::vector<float> keepProbabilities(const std::vector<int>& ids) = 0;
};

// Carrega o modelo de `modelDir` (kompress-int8-wo.onnx + tokenizer.json).
// Retorna nullptr quando o build nao inclui ONNX Runtime ou o modelo nao esta presente.
std::shared_ptr<ProseModel> loadProseModel(const std::string& modelDir);

// Diretorio padrao do modelo (fora do repositorio, ao lado do CCR):
// %LOCALAPPDATA%\Arestrix\models\kompress-v2-base no Windows.
std::string defaultProseModelDir();

} // namespace compress
} // namespace rt
} // namespace taskforge
