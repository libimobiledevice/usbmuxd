# - Check if the given constant exists (as an enum, define, or whatever)
# CHECK_CONSTANT_EXISTS (CONSTANT HEADER VARIABLE)
#
#  CONSTANT - the name of the constant you are interested in
#  HEADER - the header(s) where the prototype should be declared
#  VARIABLE - variable to store the result
#
# The following variables may be set before calling this macro to
# modify the way the check is run:
#
#  CMAKE_REQUIRED_FLAGS = string of compile command line flags
#  CMAKE_REQUIRED_DEFINITIONS = list of macros to define (-DFOO=bar)
#  CMAKE_REQUIRED_INCLUDES = list of include directories
#
# Example: CHECK_CONSTANT_EXISTS(O_NOFOLLOW fcntl.h HAVE_O_NOFOLLOW)


INCLUDE(CheckCSourceCompiles)

MACRO (CHECK_CONSTANT_EXISTS _CONSTANT _HEADER _RESULT)
   SET(_INCLUDE_FILES)
   FOREACH (it ${_HEADER})
      SET(_INCLUDE_FILES "${_INCLUDE_FILES}#include <${it}>\n")
   ENDFOREACH (it)

   SET(_CHECK_CONSTANT_SOURCE_CODE "
${_INCLUDE_FILES}
void cmakeRequireConstant(int dummy,...){(void)dummy;}
int main()
{
   cmakeRequireConstant(0,${_CONSTANT});
   return 0;
}
")
   CHECK_C_SOURCE_COMPILES("${_CHECK_CONSTANT_SOURCE_CODE}" ${_RESULT})

ENDMACRO (CHECK_CONSTANT_EXISTS)

