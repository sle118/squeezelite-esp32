/*
 *
 *      Sebastien L. 2023, sle118@hotmail.com
 *      Philippe G. 2023, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 *  License Overview:
 *  ----------------
 *  The MIT License is a permissive open source license. As a user of this software, you are free to:
 *  - Use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of this software.
 *  - Use the software for private, commercial, or any other purposes.
 *
 *  Conditions:
 *  - You must include the above copyright notice and this permission notice in all
 *    copies or substantial portions of the Software.
 *
 *  The MIT License offers a high degree of freedom and is well-suited for both open source and
 *  commercial applications. It places minimal restrictions on how the software can be used,
 *  modified, and redistributed. For more details on the MIT License, please refer to the link above.
 */

#ifdef __cplusplus
#include <string>
/**
 * @brief Trims leading and trailing whitespace from a string.
 *
 * This function removes all leading and trailing spaces from the given string.
 * It does not modify the original string but returns a new trimmed string.
 *
 * @param str The string to trim.
 * @return std::string A new string with leading and trailing spaces removed.
 */
std::string trim(const std::string& str);

/**
 * @brief Converts a string to lowercase.
 *
 * This function modifies the given string in place, converting all characters
 * to their lowercase equivalents.
 *
 * @param str Reference to the string to be converted to lowercase.
 * @return std::string& Reference to the modified string.
 */
std::string& toLowerStr(std::string& str);

#endif
