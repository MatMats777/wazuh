#ifndef _BUILDERS_RULE_H
#define _BUILDERS_RULE_H

#include <rxcpp/rx.hpp>
#include <stdexcept>
#include <string>

#include "connectable.hpp"
#include "json.hpp"

#include "builders/check_stage.hpp"
#include "builders/normalize_stage.hpp"

namespace builder::internals::builders
{

/**
 * @brief Builds rule connectable
 *
 * @param inputJson
 * @return Connectable
 */
Connectable ruleBuilder(const json::Document & inputJson)
{
    std::vector<std::string> parents;
    if (inputJson.exists(".parents"))
    {
        for (rapidjson::Value::ConstValueIterator it = inputJson.get(".parents")->GetArray().Begin();
             it != inputJson.get(".parents")->GetArray().End(); it++)
        {
            parents.push_back(it->GetString());
        }
    }
    Connectable connectable(inputJson.get(".name")->GetString(), parents);
    // Check stage is mandatory in a rule
    auto checkVal = inputJson.get(".check");
    if (!checkVal)
    {
        throw std::invalid_argument("Rule builder expects rule definition to have a check section. ");
    }
    auto outputObs = checkStageBuilder(connectable.output(), checkVal);

    // Normalize stage is optional
    auto normalizeVal = inputJson.get(".normalize");
    if (normalizeVal)
    {
        outputObs = normalizeStageBuilder(outputObs, normalizeVal);
    }

    // Update connectable and return
    connectable.set(outputObs);

    return connectable;
}

} // namespace builder::internals::builders
#endif // _BUILDERS_RULE_H
