#pragma once

#include <string>

struct FileFingerprintResult {
    bool ok = false;

    std::string fingerprint;
    std::string error;
};

/*
 * 对文件的原始二进制内容计算
 * 64 位 FNV-1a 指纹。
 *
 * 返回固定 16 位小写十六进制字符串。
 */
FileFingerprintResult calculateFileFnv1a64(const std::string& path);
