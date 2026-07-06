// --- START OF FILE: HexaStudio/guide/GuideScenarios.h ---
/**
 * @file GuideScenarios.h
 * @brief The ONLY scenario source: a static, compiled-in table (GDE-REQ-0030).
 *
 * No parser, no runtime format, no I/O, no user authoring surface. Adding or changing a scenario
 * is a code change in GuideScenarios.cpp, reviewed like any other (boss decision 2026-07-06).
 */
#ifndef HEXA_GUIDE_SCENARIOS_H
#define HEXA_GUIDE_SCENARIOS_H

#include "GuideTypes.h"

namespace hexa {

class GuideScenarios {
public:
    /// @brief All compiled-in scenarios, in card order. Static storage — pointers into the
    /// returned vector stay valid for the process lifetime.
    static const QVector<GuideScenario>& all();

    /// @brief Find a scenario by id. Returns nullptr for an unknown id — the caller turns that
    /// into the typed GuideError::UnknownScenario (never a silent fallback).
    static const GuideScenario* find(const QString& scenarioId);
};

} // namespace hexa

#endif // HEXA_GUIDE_SCENARIOS_H
// --- END OF FILE: HexaStudio/guide/GuideScenarios.h ---
