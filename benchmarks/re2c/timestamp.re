#include <cstddef>

bool re2c_timestamp(const char* cursor) {
  const char* marker = nullptr;
  (void)marker;
  /*!re2c
     re2c:define:YYCTYPE = char;
     re2c:define:YYCURSOR = cursor;
     re2c:define:YYMARKER = marker;
     re2c:yyfill:enable = 0;

     digit = [0-9];
     digit{4} "-" digit{2} "-" digit{2} "T"
     digit{2} ":" digit{2} ":" digit{2} "\x00" { return true; }
     *                                          { return false; }
   */
}
