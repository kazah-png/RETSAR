#pragma once
#include "lab_control_channel.hpp"

namespace seats::rds::lab {
// One operation, a fixed two-minute lease, and no outbound RDS call on the pipe
// thread. RequestStop never waits for a callback; a stuck callback retains its
// COM reference/module until it returns. It is not forcefully terminated.
class Worker : public Handler {
public:
    virtual void RequestStop() noexcept=0;
    virtual bool Stopped() const noexcept=0;
};
HRESULT StartWorker(IRetsarRdsLabControl* control,std::shared_ptr<Worker>& worker) noexcept;
}