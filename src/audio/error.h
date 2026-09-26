#pragma once

#include <stdexcept>

namespace Audio {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

}  // namespace Audio
