#pragma once

#include <string>
#include <vector>

struct OrtApi;
struct OrtEnv;
struct OrtSessionOptions;
struct OrtSession;
struct OrtMemoryInfo;

// Depth Anything V2 through ONNX Runtime on DirectML. onnxruntime.dll is loaded at runtime from the
// given directory, so the host still runs when the depth files are not installed.
class DepthModel
{
public:
    ~DepthModel();

    // Builds a session for one input size, since DirectML needs fixed shapes to run fast.
    // Throws std::runtime_error with a printable reason.
    void Load(const std::wstring& directory, const std::wstring& modelFile, int width, int height);

    // input holds planar RGB, ImageNet-normalized, 3 * width * height floats. width and height must be
    // multiples of 14. output receives width * height relative inverse depth values (larger is closer).
    void Run(const std::vector<float>& input, int width, int height, std::vector<float>& output);

private:
    void* library = nullptr;
    const OrtApi* api = nullptr;
    OrtEnv* env = nullptr;
    OrtSessionOptions* options = nullptr;
    OrtSession* session = nullptr;
    OrtMemoryInfo* memory = nullptr;
    std::string inputName;
    std::string outputName;
    bool halfInput = false;
    bool halfOutput = false;
    std::vector<unsigned short> halfBuffer;
};
