#pragma once

// Perfilamento opcional com Tracy. O build de release nao define
// ARESTRIX_TRACY e os macros viram no-ops, sem incluir o cabecalho do cliente.
#if defined(ARESTRIX_TRACY)
#include <tracy/Tracy.hpp>
#define ARX_ZONE_SCOPED() ZoneScoped
#define ARX_ZONE_NAMED(name) ZoneScopedN(name)
#define ARX_FRAME_MARK() FrameMark
#define ARX_PLOT(name, value) TracyPlot(name, static_cast<int64_t>(value))
#else
#define ARX_ZONE_SCOPED() do { } while (0)
#define ARX_ZONE_NAMED(name) do { } while (0)
#define ARX_FRAME_MARK() do { } while (0)
#define ARX_PLOT(name, value) do { } while (0)
#endif
