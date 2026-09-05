#include <cstddef>

bool re2c_address(const char* cursor) {
  const char* marker = nullptr;
  (void)marker;
  /*!re2c
     re2c:define:YYCTYPE = char;
     re2c:define:YYCURSOR = cursor;
     re2c:define:YYMARKER = marker;
     re2c:yyfill:enable = 0;

     atom  = [-a-zA-Z0-9!#$%&'*+/=?^_`|~]+;
     label = [a-zA-Z0-9] ([-a-zA-Z0-9]* [a-zA-Z0-9])?;

     atom ("." atom)* "@" (label ".")+ label "\x00" { return true; }
     *                                              { return false; }
   */
}
