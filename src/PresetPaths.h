#pragma once
#include <filesystem>

// 개발 원본과 배포 사본 중 루트 하나를 선택한다. 파일별 fallback은 하지 않는다.
inline std::filesystem::path SelectPresetRoot(
    const std::filesystem::path& sourceRoot,
    const std::filesystem::path& executableDirectory)
{
    std::error_code error;
    if (!sourceRoot.empty())
    {
        const bool exists = std::filesystem::exists(sourceRoot, error);
        // 접근 오류를 파일 누락으로 오인하여 다른 루트에 저장하지 않는다.
        if (error || exists) return sourceRoot;
    }
    return executableDirectory / "presets";
}
