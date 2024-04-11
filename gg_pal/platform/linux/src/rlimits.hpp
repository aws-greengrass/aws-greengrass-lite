#pragma once

#include <system_error>
std::error_code setFdLimit(int limit) noexcept;

std::error_code resetFdLimit() noexcept;
