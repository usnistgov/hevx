#ifndef HEV_IRIS_WSI_ERROR_H_
#define HEV_IRIS_WSI_ERROR_H_
/*! \file
 * \brief \ref iris::wsi::Error definition.
 */

#include <string>
#include <system_error>

namespace iris::wsi {

//! \brief WSI errors.
enum class Error {
  kNone = 0,
  kNoDisplay,
  kXError,
  kWin32Error,
}; // enum class Error

//! \brief Implements std::error_category for \ref Error
class ErrorCategory : public std::error_category {
public:
  virtual ~ErrorCategory() {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::wsi::Error";
  }

  //! \brief Convert an int representing an Error into a std::string.
  virtual std::string message(int code) const override {
    using namespace std::string_literals;
    switch (static_cast<Error>(code)) {
    case Error::kNone: return "none"s;
    case Error::kNoDisplay: return "no display"s;
    case Error::kXError: return "X error"s;
    case Error::kWin32Error: return "Win32 error"s;
    }
    return "unknown"s;
  }
}; // class ErrorCategory

//! The global instance of the ErrorCategory.
inline ErrorCategory const gErrorCategory;

/*! \brief Get the global instance of the ErrorCategory.
 * \return \ref gErrorCategory
 */
inline std::error_category const& GetErrorCategory() {
  return gErrorCategory;
}

/*! \brief Make a std::error_code from a \ref Error.
 * \return std::error_code
 */
inline std::error_code make_error_code(Error e) noexcept {
  return std::error_code(static_cast<int>(e), GetErrorCategory());
}

} // namespace iris::wsi

namespace std {

template <>
struct is_error_code_enum<iris::wsi::Error> : public true_type {};

} // namespace std

#endif // HEV_IRIS_WSI_ERROR_H_

