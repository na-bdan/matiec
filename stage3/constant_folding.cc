/*
 *  matiec - a compiler for the programming languages defined in IEC 61131-3
 *
 *  Copyright (C) 2003-2011  Mario de Sousa (msousa@fe.up.pt)
 *  Copyright (C) 2012       Manuele Conti (conti.ma@alice.it)
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This code is made available on the understanding that it will not be
 * used in safety-critical situations without a full and competent review.
 */

/*
 * An IEC 61131-3 compiler.
 *
 * Based on the
 * FINAL DRAFT - IEC 61131-3, 2nd Ed. (2001-12-10)
 *
 */





/* Do constant folding...
 *
 * I.e., Determine the value of all expressions in which only constant values (i.e. literals) are used.
 * The (constant) result of each operation is stored (annotated) in the respective operation symbol 
 * (e.g.: add_expression_c) in the abstract syntax tree,
 *
 * For example:
 *       2 + 3         -> the constant value '5'    is stored in the add_expression_c symbol.
 *       22.2 - 5.0    -> the constant value '17.2' is stored in the add_expression_c symbol.
 *       etc...
 *
 *
 * NOTE 1 
 *      Some operations and constants can have multiple data types. For example,
 *        1 AND 0
 *      may be either a BOOL, BYTE, WORD or LWORD.
 *
 *      The same happens with 
 *        1 + 2
 *      which may be signed (e.g. INT) or unsigned (UINT)
 *
 *      For the above reason, instead of storing a single constant value, we actually store 4:
 *        - bool
 *        - uint64
 *        -  int64
 *        - real64
 *
 *      Additionally, since the result of an operation may result in an overflow, we actually
 *      store the result inside a struct (defined in absyntax.hh)
 *
 *             ** During stage 3 (semantic analysis/checking) we will be doing constant folding.
 *              * That algorithm will anotate the abstract syntax tree with the result of operations
 *              * on literals (i.e. 44 + 55 will store the result 99).
 *              * Since the same source code (e.g. 1 + 0) may actually be a BOOL or an ANY_INT,
 *              * or an ANY_BIT, we need to handle all possibilities, and determine the result of the
 *              * operation assuming each type.
 *              * For this reason, we have one entry for each possible type, with some expressions
 *              * having more than one entry filled in!
 *              **
 *             typedef enum { cs_undefined,   // not defined --> const_value is not valid!
 *                            cs_const_value, // const value is valid
 *                            cs_overflow     // result produced overflow or underflow --> const_value is not valid!
 *                          } const_status_t;
 *    
 *             typedef struct {
 *                 const_status_t status;
 *                 real64_t       value; 
 *             } const_value_real64_t;
 *             const_value_real64_t *const_value_real64; // when NULL --> UNDEFINED
 *             
 *             typedef struct {
 *                 const_status_t status;
 *                 int64_t        value; 
 *             } const_value_int64_t;
 *             const_value_int64_t *const_value_int64; // when NULL --> UNDEFINED
 *             
 *             typedef struct {
 *                 const_status_t status;
 *                 uint64_t       value; 
 *             } const_value_uint64_t;
 *             const_value_uint64_t *const_value_uint64; // when NULL --> UNDEFINED
 *             
 *             typedef struct {
 *                 const_status_t status;
 *                 bool           value; 
 *             } const_value_bool_t;
 *             const_value_bool_t *const_value_bool; // when NULL --> UNDEFINED
 *
 *
 *
 * NOTE 2 
 *    This file does not print out any error messages!
 *    We cannot really print out error messages when we find an overflow. Since each operation
 *    (symbol in the absract syntax tree for that operation) will have up to 4 constant results,
 *    it may happen that some of them overflow, while other do not.
 *    We must wait for data type checking to determine the exact data type of each expression
 *    before we can decide whether or not we should print out an overflow error message.
 *
 *    For this reason, this visitor merely annotates the abstract syntax tree, and leaves the
 *    actuall printing of errors for the print_datatype_errors_c class!
 */

#include "constant_folding.hh"
#include <stdlib.h> /* required for malloc() */

#include <string.h>  /* required for strlen() */
// #include <stdlib.h>  /* required for atoi() */
#include <errno.h>   /* required for errno */

#include "../main.hh" // required for uint8_t, real_64_t, ..., and the macros NAN, INFINITY, INT8_MAX, REAL32_MAX, ... */






#define FIRST_(symbol1, symbol2) (((symbol1)->first_order < (symbol2)->first_order)   ? (symbol1) : (symbol2))
#define  LAST_(symbol1, symbol2) (((symbol1)->last_order  > (symbol2)->last_order)    ? (symbol1) : (symbol2))

#define STAGE3_ERROR(error_level, symbol1, symbol2, ...) {                                                                  \
  if (current_display_error_level >= error_level) {                                                                         \
    fprintf(stderr, "%s:%d-%d..%d-%d: error: ",                                                                             \
            FIRST_(symbol1,symbol2)->first_file, FIRST_(symbol1,symbol2)->first_line, FIRST_(symbol1,symbol2)->first_column,\
                                                 LAST_(symbol1,symbol2) ->last_line,  LAST_(symbol1,symbol2) ->last_column);\
    fprintf(stderr, __VA_ARGS__);                                                                                           \
    fprintf(stderr, "\n");                                                                                                  \
    error_count++;                                                                                                     \
  }                                                                                                                         \
}


#define STAGE3_WARNING(symbol1, symbol2, ...) {                                                                             \
    fprintf(stderr, "%s:%d-%d..%d-%d: warning: ",                                                                           \
            FIRST_(symbol1,symbol2)->first_file, FIRST_(symbol1,symbol2)->first_line, FIRST_(symbol1,symbol2)->first_column,\
                                                 LAST_(symbol1,symbol2) ->last_line,  LAST_(symbol1,symbol2) ->last_column);\
    fprintf(stderr, __VA_ARGS__);                                                                                           \
    fprintf(stderr, "\n");                                                                                                  \
    warning_found = true;                                                                                                   \
}












#define NEW_CVALUE(dtype, symbol) \
 (symbol->const_value_##dtype) = new(symbol_c::const_value_##dtype##_t); \
 if ((symbol->const_value_##dtype) == NULL) ERROR; \
 (symbol->const_value_##dtype)->status = symbol_c::cs_undefined;

#define SET_CVALUE(dtype, symbol, new_value)  ((symbol)->const_value_##dtype->value) = new_value; ((symbol)->const_value_##dtype->status) = symbol_c::cs_const_value;
#define GET_CVALUE(dtype, symbol)             ((symbol)->const_value_##dtype->value)
#define SET_OVFLOW(dtype, symbol)             ((symbol)->const_value_##dtype->status) = symbol_c::cs_overflow
    /* The following test is correct in the presence of a NULL pointer, as the logical evaluation will be suspended as soon as the first condition is false! */
#define VALID_CVALUE(dtype, symbol)           ((NULL != (symbol)->const_value_##dtype) && (symbol_c::cs_const_value == (symbol)->const_value_##dtype->status))
#define ISZERO_CVALUE(dtype, symbol)          ((VALID_CVALUE(dtype, symbol)) && (GET_CVALUE(dtype, symbol) == 0))

#define DO_BINARY_OPER(dtype, oper, otype)\
	if (VALID_CVALUE(dtype, symbol->r_exp) && VALID_CVALUE(dtype, symbol->l_exp)) {                                \
		NEW_CVALUE(otype, symbol);                                                                             \
		SET_CVALUE(otype, symbol, GET_CVALUE(dtype, symbol->l_exp) oper GET_CVALUE(dtype, symbol->r_exp));     \
	}

#define DO_UNARY_OPER(dtype, oper, arg)\
	if (VALID_CVALUE(dtype, arg)) {                                                                                \
		NEW_CVALUE(dtype, symbol);                                                                             \
		SET_CVALUE(dtype, symbol, oper GET_CVALUE(dtype, arg));                                                \
	}





/***********************************************************************/
/***********************************************************************/
/***********************************************************************/
/***            convert string to numerical value                    ***/
/***********************************************************************/
/***********************************************************************/
/***********************************************************************/



  /* To allow the compiler to be portable, we cannot assume that int64_t is mapped onto long long int,
   * so we cannot call strtoll() and strtoull() in extract_int64() and extract_uint64().
   *
   * So, we create our own strtouint64() and strtoint64() functions.
   * (We actually call them matiec_strtoint64() so they will not clash with any function
   *  that may be added to the standard library in the future).
   * We actually create several of each, and let the compiler choose which is the correct one,
   * by having it resolve the call to the overloaded function. For the C++ compiler to be able
   * to resolve this ambiguity, we need to add a dummy parameter to each function!
   *
   * TODO: support platforms in which int64_t is mapped onto int !! Is this really needed?
   */
static  int64_t matiec_strtoint64 (         long      int *dummy, const char *nptr, char **endptr, int base) {return strtol  (nptr, endptr, base);}
static  int64_t matiec_strtoint64 (         long long int *dummy, const char *nptr, char **endptr, int base) {return strtoll (nptr, endptr, base);}
  
static uint64_t matiec_strtouint64(unsigned long      int *dummy, const char *nptr, char **endptr, int base) {return strtoul (nptr, endptr, base);}
static uint64_t matiec_strtouint64(unsigned long long int *dummy, const char *nptr, char **endptr, int base) {return strtoull(nptr, endptr, base);}


/* extract the value of an integer from an integer_c object !! */
/* NOTE: it must ignore underscores! */
/* NOTE: To follow the basic structure used throughout the compiler's code, we should really be
 * writing this as a visitor_c (and do away with the dynamic casts!), but since we only have 3 distinct 
 * symbol class types to handle, it is probably easier to read if we write it as a standard function... 
 */
int64_t extract_int64_value(symbol_c *sym, bool *overflow) {
  int64_t      ret;
  std::string  str = "";
  char        *endptr;
  const char  *value;
  int          base;
  integer_c         *integer;
  hex_integer_c     *hex_integer;
  octal_integer_c   *octal_integer;
  binary_integer_c  *binary_integer;

   if       ((integer        = dynamic_cast<integer_c *>(sym))        != NULL) {value = integer       ->value + 0; base = 10;}
   else  if ((hex_integer    = dynamic_cast<hex_integer_c *>(sym))    != NULL) {value = hex_integer   ->value + 3; base = 16;}
   else  if ((octal_integer  = dynamic_cast<octal_integer_c *>(sym))  != NULL) {value = octal_integer ->value + 2; base =  8;}
   else  if ((binary_integer = dynamic_cast<binary_integer_c *>(sym)) != NULL) {value = binary_integer->value + 2; base =  2;}
   else  ERROR;

  for(unsigned int i = 0; i < strlen(value); i++)
    if (value[i] != '_')  str += value[i];

  errno = 0; // since strtoXX() may legally return 0, we must set errno to 0 to detect errors correctly!
  ret = matiec_strtoint64((int64_t *)NULL, str.c_str(), &endptr, base);
  if (overflow != NULL)
    *overflow = (errno == ERANGE);
  if (((errno != 0) && (errno != ERANGE)) || (*endptr != '\0'))
    ERROR;

  return ret;
}



uint64_t extract_uint64_value(symbol_c *sym, bool *overflow) {
  uint64_t     ret;
  std::string  str = "";
  char        *endptr;
  const char  *value;
  int          base;
  integer_c         *integer;
  hex_integer_c     *hex_integer;
  octal_integer_c   *octal_integer;
  binary_integer_c  *binary_integer;

   if       ((integer        = dynamic_cast<integer_c *>(sym))        != NULL) {value = integer       ->value + 0; base = 10;}
   else  if ((hex_integer    = dynamic_cast<hex_integer_c *>(sym))    != NULL) {value = hex_integer   ->value + 3; base = 16;}
   else  if ((octal_integer  = dynamic_cast<octal_integer_c *>(sym))  != NULL) {value = octal_integer ->value + 2; base =  8;}
   else  if ((binary_integer = dynamic_cast<binary_integer_c *>(sym)) != NULL) {value = binary_integer->value + 2; base =  2;}
   else  ERROR;

  for(unsigned int i = 0; i < strlen(value); i++)
    if (value[i] != '_')  str += value[i];

  errno = 0; // since strtoXX() may legally return 0, we must set errno to 0 to detect errors correctly!
  ret = matiec_strtouint64((uint64_t *)NULL, str.c_str(), &endptr, base);
  if (overflow != NULL)
    *overflow = (errno == ERANGE);
  if (((errno != 0) && (errno != ERANGE)) || (*endptr != '\0'))
    ERROR;

  return ret;
}



/* extract the value of a real from an real_c object !! */
/* NOTE: it must ignore underscores! */
/* From iec_bison.yy
 *  real:
 *   real_token		{$$ = new real_c($1, locloc(@$));}
 * | fixed_point_token	{$$ = new real_c($1, locloc(@$));}
 *
 * From iec_flex.ll
 * {real}			{yylval.ID=strdup(yytext); return real_token;}
 * {fixed_point}		{yylval.ID=strdup(yytext); return fixed_point_token;}
 *
 * real		{integer}\.{integer}{exponent}
 * fixed_point		{integer}\.{integer}
 * exponent        [Ee]([+-]?){integer}
 * integer         {digit}((_?{digit})*)
 */
real64_t extract_real_value(symbol_c *sym, bool *overflow) {
  std::string str = "";
  real_c *real_sym;
  char   *endptr;
  real64_t ret;

  if ((real_sym = dynamic_cast<real_c *>(sym)) == NULL) ERROR;
  for(unsigned int i = 0; i < strlen(real_sym->value); i++)
    if (real_sym->value[i] != '_') str += real_sym->value[i];
    
  errno = 0; // since strtoXX() may legally return 0, we must set errno to 0 to detect errors correctly!
  #if    (real64_t  == float)
    ret = strtof(str.c_str(),  &endptr);
  #elif  (real64_t  == double)
    ret = strtod(str.c_str(),  &endptr);
  #elif  (real64_t  == long_double)
    ret = strtold(str.c_str(), &endptr);
  #else 
    #error Could not determine which data type is being used for real64_t (defined in absyntax.hh). Aborting!
  #endif
  if (overflow != NULL)
    *overflow = (errno == ERANGE);
  if (((errno != 0) && (errno != ERANGE)) || (*endptr != '\0'))
    ERROR;

  return ret;
}





/***********************************************************************/
/***********************************************************************/
/***********************************************************************/
/***        Functions to check for overflow situation                ***/
/***********************************************************************/
/***********************************************************************/
/***********************************************************************/


/* NOTE:
 *   Most of the conditions to detect overflows on signed and unsigned integer operations were adapted from
 *   https://www.securecoding.cert.org/confluence/display/seccode/INT32-C.+Ensure+that+operations+on+signed+integers+do+not+result+in+overflow?showComments=false
 *   https://www.securecoding.cert.org/confluence/display/seccode/INT30-C.+Ensure+that+unsigned+integer+operations+do+not+wrap
 */

/* NOTE: If at all possible, all overflow tests are done by pre-condition tests, i.e. tests that 
 *       can be run _before_ the operation is executed, and therefore without accessing the result!
 *
 *       The exception is for real/floating point values, that simply test if the result is NaN (not a number).
 */

/* res = a + b */
static void CHECK_OVERFLOW_uint64_SUM(symbol_c *res, symbol_c *a, symbol_c *b) {
	if (!VALID_CVALUE(uint64, res))
		return;
	/* Test by post-condition: If sum is smaller than either operand => overflow! */
	// if (GET_CVALUE(uint64, res) < GET_CVALUE(uint64, a))
	/* Test by pre-condition: If (UINT64_MAX - a) < b => overflow! */
	if ((UINT64_MAX - GET_CVALUE(uint64, a)) < GET_CVALUE(uint64, b))
		SET_OVFLOW(uint64, res);
}


/* res = a - b */
static void CHECK_OVERFLOW_uint64_SUB(symbol_c *res, symbol_c *a, symbol_c *b) {
	if (!VALID_CVALUE(uint64, res))
		return;
	/* Test by post-condition: If diference is larger than a => overflow! */
	// if (GET_CVALUE(uint64, res) > GET_CVALUE(uint64, a))
	/* Test by pre-condition: if b > a => overflow! */
	if (GET_CVALUE(uint64, b) > GET_CVALUE(uint64, a))
		SET_OVFLOW(uint64, res);
}


/* res = a * b */
static void CHECK_OVERFLOW_uint64_MUL(symbol_c *res, symbol_c *a, symbol_c *b) {
	if (!VALID_CVALUE(uint64, res))
		return;
	/* Test by pre-condition: If (UINT64_MAX / a) < b => overflow! */
	if ((UINT64_MAX / GET_CVALUE(uint64, a)) < GET_CVALUE(uint64, b))
		SET_OVFLOW(uint64, res);
}


/* res = a / b */
static void CHECK_OVERFLOW_uint64_DIV(symbol_c *res, symbol_c *a, symbol_c *b) {
	if (!VALID_CVALUE(uint64, res))
		return;
	if (GET_CVALUE(uint64, b) == 0) /* division by zero! */
		SET_OVFLOW(uint64, res);
}


/* res = a MOD b */
static void CHECK_OVERFLOW_uint64_MOD(symbol_c *res, symbol_c *a, symbol_c *b) {
	if (!VALID_CVALUE(uint64, res))
		return;
	/* no overflow condition exists, including division by zero, which IEC 61131-3 considers legal for MOD operation! */
	if (false) 
		SET_OVFLOW(uint64, res);
}


/* res = a + b */
static void CHECK_OVERFLOW_int64_SUM(symbol_c *res, symbol_c *a_ptr, symbol_c *b_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	int64_t b = GET_CVALUE(int64, b_ptr);
	/* The following test is valid no matter what representation is being used (e.g. two's complement, etc...) */
	if (((b > 0) && (a > (INT64_MAX - b)))
	 || ((b < 0) && (a < (INT64_MIN - b))))
		SET_OVFLOW(int64, res);
}


/* res = a - b */
static void CHECK_OVERFLOW_int64_SUB(symbol_c *res, symbol_c *a_ptr, symbol_c *b_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	int64_t b = GET_CVALUE(int64, b_ptr);
	/* The following test is valid no matter what representation is being used (e.g. two's complement, etc...) */
	if (((b > 0) && (a < (INT64_MIN + b)))
	 || ((b < 0) && (a > (INT64_MAX + b))))
		SET_OVFLOW(int64, res);
}


/* res = a * b */
static void CHECK_OVERFLOW_int64_MUL(symbol_c *res, symbol_c *a_ptr, symbol_c *b_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	int64_t b = GET_CVALUE(int64, b_ptr);
	if (   ( (a > 0) &&  (b > 0) &&             (a > (INT64_MAX / b))) 
	    || ( (a > 0) && !(b > 0) &&             (b < (INT64_MIN / a))) 
	    || (!(a > 0) &&  (b > 0) &&             (a < (INT64_MIN / b))) 
	    || (!(a > 0) && !(b > 0) && (a != 0) && (b < (INT64_MAX / a))))
		SET_OVFLOW(int64, res);
}


/* res = a / b */
static void CHECK_OVERFLOW_int64_DIV(symbol_c *res, symbol_c *a_ptr, symbol_c *b_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	int64_t b = GET_CVALUE(int64, b_ptr);
	if ((b == 0) || ((a == INT64_MIN) && (b == -1)))
		SET_OVFLOW(int64, res);
}


/* res = a MOD b */
static void CHECK_OVERFLOW_int64_MOD(symbol_c *res, symbol_c *a_ptr, symbol_c *b_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	int64_t b = GET_CVALUE(int64, b_ptr);
	/* IEC 61131-3 standard says IN1 MOD IN2 must be equivalent to
	 *  IF (IN2 = 0) THEN OUT:=0 ; ELSE OUT:=IN1 - (IN1/IN2)*IN2 ; END_IF
	 *
	 * Note that, when IN1 = INT64_MIN, and IN2 = -1, an overflow occurs in the division,
	 * so although the MOD operation should be OK, acording to the above definition, we actually have an overflow!!
	 *
	 * On the other hand, division by 0 is OK!!
	 */
	if ((a == INT64_MIN) && (b == -1))
		SET_OVFLOW(int64, res);
}


/* res = - a */
static void CHECK_OVERFLOW_int64_NEG(symbol_c *res, symbol_c *a_ptr) {
	if (!VALID_CVALUE(int64, res))
		return;
	int64_t a = GET_CVALUE(int64, a_ptr);
	if (a == INT64_MIN)
		SET_OVFLOW(int64, res);
}



static void CHECK_OVERFLOW_real64(symbol_c *res_ptr) {
	if (!VALID_CVALUE(real64, res_ptr))
		return;
	real64_t res = GET_CVALUE(real64, res_ptr);
	/* NaN => underflow, overflow, number is a higher precision format, is a complex number (IEEE standard) */
	/* The IEC 61131-3 clearly states in section '2.5.1.5.2 Numerical functions':
	 * "It is an error if the result of evaluation of one of these [numerical] functions exceeds the range of values
	 *  specified for the data type of the function output, or if division by zero is attempted."
	 * For this reason, any operation that has as a result a positive or negative inifinity, is also an error!
	 */
	if ((isnan(res)) || (res == INFINITY) || (res == -INFINITY))
		SET_OVFLOW(real64, res_ptr);
}







/***********************************************************************/
/***********************************************************************/
/***********************************************************************/
/***        The constant_folding_c                                   ***/
/***********************************************************************/
/***********************************************************************/
/***********************************************************************/






constant_folding_c::constant_folding_c(symbol_c *symbol) {
    error_count = 0;
    warning_found = false;
    current_display_error_level = 0;
    
    /* check whether the platform on which the compiler is being run implements IEC 559 floating point data types. */
    symbol_c null_symbol;
    if (! (std::numeric_limits<real64_t>::is_iec559) )
        STAGE3_WARNING(&null_symbol, &null_symbol, "The platform running the compiler does not implement IEC 60559 floating point numbers. "
                                                   "Any error and/or warning messages related to overflow/underflow of the result of operations on REAL/LREAL literals "
                                                   "(i.e. constant folding) may themselves be erroneous, although are most probably correct."
                                                   "However, more likely is the possible existance of overflow/underflow errors that are not detected.");
}


constant_folding_c::~constant_folding_c(void) {
}


int constant_folding_c::get_error_count() {
	return error_count;
}


/*********************/
/* B 1.2 - Constants */
/*********************/
/******************************/
/* B 1.2.1 - Numeric Literals */
/******************************/
void *constant_folding_c::visit(real_c *symbol) {
	bool overflow;
	NEW_CVALUE(real64, symbol);	SET_CVALUE(real64, symbol, extract_real_value(symbol, &overflow));
	if (overflow) SET_OVFLOW(real64, symbol);
	return NULL;
}


void *constant_folding_c::visit(integer_c *symbol) {
	bool overflow;
	NEW_CVALUE( int64, symbol);	SET_CVALUE( int64, symbol, extract_int64_value (symbol, &overflow));
	if (overflow) SET_OVFLOW(int64, symbol);
	NEW_CVALUE(uint64, symbol);	SET_CVALUE(uint64, symbol, extract_uint64_value(symbol, &overflow));
	if (overflow) SET_OVFLOW(uint64, symbol);
	return NULL;
}


void *constant_folding_c::visit(neg_real_c *symbol) {
	symbol->exp->accept(*this);
	DO_UNARY_OPER(real64, -, symbol->exp);
	CHECK_OVERFLOW_real64(symbol);
	return NULL;
}

/* | '-' integer	{$$ = new neg_integer_c($2, locloc(@$));} */
void *constant_folding_c::visit(neg_integer_c *symbol) {
	symbol->exp->accept(*this);
	DO_UNARY_OPER(int64, -, symbol->exp);
	CHECK_OVERFLOW_int64_NEG(symbol, symbol->exp);
	/* NOTE 1: INT64_MIN = -(INT64_MAX + 1)   ---> assuming two's complement representation!!!
	 * NOTE 2: if the user happens to want INT_MIN, that value will first be parsed as a positive integer, before being negated here.
	 * However, the positive value cannot be stored inside an int64! So, in this case, we will get the value from the uint64 cvalue.
	 */
	// if (INT64_MIN == -INT64_MAX - 1) // We do not really need to check that the platform uses two's complement
	if (VALID_CVALUE(uint64, symbol->exp) && (GET_CVALUE(uint64, symbol->exp) == -INT64_MIN)) { // How do we stop the compiler from complaining about a comparison between int and unsigned int?
		NEW_CVALUE(int64, symbol); // in principle, if the above condition is true, then no new cvalue was created by DO_UNARY_OPER(). Not that it would be a problem to create a new one!
		SET_CVALUE(int64, symbol, INT64_MIN);
	}
	return NULL;
}


void *constant_folding_c::visit(binary_integer_c *symbol) {
	bool overflow;
	NEW_CVALUE( int64, symbol);	SET_CVALUE( int64, symbol, extract_int64_value (symbol, &overflow));
	if (overflow) SET_OVFLOW(int64, symbol);
	NEW_CVALUE(uint64, symbol);	SET_CVALUE(uint64, symbol, extract_uint64_value(symbol, &overflow));
	if (overflow) SET_OVFLOW(uint64, symbol);
	return NULL;
}


void *constant_folding_c::visit(octal_integer_c *symbol) {
	bool overflow;
	NEW_CVALUE( int64, symbol);	SET_CVALUE( int64, symbol, extract_int64_value (symbol, &overflow));
	if (overflow) SET_OVFLOW(int64, symbol);
	NEW_CVALUE(uint64, symbol);	SET_CVALUE(uint64, symbol, extract_uint64_value(symbol, &overflow));
	if (overflow) SET_OVFLOW(uint64, symbol);
	return NULL;
}


void *constant_folding_c::visit(hex_integer_c *symbol) {
	bool overflow;
	NEW_CVALUE( int64, symbol);	SET_CVALUE( int64, symbol, extract_int64_value (symbol, &overflow));
	if (overflow) SET_OVFLOW(int64, symbol);
	NEW_CVALUE(uint64, symbol);	SET_CVALUE(uint64, symbol, extract_uint64_value(symbol, &overflow));
	if (overflow) SET_OVFLOW(uint64, symbol);
	return NULL;
}


/*
integer_literal:
  integer_type_name '#' signed_integer	{$$ = new integer_literal_c($1, $3, locloc(@$));}
| integer_type_name '#' binary_integer	{$$ = new integer_literal_c($1, $3, locloc(@$));}
| integer_type_name '#' octal_integer	{$$ = new integer_literal_c($1, $3, locloc(@$));}
| integer_type_name '#' hex_integer	{$$ = new integer_literal_c($1, $3, locloc(@$));}
*/
// SYM_REF2(integer_literal_c, type, value)
void *constant_folding_c::visit(integer_literal_c *symbol) {
	symbol->value->accept(*this);
	DO_UNARY_OPER( int64, /* none */, symbol->value);
	DO_UNARY_OPER(uint64, /* none */, symbol->value);
	return NULL;
}


void *constant_folding_c::visit(real_literal_c *symbol) {
	symbol->value->accept(*this);
	DO_UNARY_OPER(real64, /* none */, symbol->value);
	return NULL;
}


void *constant_folding_c::visit(bit_string_literal_c *symbol) {
	return NULL;
}


void *constant_folding_c::visit(boolean_literal_c *symbol) {
	symbol->value->accept(*this);
	DO_UNARY_OPER(bool, /* none */, symbol->value);
	return NULL;
}


void *constant_folding_c::visit(boolean_true_c *symbol) {
	NEW_CVALUE(bool, symbol);	SET_CVALUE(bool, symbol, true);
	return NULL;
}


void *constant_folding_c::visit(boolean_false_c *symbol) {
	NEW_CVALUE(bool, symbol);	SET_CVALUE(bool, symbol, false);
	return NULL;
}


/***************************************/
/* B.3 - Language ST (Structured Text) */
/***************************************/
/***********************/
/* B 3.1 - Expressions */
/***********************/
void *constant_folding_c::visit(or_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, ||, bool);
	DO_BINARY_OPER(uint64, | , uint64);
	return NULL;
}


void *constant_folding_c::visit(xor_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, ^, bool);
	DO_BINARY_OPER(uint64, ^, uint64);
	return NULL;
}


void *constant_folding_c::visit(and_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, &&, bool);
	DO_BINARY_OPER(uint64, & , uint64);
	return NULL;
}


void *constant_folding_c::visit(equ_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, ==, bool);
	DO_BINARY_OPER(uint64, ==, bool);
	DO_BINARY_OPER( int64, ==, bool);
	DO_BINARY_OPER(real64, ==, bool);
	return NULL;
}


void *constant_folding_c::visit(notequ_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, !=, bool);
	DO_BINARY_OPER(uint64, !=, bool);
	DO_BINARY_OPER( int64, !=, bool);
	DO_BINARY_OPER(real64, !=, bool);
	return NULL;
}


void *constant_folding_c::visit(lt_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, <, bool);
	DO_BINARY_OPER(uint64, <, bool);
	DO_BINARY_OPER( int64, <, bool);
	DO_BINARY_OPER(real64, <, bool);
	return NULL;
}


void *constant_folding_c::visit(gt_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, >, bool);
	DO_BINARY_OPER(uint64, >, bool);
	DO_BINARY_OPER( int64, >, bool);
	DO_BINARY_OPER(real64, >, bool);
	return NULL;
}


void *constant_folding_c::visit(le_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, <=, bool);
	DO_BINARY_OPER(uint64, <=, bool);
	DO_BINARY_OPER( int64, <=, bool);
	DO_BINARY_OPER(real64, <=, bool);
	return NULL;
}


void *constant_folding_c::visit(ge_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(  bool, >=, bool);
	DO_BINARY_OPER(uint64, >=, bool);
	DO_BINARY_OPER( int64, >=, bool);
	DO_BINARY_OPER(real64, >=, bool);
	return NULL;
}


void *constant_folding_c::visit(add_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(uint64, +, uint64);   CHECK_OVERFLOW_uint64_SUM(symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER( int64, +,  int64);   CHECK_OVERFLOW_int64_SUM (symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER(real64, +, real64);   CHECK_OVERFLOW_real64    (symbol);
	return NULL;
}


void *constant_folding_c::visit(sub_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(uint64, -, uint64);   CHECK_OVERFLOW_uint64_SUB(symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER( int64, -,  int64);   CHECK_OVERFLOW_int64_SUB (symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER(real64, -, real64);   CHECK_OVERFLOW_real64    (symbol);
	return NULL;
}


void *constant_folding_c::visit(mul_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	DO_BINARY_OPER(uint64, *, uint64);   CHECK_OVERFLOW_uint64_MUL(symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER( int64, *,  int64);   CHECK_OVERFLOW_int64_MUL (symbol, symbol->l_exp, symbol->r_exp);
	DO_BINARY_OPER(real64, *, real64);   CHECK_OVERFLOW_real64    (symbol);
	return NULL;
}



void *constant_folding_c::visit(div_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	if (ISZERO_CVALUE(uint64, symbol->r_exp))  {NEW_CVALUE(uint64, symbol); SET_OVFLOW(uint64, symbol);} else {DO_BINARY_OPER(uint64, /, uint64); CHECK_OVERFLOW_uint64_DIV(symbol, symbol->l_exp, symbol->r_exp);};
	if (ISZERO_CVALUE( int64, symbol->r_exp))  {NEW_CVALUE( int64, symbol); SET_OVFLOW( int64, symbol);} else {DO_BINARY_OPER( int64, /,  int64); CHECK_OVERFLOW_int64_DIV(symbol, symbol->l_exp, symbol->r_exp);};
	if (ISZERO_CVALUE(real64, symbol->r_exp))  {NEW_CVALUE(real64, symbol); SET_OVFLOW(real64, symbol);} else {DO_BINARY_OPER(real64, /, real64); CHECK_OVERFLOW_real64(symbol);};
	return NULL;
}


void *constant_folding_c::visit(mod_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	/* IEC 61131-3 standard says IN1 MOD IN2 must be equivalent to
	 *  IF (IN2 = 0) THEN OUT:=0 ; ELSE OUT:=IN1 - (IN1/IN2)*IN2 ; END_IF
	 *
	 * Note that, when IN1 = INT64_MIN, and IN2 = -1, an overflow occurs in the division,
	 * so although the MOD operation should be OK, acording to the above definition, we actually have an overflow!!
	 */
	if (ISZERO_CVALUE(uint64, symbol->r_exp))  {NEW_CVALUE(uint64, symbol); SET_CVALUE(uint64, symbol, 0);} else {DO_BINARY_OPER(uint64, %, uint64); CHECK_OVERFLOW_uint64_MOD(symbol, symbol->l_exp, symbol->r_exp);};
	if (ISZERO_CVALUE( int64, symbol->r_exp))  {NEW_CVALUE( int64, symbol); SET_CVALUE( int64, symbol, 0);} else {DO_BINARY_OPER( int64, %,  int64); CHECK_OVERFLOW_int64_MOD(symbol, symbol->l_exp, symbol->r_exp);};
	return NULL;
}


void *constant_folding_c::visit(power_expression_c *symbol) {
	symbol->l_exp->accept(*this);
	symbol->r_exp->accept(*this);
	/* NOTE: If the const_value in symbol->r_exp is within the limits of both int64 and uint64, then we do both operations.
	 *       That is OK, as the result should be identicial (we do create an unnecessary CVALUE variable, but who cares?).
	 *       If only one is valid, then that is the oper we will do!
	 */
	if (VALID_CVALUE(real64, symbol->l_exp) && VALID_CVALUE( int64, symbol->r_exp)) {
		NEW_CVALUE(real64, symbol);
		SET_CVALUE(real64, symbol, pow(GET_CVALUE(real64, symbol->l_exp), GET_CVALUE( int64, symbol->r_exp)));
	}
	if (VALID_CVALUE(real64, symbol->l_exp) && VALID_CVALUE(uint64, symbol->r_exp)) {
		NEW_CVALUE(real64, symbol);
		SET_CVALUE(real64, symbol, pow(GET_CVALUE(real64, symbol->l_exp), GET_CVALUE(uint64, symbol->r_exp)));
	}
	CHECK_OVERFLOW_real64(symbol);
	return NULL;
}


void *constant_folding_c::visit(neg_expression_c *symbol) {
	symbol->exp->accept(*this);
	DO_UNARY_OPER( int64, -, symbol->exp);	CHECK_OVERFLOW_int64_NEG(symbol, symbol->exp);
	DO_UNARY_OPER(real64, -, symbol->exp);	CHECK_OVERFLOW_real64(symbol);
	return NULL;
}



void *constant_folding_c::visit(not_expression_c *symbol) {
	symbol->exp->accept(*this);
	DO_UNARY_OPER(  bool, !, symbol->exp);
	DO_UNARY_OPER(uint64, ~, symbol->exp);
	return NULL;
}


