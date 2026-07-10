#ifndef SLE_MEASURE_CMD_H
#define SLE_MEASURE_CMD_H

#include <QString>

namespace SleMeasureCmd {

bool sendStartMeasure(int deviceId = 0, int durationMs = 1800);
bool sendStopMeasure(int deviceId = 0, int durationMs = 1200);
QString protocolSummary();

} // namespace SleMeasureCmd

#endif
