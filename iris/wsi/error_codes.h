#ifndef HEV_IRIS_WSI_ERROR_H_
#define HEV_IRIS_WSI_ERROR_H_
/*! \file
 * \brief \ref iris::wsi::Error definition.
 */

#include <string>
#include <system_error>

namespace iris::wsi {

//! \brief WSI error codes.
enum class ErrorCodes {
  kNone = 0,
  kXCBError,
  kWin32Error,
}; // enum class ErrorCodes

//! \brief Implements std::error_category for \ref ErrorCodes
class ErrorCodesCategory : public std::error_category {
public:
  virtual ~ErrorCodesCategory() {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::wsi::ErrorCodes";
  }

  //! \brief Convert an int representing an ErrorCodes into a std::string.
  virtual std::string message(int code) const override {
    using namespace std::string_literals;
    switch (static_cast<ErrorCodes>(code)) {
    case ErrorCodes::kNone: return "none"s;
    case ErrorCodes::kXCBError: return "XCB error"s;
    case ErrorCodes::kWin32Error: return "Win32 error"s;
    }
    return "unknown"s;
  }
}; // class ErrorCodesCategory

//! The global instance of the ErrorCodesCategory.
inline ErrorCodesCategory const gErrorCodesCategory;

/*! \brief Get the global instance of the ErrorCodesCategory.
 * \return \ref gErrorCodesCategory
 */
inline std::error_category const& GetErrorCodesCategory() {
  return gErrorCodesCategory;
}

/*! \brief Make a std::error_code from a \ref ErrorCodes.
 * \return std::error_code
 */
inline std::error_code make_error_code(ErrorCodes e) noexcept {
  return std::error_code(static_cast<int>(e), GetErrorCodesCategory());
}

} // namespace iris::wsi

namespace std {

template <>
struct is_error_code_enum<iris::wsi::ErrorCodes> : public true_type {};

} // namespace std

#endif // HEV_IRIS_WSI_ERROR_H_

