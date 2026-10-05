#pragma once

#include <taskflow/taskflow.hpp>
#include <iostream>
#include <chrono>
#include <string>
#include <syncstream>

namespace taskforge {

/**
 * @class LiveTelemetryObserver
 * @brief Observer plug-and-play desacoplado para monitoramento de tarefas e agentes do Taskflow.
 *
 * Utiliza o tf::ObserverInterface nativo do Taskflow sem modificar nem uma única linha
 * do código das tarefas ou bibliotecas.
 */
class LiveTelemetryObserver : public tf::ObserverInterface {
public:
    LiveTelemetryObserver() = default;

    void set_up(size_t num_workers) override final {
        std::osyncstream(std::cout) << "[NF_TELEMETRY:SETUP] Workers=" << num_workers << "\n";
    }

    void on_entry(tf::WorkerView w, tf::TaskView tv) override final {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        // Emite evento em linha única thread-safe sincronizada
        std::osyncstream(std::cout) << "[NF_TELEMETRY:ENTER] Task=" << tv.name() 
                                    << " Worker=" << w.id() 
                                    << " Time=" << now_ns << "\n";
    }

    void on_exit(tf::WorkerView w, tf::TaskView tv) override final {
        auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
        std::osyncstream(std::cout) << "[NF_TELEMETRY:EXIT] Task=" << tv.name() 
                                    << " Worker=" << w.id() 
                                    << " Time=" << now_ns << "\n";
    }
};

} // namespace taskforge
