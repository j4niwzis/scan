// Generated into a benchmark by re2c. One pattern per file, as everywhere here.
#include <cstddef>

bool re2c_word(const char* cursor) {
  const char* marker = nullptr;
  (void)marker;
  /*!re2c
     re2c:define:YYCTYPE = char;
     re2c:define:YYCURSOR = cursor;
     re2c:define:YYMARKER = marker;
     re2c:yyfill:enable = 0;

     [a-z]+ "\x00" { return true; }
     *              { return false; }
   */
}
