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
  kNone = 0,              //!< No error
  kInitializationFailed,  //!< Initialization failed for some reason.
  kAlreadyInitialized,    //!< The renderer has already been initialized.
  kNoPhysicalDevice,      //!< No physical device available.
  kSurfaceCreationFailed, //!< Surface creation failed for some reason.
  kSurfaceNotSupported,   //!< Surface is not supported by the physical device.
  kInvalidControlCommand, //!< Invalid control command.
  kControlCommandFailed,  //!< The control command failed while executing.
  kUnknownControlCommand, //!< Unknown control command.
  kUnknownDSO,            //!< Unknown DSO.
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
    case Error::kNoPhysicalDevice: return "no physical device"s;
    case Error::kSurfaceCreationFailed: return "surface creation failed"s;
    case Error::kSurfaceNotSupported: return "surface not supported"s;
    case Error::kInvalidControlCommand: return "invalid control command"s;
    case Error::kControlCommandFailed: return "control command failed"s;
    case Error::kUnknownControlCommand: return "unknown control command"s;
    case Error::kUnknownDSO: return "unknown DSO"s;
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

