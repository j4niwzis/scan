// The same five fields, taken out rather than only recognised. re2c does this
// with tags: `@name` binds the position where it stands, which is what a
// capture is.
#include <cstddef>

bool re2c_captures(const char* cursor, const char** positions) {
  const char* marker = nullptr;
  const char *t1, *t2, *t3, *t4, *t5, *t6, *t7, *t8, *t9, *t10;
  (void)marker;
  // The generator keeps its own variables for the tags it places, and they
  // have to be declared where it can see them. This block is where it writes
  // those declarations.
  /*!stags:re2c format = "const char *@@;\n"; */
  /*!re2c
     re2c:define:YYCTYPE = char;
     re2c:define:YYCURSOR = cursor;
     re2c:define:YYMARKER = marker;
     re2c:yyfill:enable = 0;
     re2c:tags = 1;

     field = [a-z]+;

     @t1 field @t2 "," @t3 field @t4 "," @t5 field @t6 ","
     @t7 field @t8 "," @t9 field @t10 "\x00" {
       positions[0] = t1; positions[1] = t2;
       positions[2] = t3; positions[3] = t4;
       positions[4] = t5; positions[5] = t6;
       positions[6] = t7; positions[7] = t8;
       positions[8] = t9; positions[9] = t10;
       return true;
     }
     * { return false; }
   */
}
