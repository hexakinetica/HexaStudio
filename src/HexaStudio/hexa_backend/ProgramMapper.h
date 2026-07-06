// --- START OF FILE: HexaStudio/hexa_backend/ProgramMapper.h ---
/**
 * @file ProgramMapper.h
 * @brief Pure, stateless mapping between the studio program DTO (QVector<ProgramCommand>) and the RDT
 *        wire program (RDT::NetProtocol::ProgramDataStruct).
 *
 * This is the studio ↔ controller program data contract, extracted from HexaBackend so it can be
 * exercised directly (no network thread, no ClientState) by the pipeline integration test and reused
 * unchanged by the backend. It has no Qt widgets and no backend state.
 */
#ifndef HEXA_PROGRAM_MAPPER_H
#define HEXA_PROGRAM_MAPPER_H

#include <QVector>

#include "ProgramData.h"   // hexa ProgramCommand / CommandType
#include "RdtProtocol.h"   // RDT::NetProtocol::ProgramDataStruct

namespace hexa::program_mapper {

/**
 * @brief Studio program → RDT wire program.
 *
 * Fills motion targets (`joint_target`/`cartesian_target`), `speed_ratio`, `blending_radius_mm`,
 * `wait_duration_s`, execution context (`tool_id`/`base_id`), flow-control linkage (`Label` step id
 * and GOTO/IF `jump_target_id`) and DI conditions (`condition.{io_port,trigger_on_state}` for WAIT DI /
 * IF). `SetDO` is not authored on the studio side (the controller sequencer fail-closes on it), so
 * `io_port`/`io_state` are left at 0.
 */
RDT::NetProtocol::ProgramDataStruct toRdt(const QVector<ProgramCommand>& prog);

/**
 * @brief RDT wire program → studio program (inverse of toRdt; restores the authoring params so the
 *        editor renders a controller-loaded program identically to a locally authored one).
 */
QVector<ProgramCommand> toHmi(const RDT::NetProtocol::ProgramDataStruct& rdt_prog);

} // namespace hexa::program_mapper

#endif // HEXA_PROGRAM_MAPPER_H
// --- END OF FILE: HexaStudio/hexa_backend/ProgramMapper.h ---
