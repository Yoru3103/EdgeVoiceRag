#pragma once

#include <string>
#include <vector>

#include "retrieval_types.h"

class Retriever {
public:
    virtual ~Retriever() = default;

    virtual std::vector<RetrievalResult> searchTopK(const std::string& query, int top_k) const = 0;
};
