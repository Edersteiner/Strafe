#pragma once

// Preprocessor definitons and flags
namespace strafe::flags {

#ifdef STRAFE_DEBUG
  inline constexpr bool kEnableValidation = true;
#else
  inline constexpr bool kEnableValidation = false;
#endif

}
