#pragma once

#include "crpc_except.h"

namespace crpc {
    class server final {
        void bind() {
            throw unimplemented_error("bind not implemented");
        }
    public:
        void connect_registry() {
            throw unimplemented_error("connect_registry not implemented");
        }
        void register_method() {
            throw unimplemented_error("register_method not implemented");
        }
        [[noreturn]] void run() {
            throw unimplemented_error("run not implemented");
        }
    };
}
