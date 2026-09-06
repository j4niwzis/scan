#include <cstddef>

bool re2c_csv(const char* cursor) {
  const char* marker = nullptr;
  (void)marker;
  /*!re2c
     re2c:define:YYCTYPE = char;
     re2c:define:YYCURSOR = cursor;
     re2c:define:YYMARKER = marker;
     re2c:yyfill:enable = 0;

     field = [a-z]+;
     field "," field "," field "," field "," field "\x00" { return true; }
     *                                                     { return false; }
   */
}
