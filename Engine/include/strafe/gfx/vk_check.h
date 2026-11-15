#pragma once

#include <volk.h>

#include <source_location>
#include <string_view>

namespace strafe::gfx {

std::string_view resultString(VkResult result);

namespace detail {
// Logs any result other than VK_SUCCESS through strafe::log at the caller's
// location. Returns false only for errors, which Vulkan defines as negative.
bool checkResult(VkResult result, std::string_view expression, std::source_location location);
} // namespace detail

} // namespace strafe::gfx

// Evaluates to true unless the call returned an error. Positive results such
// as VK_SUBOPTIMAL_KHR count as success but are logged as warnings, so calls
// that expect them handle the result themselves.
#define VK_CHECK(expr) ::strafe::gfx::detail::checkResult((expr), #expr, std::source_location::current())
