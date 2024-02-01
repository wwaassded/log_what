#include "log_what.hpp"

namespace what::Log {

/*********************************Class Text*********************************/
Text::Text(Text &&other) {
  __str = other.__str;
  other.__str = nullptr;
}

Text::~Text() {
  if (!__str) {
    free(__str);
    __str = nullptr;
  }
}

auto Text::Release() -> char * {
  auto str = __str;
  __str = nullptr;
  return str;
}

/*********************************log function*********************************/
void log(Verbosity verbosity, const char *file, unsigned int line,
         const char *format, ...) {
  va_list list;
  va_start(list, format);
  auto buffer = vastextprint(format, list);
  log_to_everywhere(verbosity, file, line, buffer.C_str());
  va_end(list);
}

void log_to_everywhere(Verbosity verbosity, const char *file, unsigned line,
                       const char *message) {
  char prifix[PRIFIX_WIDTH];
  print_prifix(prifix, sizeof prifix, verbosity, file, line);
  Message real_message = Message{};
  log_message(verbosity, real_message);
}

auto vastextprint(const char *format, va_list list) -> Text {
  char *buffer;
  int bytes = vasprintf(&buffer, format, list);
  ASSERT(bytes >= 0,
         "bad format fatal"); // TODO 应该使用自己的fatal log方式进行处理
  return Text(buffer);
}

} // namespace what::Log