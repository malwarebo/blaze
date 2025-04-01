#pragma once

// Include the main client headers
#include "../../../lib/http_client.hpp"

namespace blaze {
    // Library version
    constexpr char VERSION[] = "1.0.0";
    
    // Function to get library version
    inline const char* getVersion() {
        return VERSION;
    }
}
