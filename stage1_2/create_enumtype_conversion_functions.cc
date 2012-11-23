/*
 *  matiec - a compiler for the programming languages defined in IEC 61131-3
 *
 *  Copyright (C) 2009-2012  Mario de Sousa (msousa@fe.up.pt)
 *  Copyright (C) 2012       Manuele Conti  (conti.ma@alice.it)
 *
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

#include <sstream>
#include "create_enumtype_conversion_functions.hh"

/* set to 1 to see debug info during execution */
static const int debug = 0;

/*
 * functionDataType array contains all supported data type conversion.
 */
const char *create_enumtype_conversion_functions_c::functionDataType[] = {
		"STRING",
		"SINT"  ,
		"INT"   ,
		"DINT"  ,
		"LINT"  ,
		"USINT" ,
		"UINT"  ,
		"UDINT" ,
		"ULINT" ,
		NULL
};

create_enumtype_conversion_functions_c::create_enumtype_conversion_functions_c(symbol_c *ignore) {

}

create_enumtype_conversion_functions_c::~create_enumtype_conversion_functions_c(void) {

}

std::string &create_enumtype_conversion_functions_c::get_declaration(symbol_c *root) {
    text = "";
    if (NULL != root) {
        root->accept(*this);
    }

    return text;
}

void *create_enumtype_conversion_functions_c::visit(identifier_c *symbol) {
    currentToken = symbol->value;

    return NULL;
}

/**********************/
/* B 1.3 - Data types */
/**********************/
/********************************/
/* B 1.3.3 - Derived data types */
/********************************/
void *create_enumtype_conversion_functions_c::visit(enumerated_type_declaration_c *symbol) {
    std::string enumerateName;
    std::string functionName;
    std::list <std::string> enumerateValues;

    symbol->enumerated_type_name->accept(*this);
    enumerateName = currentToken;

    symbol->enumerated_spec_init->accept(*this);
    enumerateValues = currentTokenList;

    printStringToEnum  (enumerateName, enumerateValues);
    printEnumToString  (enumerateName, enumerateValues);
    for (size_t s = 8; s <= 64; s*= 2) {
        printIntegerToEnum (enumerateName, enumerateValues, true , s);
        printEnumToInteger (enumerateName, enumerateValues, true , s);
        printIntegerToEnum (enumerateName, enumerateValues, false, s);
        printEnumToInteger (enumerateName, enumerateValues, false, s);
    }
    if (debug) std::cout << text << std::endl;
    
    return NULL;
}

void *create_enumtype_conversion_functions_c::visit(enumerated_value_list_c *symbol) {
    list_c *list;

    currentTokenList.clear();
    list = (list_c *)symbol;
    for (int i = 0; i < list->n; i++) {
        list->elements[i]->accept(*this);
        currentTokenList.push_back(currentToken);
    }

    return NULL;
}

/*
 * getIntegerName function generate a integer data name from signed and size.
 */
std::string create_enumtype_conversion_functions_c::getIntegerName(bool isSigned, size_t size) {
    std::string integerType = "";
    if (! isSigned) {
        integerType = "U";
    }
    switch(size) {
    case 8 : integerType += "S"; break;
    case 16:                     break;
    case 32: integerType += "D"; break;
    case 64: integerType += "L"; break;
    default:                     break;
    }
    integerType +="INT";

    return integerType;
}

/*
 * printStringToEnum function print conversion function from STRING to <ENUM>:
 * ST Output:
 *

 FUNCTION STRING_TO_<ENUM> : <ENUM>
  VAR_INPUT
  IN: STRING;
  END_VAR
  IF IN = '<ENUM.VALUE_1>' THEN
   STRING_TO_<ENUM> := <ENUM.VALUE_1>;
   RETURN;
  END_IF;
  ...
  IF IN = '<ENUM.VALU_N>' THEN
   STRING_TO_<ENUM> := <ENUM.VALUE_N>;
   RETURN;
  END_IF;
  END_FUNCTION

  Note: if you change code below remember to update this comment.
 */
void create_enumtype_conversion_functions_c::printStringToEnum  (std::string &enumerateName, std::list<std::string> &enumerateValues) {
    std::list <std::string>::const_iterator itr;
    std::string functionName;

    functionName = "STRING_TO_" + enumerateName;
    text += "FUNCTION " + functionName + " : " + enumerateName;
    text += "\nVAR_INPUT\nIN : STRING;\nEND_VAR\n";
    for (itr = enumerateValues.begin(); itr != enumerateValues.end(); ++itr) {
       std::string value = *itr;
       text += "IF IN = '" + value + "' THEN\n";
       text += " " + functionName + " := " + value + ";\n";
       text += " RETURN;\n";
       text += "END_IF;\n";
    }
    text += "END_FUNCTION\n\n";
}

/*
 * printEnumToString function print conversion function from <ENUM> to STRING:
 * ST Output:
 *

 FUNCTION <ENUM>_TO_STRING : STRING
  VAR_INPUT
  IN: <ENUM>;
  END_VAR
  IF IN = <ENUM.VALUE_1> THEN
   <ENUM>_TO_STRING := '<ENUM.VALUE_1>';
   RETURN;
  END_IF;
  ...
  IF IN = <ENUM.VALUE_N> THEN
   <ENUM>_TO_STRING := '<ENUM.VALUE_N>';
   RETURN;
  END_IF;
  END_FUNCTION

  Note: if you change code below remember to update this comment.
 */
void create_enumtype_conversion_functions_c::printEnumToString  (std::string &enumerateName, std::list<std::string> &enumerateValues) {
    std::list <std::string>::const_iterator itr;
    std::string functionName;

    functionName = enumerateName + "_TO_STRING";
    text += "FUNCTION " + functionName + " : STRING";
    text += "\nVAR_INPUT\nIN : " + enumerateName + ";\nEND_VAR\n";
    for (itr = enumerateValues.begin(); itr != enumerateValues.end(); ++itr) {
        std::string value = *itr;
        text += "IF IN = " + value + " THEN\n";
        text += " " + functionName + " := '" + value + "';\n";
        text += " RETURN;\n";
        text += "END_IF;\n";
    }
    text += "END_FUNCTION\n\n";
}

/*
 * printIntegerToEnum function print conversion function from <INTEGER> to <ENUM>:
 * ST Output:
 *

 FUNCTION <INTEGER>_TO_<ENUM> : <ENUM>
  VAR_INPUT
  IN: <INTEGER>;
  END_VAR
  IF IN = 1 THEN
   <INTEGER>_TO_<ENUM> := <ENUM.VALUE_1>;
   RETURN;
  END_IF;
  ...
  IF IN = N THEN
   <INTEGER>_TO_<ENUM> := <ENUM.VALUE_N>;
   RETURN;
  END_IF;
  END_FUNCTION

  Note: if you change code below remember to update this comment.
 */
void create_enumtype_conversion_functions_c::printIntegerToEnum (std::string &enumerateName, std::list<std::string> &enumerateValues, bool isSigned, size_t size) {
    std::list <std::string>::const_iterator itr;
    std::string functionName;
    std::string integerType;
    int count;

    integerType  = getIntegerName(isSigned, size);
    functionName = integerType + "_TO_" + enumerateName;
    text += "FUNCTION " + functionName + " : " + enumerateName;
    text += "\nVAR_INPUT\nIN : " + integerType + ";\nEND_VAR\n";
    count = 0;
    for (itr = enumerateValues.begin(); itr != enumerateValues.end(); ++itr) {
        std::string value = *itr;
        std::stringstream out;
        out << count;
        text += "IF IN = " + out.str() + " THEN\n";
        text += " " + functionName + " := " + value + ";\n";
        text += " RETURN;\n";
        text += "END_IF;\n";
        count++;
    }
    text += "END_FUNCTION\n\n";
}

/*
 * printEnumToInteger function print conversion function from <ENUM> to <INTEGER>:
 * ST Output:
 *

 FUNCTION <ENUM>_TO_<INTEGER> : <INTEGER>
  VAR_INPUT
  IN: <INTEGER>;
  END_VAR
  IF IN = <ENUM.VALUE_1> THEN
   <ENUM>_TO_<INTEGER> := 1;
   RETURN;
  END_IF;
  ...
  IF IN = <ENUM.VALUE_N> THEN
   <ENUM>_TO_<INTEGER> := N;
   RETURN;
  END_IF;
  END_FUNCTION

  Note: if you change code below remember to update this comment.
 */
void create_enumtype_conversion_functions_c::printEnumToInteger (std::string &enumerateName, std::list<std::string> &enumerateValues, bool isSigned, size_t size) {
    std::list <std::string>::const_iterator itr;
    std::string functionName;
    std::string integerType;
    int count;

    integerType  = getIntegerName(isSigned, size);
    functionName = enumerateName + "_TO_" + integerType;
    text += "FUNCTION " + functionName + " : " + integerType;
    text += "\nVAR_INPUT\nIN : " + enumerateName + ";\nEND_VAR\n";
    count = 0;
    for (itr = enumerateValues.begin(); itr != enumerateValues.end(); ++itr) {
        std::string value = *itr;
        std::stringstream out;
        out << count;
        text += "IF IN = " + value + " THEN\n";
        text += " " + functionName + " := " + out.str() + ";\n";
        text += " RETURN;\n";
        text += "END_IF;\n";
        count++;
    }
    text += "END_FUNCTION\n\n";
}



