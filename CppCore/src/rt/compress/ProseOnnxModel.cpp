#include "rt/compress/ProseOnnxModel.hpp"

#include "rt/compress/ProseTokenizer.hpp"
#include "rt/compress/ProseRecommendation.hpp"

#ifdef ARESTRIX_WITH_ONNX
#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace taskforge {
namespace rt {
namespace compress {
namespace {

constexpr size_t kDefaultMaxLength = 8192;

size_t readMaxLength(const std::filesystem::path& configPath) {
    try {
        std::ifstream in(configPath, std::ios::binary);
        if (!in) return kDefaultMaxLength;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto parsed = jsonutil::tryParse(content);
        if (!parsed) return kDefaultMaxLength;
        const int64_t value = parsed->value("max_length", static_cast<int64_t>(kDefaultMaxLength));
        return static_cast<size_t>(std::max<int64_t>(64, std::min<int64_t>(32768, value)));
    } catch (...) {
        return kDefaultMaxLength;
    }
}

std::vector<std::filesystem::path> modelCandidates(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> paths;
    // fp32 primeiro: no CPU ele foi ~1,6x mais rapido que o weight-only int8 (dequantizacao por
    // MatMul). O int8 continua aceito como alternativa compacta.
    for (const char* candidate : {"kompress-fp32.onnx", "kompress-int8-wo.onnx", "kompress-int8.onnx",
                                  "model.onnx"}) {
        const auto path = dir / candidate;
        std::error_code ec;
        if (std::filesystem::exists(path, ec)) paths.push_back(path);
    }
    return paths;
}

float sigmoid(float value) {
    return 1.0f / (1.0f + std::exp(-value));
}

class OnnxProseModel final : public ProseModel {
public:
    static std::shared_ptr<ProseModel> load(const std::string& modelDir) {
        const std::filesystem::path dir(modelDir);
        const auto candidates = modelCandidates(dir);
        if (candidates.empty()) return nullptr;

        auto model = std::shared_ptr<OnnxProseModel>(new OnnxProseModel(dir));
        if (!model->init(candidates)) return nullptr;
        return model;
    }

    bool available() const override { return ok_; }

    const char* provider() const override { return provider_.c_str(); }

    // O export do Kompress pode trazer:
    //  - [1, seq, 2]  logits do token head (softmax na classe 1);
    //  - [1, seq]     score final (keep) ja calculado;
    //  - dois outputs: token logits + span (score final = p_keep * (0.5 + 0.5 * sigmoid(span))).
    std::vector<float> keepProbabilities(const std::vector<int>& ids) override {
        if (!ok_ || ids.empty()) return {};
        // Uma sequencia gigante estoura a VRAM do DirectML (fail-fast, nao exception). O
        // ProseCompressor divide em janelas de 2048; chamadas diretas acima disso falham
        // fechadas (texto original preservado) em vez de derrubar o processo.
        if (ids.size() > 2048) return {};

        std::vector<int64_t> input;
        input.reserve(ids.size() + 2);
        if (clsId_ >= 0) input.push_back(clsId_);
        const size_t room = maxLength_ > 2 ? maxLength_ - 2 : maxLength_;
        for (size_t i = 0; i < ids.size() && input.size() < room; ++i) {
            input.push_back(static_cast<int64_t>(ids[i]));
        }
        if (sepId_ >= 0) input.push_back(sepId_);
        if (input.size() < 2) return {};

        std::vector<int64_t> mask(input.size(), 1);
        std::vector<int64_t> types(input.size(), 0);
        const std::array<int64_t, 2> shape{1, static_cast<int64_t>(input.size())};
        Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<Ort::Value> feeds;
        std::vector<const char*> feedNames;
        feeds.reserve(inputNames_.size());
        feedNames.reserve(inputNames_.size());
        for (const auto& name : inputNames_) {
            if (name.find("attention") != std::string::npos) {
                feeds.push_back(Ort::Value::CreateTensor<int64_t>(memory, mask.data(), mask.size(),
                                                                  shape.data(), shape.size()));
            } else if (name.find("token_type") != std::string::npos) {
                feeds.push_back(Ort::Value::CreateTensor<int64_t>(memory, types.data(), types.size(),
                                                                  shape.data(), shape.size()));
            } else {
                feeds.push_back(Ort::Value::CreateTensor<int64_t>(memory, input.data(), input.size(),
                                                                  shape.data(), shape.size()));
            }
            feedNames.push_back(name.c_str());
        }

        std::vector<const char*> outputNames;
        outputNames.reserve(outputNames_.size());
        for (const auto& name : outputNames_) outputNames.push_back(name.c_str());

        auto outputs = session_->Run(Ort::RunOptions{nullptr}, feedNames.data(), feeds.data(), feeds.size(),
                                     outputNames.data(), outputNames.size());
        if (outputs.empty()) return {};

        const auto shape0 = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const float* data = outputs[0].GetTensorMutableData<float>();
        const size_t offset = clsId_ >= 0 ? 1 : 0;

        size_t positions = 0;
        bool useSpan = false;
        const float* span = nullptr;

        if (shape0.size() == 3 && shape0[2] == 2) {
            positions = static_cast<size_t>(shape0[1]);
            if (outputs.size() >= 2) {
                const auto spanShape = outputs[1].GetTensorTypeAndShapeInfo().GetShape();
                if (spanShape.size() == 2 && static_cast<size_t>(spanShape[1]) >= positions) {
                    span = outputs[1].GetTensorMutableData<float>();
                    useSpan = true;
                } else if (spanShape.size() == 3 && static_cast<size_t>(spanShape[1]) >= positions) {
                    span = outputs[1].GetTensorMutableData<float>();
                    useSpan = true;
                }
            }
        } else if (shape0.size() == 2) {
            positions = static_cast<size_t>(shape0[1]);   // score final direto
        } else if (shape0.size() == 3 && shape0[2] == 1) {
            positions = static_cast<size_t>(shape0[1]);   // score final por posicao
        } else {
            return {};
        }

        std::vector<float> keep;
        keep.reserve(ids.size());
        for (size_t i = 0; i < ids.size() && offset + i < positions; ++i) {
            const size_t pos = offset + i;
            float p = 0.0f;
            if (shape0.size() == 3 && shape0[2] == 2) {
                const float drop = data[pos * 2 + 0];
                const float keepLogit = data[pos * 2 + 1];
                const float maxLogit = std::max(drop, keepLogit);
                const float denom = std::exp(drop - maxLogit) + std::exp(keepLogit - maxLogit);
                p = std::exp(keepLogit - maxLogit) / denom;
            } else if (shape0.size() == 3) {
                p = data[pos];
            } else {
                p = data[pos];
            }
            if (useSpan && span) {
                const size_t spanIndex = (shape0.size() == 3 && shape0[2] == 2)
                                             ? pos
                                             : pos;   // [1, seq] ou [1, seq, 1]
                float s = span[spanIndex];
                if (s < 0.0f || s > 1.0f) s = sigmoid(s);
                p *= (0.5f + 0.5f * s);
            }
            keep.push_back(std::min(1.0f, std::max(0.0f, p)));
        }
        return keep;
    }

private:
    explicit OnnxProseModel(const std::filesystem::path& dir) {
        auto tokenizer = ProseTokenizer::fromJsonFile((dir / "tokenizer.json").string());
        if (tokenizer) {
            clsId_ = tokenizer->clsId();
            sepId_ = tokenizer->sepId();
        }
        maxLength_ = readMaxLength(dir / "config.json");
    }

    bool init(const std::vector<std::filesystem::path>& candidates) {
        try {
            env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "arestrix-kompress");

            const char* epEnv = std::getenv("ARESTRIX_PROSE_EP");
            const std::string wanted = epEnv ? epEnv : "auto";
            bool usedDml = false;

#ifdef ARESTRIX_WITH_ONNX_DML
            if (wanted == "dml" || wanted == "auto") {
                usedDml = openSession(candidates, true);
            }
#endif
            if (!usedDml) {
                if (wanted == "dml") return false;
                if (!openSession(candidates, false)) return false;
            }
            provider_ = usedDml ? "dml" : "cpu";

            Ort::AllocatorWithDefaultOptions allocator;
            const size_t inputCount = session_->GetInputCount();
            for (size_t i = 0; i < inputCount; ++i) {
                auto name = session_->GetInputNameAllocated(i, allocator);
                inputNames_.emplace_back(name.get());
            }
            const size_t outputCount = session_->GetOutputCount();
            for (size_t i = 0; i < outputCount; ++i) {
                auto name = session_->GetOutputNameAllocated(i, allocator);
                outputNames_.emplace_back(name.get());
            }
            ok_ = !inputNames_.empty() && !outputNames_.empty();
            return ok_;
        } catch (...) {
            ok_ = false;
            return false;
        }
    }

    // Tenta abrir a sessao no DirectML (GPU) ou no CPU. O DML cai para CPU por no quando um
    // operador nao tem kernel na GPU; se a sessao inteira falhar, o chamador tenta o CPU.
    bool openSession(const std::vector<std::filesystem::path>& candidates, bool useDml) {
        try {
            Ort::SessionOptions options;
            int threads = 4;
            if (const char* envThreads = std::getenv("ARESTRIX_PROSE_THREADS")) {
                threads = std::atoi(envThreads);
            }
            options.SetIntraOpNumThreads(threads < 0 ? 0 : threads);
            options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
            // O DML avisa que alguns nos de shape ficam no CPU; isso e esperado e poluiria o
            // Console. ARESTRIX_PROSE_DEBUG=1 mantem os avisos para diagnostico.
            if (!std::getenv("ARESTRIX_PROSE_DEBUG")) {
                options.SetLogSeverityLevel(ORT_LOGGING_LEVEL_ERROR);
            }
            if (useDml) {
                // Escolhe a GPU dedicada (maior VRAM) em vez de assumir o adaptador 0, que em
                // notebooks hibridos costuma ser a iGPU Intel (bem mais lenta que o CPU).
                int deviceId = -1;
                if (const char* envDevice = std::getenv("ARESTRIX_PROSE_DML_DEVICE")) {
                    deviceId = std::atoi(envDevice);
                } else {
                    deviceId = bestDmlDevice(detectGpus());
                    if (deviceId < 0) deviceId = 0;
                }
                options.AppendExecutionProvider("DML", {{"device_id", std::to_string(deviceId)}});
            }

            for (const auto& path : candidates) {
                try {
#ifdef _WIN32
                    const std::wstring widePath = path.wstring();
                    session_ = std::make_unique<Ort::Session>(*env_, widePath.c_str(), options);
#else
                    session_ = std::make_unique<Ort::Session>(*env_, path.string().c_str(), options);
#endif
                    return true;
                } catch (...) {
                    session_.reset();
                }
            }
        } catch (...) {
            session_.reset();
        }
        return false;
    }

    bool ok_ = false;
    std::string provider_ = "cpu";
    int clsId_ = -1;
    int sepId_ = -1;
    size_t maxLength_ = kDefaultMaxLength;
    std::unique_ptr<Ort::Env> env_;
    std::unique_ptr<Ort::Session> session_;
    std::vector<std::string> inputNames_;
    std::vector<std::string> outputNames_;
};

} // namespace

std::shared_ptr<ProseModel> loadOnnxProseModel(const std::string& modelDir) {
    return OnnxProseModel::load(modelDir);
}

} // namespace compress
} // namespace rt
} // namespace taskforge

#else   // ARESTRIX_WITH_ONNX

namespace taskforge {
namespace rt {
namespace compress {

std::shared_ptr<ProseModel> loadOnnxProseModel(const std::string&) {
    return nullptr;
}

} // namespace compress
} // namespace rt
} // namespace taskforge

#endif   // ARESTRIX_WITH_ONNX
