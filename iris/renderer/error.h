#ifndef HEV_IRIS_ERROR_H_
#define HEV_IRIS_ERROR_H_
/*! \file
 * \brief \ref iris::Renderer::Error definition.
 */

#include <string>
#include <system_error>

namespace iris::Renderer {

//! \brief Renderer errors.
enum class Error {
  kNone = 0,                 //!< No error
  kInitializationFailed = 1, //!< Initialization failed for some reason.
  kAlreadyInitialized = 2,   //!< The renderer has already been initialized.
  kNoPhysicalDevice = 3,     //!< No physical device available.
};

//! \brief Implements std::error_category for \ref Error
class ErrorCategory : public std::error_category {
public:
  virtual ~ErrorCategory() noexcept {}

  //! \brief Get the name of this category.
  virtual const char* name() const noexcept override {
    return "iris::Renderer::Error";
  }

  //! \brief Convert an int representing an Error into a std::string.
  virtual std::string message(int code) const override {
    using namespace std::string_literals;
    switch (static_cast<Error>(code)) {
    case Error::kNone: return "none"s;
    case Error::kInitializationFailed: return "initialization failed"s;
    case Error::kAlreadyInitialized: return "already initialized"s;
    case Error::kNoPhysicalDevice: return "no physical device";
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

} // namespace iris::Renderer

namespace std {

template <>
struct is_error_code_enum<iris::Renderer::Error> :
                                     public
                                       true_type{};

} // namespace std

#endif // HEV_IRIS_ERROR_H_

