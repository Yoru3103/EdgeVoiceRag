#pragma once

#include <string>
#include <vector>

namespace edge::agent {

enum class AgentIntent {
    None,
    EnvironmentQuery,
    DeviceStateQuery,
    DeviceControl,
    ConditionalWorkflow
};

struct AgentIntentClassification {
    AgentIntent intent = AgentIntent::None;
    float confidence = 0.0F;
    std::string reason;

    bool matched() const {
        return intent != AgentIntent::None;
    }
};

class AgentIntentClassifier {
public:
    AgentIntentClassification classify(const std::string& query) const;

    static std::string intentToString(AgentIntent intent);

private:
    static bool contains(
        const std::string& text,
        const std::string& keyword
    );

    static bool containsAny(
        const std::string& text,
        const std::vector<std::string>& keywords
    );

    static bool isInstructionQuestion(const std::string& query);
};

}   // namespace edge::agent
